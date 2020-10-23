/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/rename_collection.h"

#include "mongo/db/background.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/collection_catalog_entry.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/catalog/drop_collection.h"
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/catalog/index_create.h"
#include "mongo/db/catalog/uuid_catalog.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/index_builder.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/service_context.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {
void dropCollection(OperationContext* opCtx, Database* db, StringData collName) {
    WriteUnitOfWork wunit(opCtx);
    if (db->dropCollection(opCtx, collName).isOK()) {
        // ignoring failure case
        wunit.commit();
    }
}

NamespaceString getNamespaceFromUUID(OperationContext* opCtx, const BSONElement& ui) {
    if (ui.eoo())
        return {};
    auto uuid = uassertStatusOK(UUID::parse(ui));
    Collection* source = UUIDCatalog::get(opCtx).lookupCollectionByUUID(uuid);
    return source ? source->ns() : NamespaceString();
}

Status renameCollectionCommon(OperationContext* opCtx,
                              const NamespaceString& source,
                              const NamespaceString& target,
                              OptionalCollectionUUID targetUUID,
                              repl::OpTime renameOpTimeFromApplyOps,
                              const RenameCollectionOptions& options) {
    // A valid 'renameOpTimeFromApplyOps' is not allowed when writes are replicated.
    if (!renameOpTimeFromApplyOps.isNull() && opCtx->writesAreReplicated()) {
        return Status(
            ErrorCodes::BadValue,
            "renameCollection() cannot accept a rename optime when writes are replicated.");
    }

    DisableDocumentValidation validationDisabler(opCtx);

    boost::optional<Lock::GlobalWrite> globalWriteLock;
    boost::optional<Lock::DBLock> dbWriteLock;

    // If the rename is known not to be a cross-database rename, just a database lock suffices.
    if (source.db() == target.db())
        dbWriteLock.emplace(opCtx, source.db(), MODE_X);
    else
        globalWriteLock.emplace(opCtx);

    // We stay in source context the whole time. This is mostly to set the CurOp namespace.
    OldClientContext ctx(opCtx, source.ns());

    bool userInitiatedWritesAndNotPrimary = opCtx->writesAreReplicated() &&
        !repl::getGlobalReplicationCoordinator()->canAcceptWritesFor(opCtx, source);

    if (userInitiatedWritesAndNotPrimary) {
        return Status(ErrorCodes::NotMaster,
                      str::stream() << "Not primary while renaming collection " << source.ns()
                                    << " to "
                                    << target.ns());
    }

    Database* const sourceDB = dbHolder().get(opCtx, source.db());
    Collection* const sourceColl = sourceDB ? sourceDB->getCollection(opCtx, source) : nullptr;
    if (!sourceColl) {
        if (sourceDB && sourceDB->getViewCatalog()->lookup(opCtx, source.ns()))
            return Status(ErrorCodes::CommandNotSupportedOnView,
                          str::stream() << "cannot rename view: " << source.ns());
        return Status(ErrorCodes::NamespaceNotFound, "source namespace does not exist");
    }

    // Make sure the source collection is not sharded.
    if (CollectionShardingState::get(opCtx, source)->getMetadata()) {
        return {ErrorCodes::IllegalOperation, "source namespace cannot be sharded"};
    }

    // Ensure that collection name does not exceed maximum length.
    // Ensure that index names do not push the length over the max.
    std::string::size_type longestIndexNameLength =
        sourceColl->getIndexCatalog()->getLongestIndexNameLength(opCtx);
    auto status = target.checkLengthForRename(longestIndexNameLength);
    if (!status.isOK()) {
        return status;
    }

    BackgroundOperation::assertNoBgOpInProgForNs(source.ns());

    Database* const targetDB = dbHolder().openDb(opCtx, target.db());

    // Check if the target namespace exists and if dropTarget is true.
    // Return a non-OK status if target exists and dropTarget is not true or if the collection
    // is sharded.
    Collection* targetColl = targetDB->getCollection(opCtx, target);
    if (targetColl) {
        // If we already have the collection with the target UUID, we found our future selves,
        // so nothing left to do but drop the source collection in case of cross-db renames.
        if (targetUUID && targetUUID == targetColl->uuid()) {
            if (source.db() == target.db())
                return Status::OK();
            BSONObjBuilder unusedResult;
            return dropCollection(opCtx,
                                  source,
                                  unusedResult,
                                  {},
                                  DropCollectionSystemCollectionMode::kAllowSystemCollectionDrops);
        }
        if (CollectionShardingState::get(opCtx, target)->getMetadata()) {
            return {ErrorCodes::IllegalOperation, "cannot rename to a sharded collection"};
        }

        if (!options.dropTarget) {
            return Status(ErrorCodes::NamespaceExists, "target namespace exists");
        }

    } else if (targetDB->getViewCatalog()->lookup(opCtx, target.ns())) {
        return Status(ErrorCodes::NamespaceExists,
                      str::stream() << "a view already exists with that name: " << target.ns());
    }

    auto sourceUUID = sourceColl->uuid();
    // If we are renaming in the same database, just rename the namespace and we're done.
    if (sourceDB == targetDB) {
        return writeConflictRetry(opCtx, "renameCollection", target.ns(), [&] {
            WriteUnitOfWork wunit(opCtx);
            auto opObserver = getGlobalServiceContext()->getOpObserver();
            if (!targetColl) {
                // Target collection does not exist.
                auto stayTemp = options.stayTemp;
                {
                    // No logOp necessary because the entire renameCollection command is one logOp.
                    repl::UnreplicatedWritesBlock uwb(opCtx);
                    status = targetDB->renameCollection(opCtx, source.ns(), target.ns(), stayTemp);
                    if (!status.isOK()) {
                        return status;
                    }
                }
                opObserver->onRenameCollection(
                    opCtx, source, target, sourceUUID, options.dropTarget, {}, {}, stayTemp);
                wunit.commit();
                return Status::OK();
            }

            // Target collection exists - drop it.
            invariant(options.dropTarget);
            auto dropTargetUUID = targetColl->uuid();
            auto renameOpTime = opObserver->onRenameCollection(
                opCtx, source, target, sourceUUID, true, dropTargetUUID, {}, options.stayTemp);

            if (!renameOpTimeFromApplyOps.isNull()) {
                // 'renameOpTime' must be null because a valid 'renameOpTimeFromApplyOps' implies
                // replicated writes are not enabled.
                if (!renameOpTime.isNull()) {
                    severe() << "renameCollection: " << source << " to " << target
                             << " (with dropTarget=true) - unexpected renameCollection oplog entry"
                             << " written to the oplog with optime " << renameOpTime;
                    fassertFailed(40616);
                }
                renameOpTime = renameOpTimeFromApplyOps;
            }

            // No logOp necessary because the entire renameCollection command is one logOp.
            repl::UnreplicatedWritesBlock uwb(opCtx);

            status = targetDB->dropCollection(opCtx, target.ns(), renameOpTime);
            if (!status.isOK()) {
                return status;
            }

            status = targetDB->renameCollection(opCtx, source.ns(), target.ns(), options.stayTemp);
            if (!status.isOK()) {
                return status;
            }

            wunit.commit();
            return Status::OK();
        });
    }


    // If we get here, we are renaming across databases, so we must copy all the data and
    // indexes, then remove the source collection.

    // Create a temporary collection in the target database. It will be removed if we fail to copy
    // the collection, or on restart, so there is no need to replicate these writes.
    auto tmpNameResult =
        targetDB->makeUniqueCollectionNamespace(opCtx, "tmp%%%%%.renameCollection");
    if (!tmpNameResult.isOK()) {
        return Status(tmpNameResult.getStatus().code(),
                      str::stream() << "Cannot generate temporary collection name to rename "
                                    << source.ns()
                                    << " to "
                                    << target.ns()
                                    << ": "
                                    << tmpNameResult.getStatus().reason());
    }
    const auto& tmpName = tmpNameResult.getValue();
    Collection* tmpColl = nullptr;
    OptionalCollectionUUID newUUID;
    bool isSourceCollectionTemporary = false;
    {
        auto collectionOptions = sourceColl->getCatalogEntry()->getCollectionOptions(opCtx);
        isSourceCollectionTemporary = collectionOptions.temp;

        // Renaming across databases will result in a new UUID, as otherwise we'd require
        // two collections with the same uuid (temporarily).
        collectionOptions.temp = true;
        if (targetUUID)
            newUUID = targetUUID;
        else if (collectionOptions.uuid && enableCollectionUUIDs)
            newUUID = UUID::gen();

        collectionOptions.uuid = newUUID;

        writeConflictRetry(opCtx, "renameCollection", tmpName.ns(), [&] {
            WriteUnitOfWork wunit(opCtx);

            // No logOp necessary because the entire renameCollection command is one logOp.
            repl::UnreplicatedWritesBlock uwb(opCtx);
            tmpColl = targetDB->createCollection(opCtx,
                                                 tmpName.ns(),
                                                 collectionOptions,
                                                 false);  // _id index build with others later.

            wunit.commit();
        });
    }

    // Dismissed on success
    ScopeGuard tmpCollectionDropper = MakeGuard(dropCollection, opCtx, targetDB, tmpName.ns());

    MultiIndexBlock indexer(opCtx, tmpColl);
    indexer.allowInterruption();
    std::vector<MultiIndexBlock*> indexers{&indexer};

    // Copy the index descriptions from the source collection, adjusting the ns field.
    {
        std::vector<BSONObj> indexesToCopy;
        IndexCatalog::IndexIterator sourceIndIt =
            sourceColl->getIndexCatalog()->getIndexIterator(opCtx, true);
        while (sourceIndIt.more()) {
            const BSONObj currIndex = sourceIndIt.next()->infoObj();

            // Process the source index, adding fields in the same order as they were originally.
            BSONObjBuilder newIndex;
            for (auto&& elem : currIndex) {
                if (elem.fieldNameStringData() == "ns") {
                    newIndex.append("ns", tmpName.ns());
                } else {
                    newIndex.append(elem);
                }
            }
            indexesToCopy.push_back(newIndex.obj());
        }
        indexer.init(indexesToCopy).status_with_transitional_ignore();
    }

    {
        // Copy over all the data from source collection to target collection.
        auto cursor = sourceColl->getCursor(opCtx);
        while (auto record = cursor->next()) {
            opCtx->checkForInterrupt();

            const auto obj = record->data.releaseToBson();

            status = writeConflictRetry(opCtx, "renameCollection", tmpName.ns(), [&] {
                WriteUnitOfWork wunit(opCtx);
                // No logOp necessary because the entire renameCollection command is one logOp.
                repl::UnreplicatedWritesBlock uwb(opCtx);
                Status status = tmpColl->insertDocument(opCtx, obj, indexers, true);
                if (!status.isOK())
                    return status;
                wunit.commit();
                return Status::OK();
            });

            if (!status.isOK()) {
                return status;
            }
        }
    }

    status = indexer.doneInserting();
    if (!status.isOK()) {
        return status;
    }

    // Getting here means we successfully built the target copy. We now do the final
    // in-place rename and remove the source collection.
    status = writeConflictRetry(opCtx, "renameCollection", tmpName.ns(), [&] {
        WriteUnitOfWork wunit(opCtx);
        indexer.commit();
        OptionalCollectionUUID dropTargetUUID;
        {
            repl::UnreplicatedWritesBlock uwb(opCtx);
            Status status = Status::OK();
            if (targetColl) {
                dropTargetUUID = targetColl->uuid();
                status = targetDB->dropCollection(opCtx, target.ns());
            }
            if (status.isOK()) {
                // When renaming the temporary collection in the target database, we have to take
                // into account the CollectionOptions.temp value of the source collection and the
                // 'stayTemp' option requested by the caller.
                // If the source collection is not temporary, the resulting target collection must
                // not be temporary.
                auto stayTemp = isSourceCollectionTemporary && options.stayTemp;
                status = targetDB->renameCollection(opCtx, tmpName.ns(), target.ns(), stayTemp);
            }
            if (status.isOK())
                status = sourceDB->dropCollection(opCtx, source.ns());

            if (!status.isOK())
                return status;
        }

        getGlobalServiceContext()->getOpObserver()->onRenameCollection(opCtx,
                                                                       source,
                                                                       target,
                                                                       newUUID,
                                                                       options.dropTarget,
                                                                       dropTargetUUID,
                                                                       sourceUUID,
                                                                       options.stayTemp);

        wunit.commit();
        return Status::OK();
    });

    if (!status.isOK()) {
        return status;
    }

    tmpCollectionDropper.Dismiss();
    return Status::OK();
}
}  // namespace

Status renameCollection(OperationContext* opCtx,
                        const NamespaceString& source,
                        const NamespaceString& target,
                        const RenameCollectionOptions& options) {
    OptionalCollectionUUID noTargetUUID;
    return renameCollectionCommon(opCtx, source, target, noTargetUUID, {}, options);
}


Status renameCollectionForApplyOps(OperationContext* opCtx,
                                   const std::string& dbName,
                                   const BSONElement& ui,
                                   const BSONObj& cmd,
                                   const repl::OpTime& renameOpTime) {

    const auto sourceNsElt = cmd.firstElement();
    const auto targetNsElt = cmd["to"];
    const auto dropSourceElt = cmd["dropSource"];
    uassert(ErrorCodes::TypeMismatch,
            "'renameCollection' must be of type String",
            sourceNsElt.type() == BSONType::String);
    uassert(ErrorCodes::TypeMismatch,
            "'to' must be of type String",
            targetNsElt.type() == BSONType::String);

    NamespaceString sourceNss(sourceNsElt.valueStringData());
    NamespaceString targetNss(targetNsElt.valueStringData());
    NamespaceString uiNss(getNamespaceFromUUID(opCtx, ui));
    NamespaceString dropSourceNss(getNamespaceFromUUID(opCtx, dropSourceElt));

    // If the UUID we're targeting already exists, rename from there no matter what.
    // When dropSource is specified, the rename is accross databases. In that case, ui indicates
    // the UUID of the new target collection and the dropSourceNss specifies the sourceNss.
    if (!uiNss.isEmpty()) {
        sourceNss = uiNss;
        // The cross-database rename was already done and just needs a local rename, but we may
        // still need to actually remove the source collection.
        auto dropSourceNss = getNamespaceFromUUID(opCtx, dropSourceElt);
        if (!dropSourceNss.isEmpty()) {
            BSONObjBuilder unusedBuilder;
            dropCollection(opCtx,
                           dropSourceNss,
                           unusedBuilder,
                           repl::OpTime(),
                           DropCollectionSystemCollectionMode::kAllowSystemCollectionDrops)
                .ignore();
        }
    } else if (!dropSourceNss.isEmpty()) {
        sourceNss = dropSourceNss;
    } else {
        // When replaying cross-database renames, both source and target collections may no longer
        // exist. Attempting a rename anyway could result in removing a newer collection of the
        // same name.
        uassert(ErrorCodes::NamespaceNotFound,
                str::stream() << "source collection (UUID "
                              << uassertStatusOK(UUID::parse(dropSourceElt))
                              << ") for rename to "
                              << targetNss.ns()
                              << " no longer exists",
                !dropSourceElt);
    }

    OptionalCollectionUUID targetUUID;
    if (!ui.eoo())
        targetUUID = uassertStatusOK(UUID::parse(ui));

    RenameCollectionOptions options;
    options.dropTarget = cmd["dropTarget"].trueValue();
    options.stayTemp = cmd["stayTemp"].trueValue();
    return renameCollectionCommon(opCtx, sourceNss, targetNss, targetUUID, renameOpTime, options);
}
}  // namespace mongo
