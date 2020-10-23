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
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/catalog/index_create.h"
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
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {
static void dropCollection(OperationContext* opCtx, Database* db, StringData collName) {
    WriteUnitOfWork wunit(opCtx);
    if (db->dropCollection(opCtx, collName).isOK()) {
        // ignoring failure case
        wunit.commit();
    }
}
}  // namespace

Status renameCollection(OperationContext* opCtx,
                        const NamespaceString& source,
                        const NamespaceString& target,
                        bool dropTarget,
                        bool stayTemp) {
    DisableDocumentValidation validationDisabler(opCtx);

    Lock::GlobalWrite globalWriteLock(opCtx);
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

    {
        // Ensure that collection name does not exceed maximum length.
        // Ensure that index names do not push the length over the max.
        // Iterator includes unfinished indexes.
        IndexCatalog::IndexIterator sourceIndIt =
            sourceColl->getIndexCatalog()->getIndexIterator(opCtx, true);
        int longestIndexNameLength = 0;
        while (sourceIndIt.more()) {
            int thisLength = sourceIndIt.next()->indexName().length();
            if (thisLength > longestIndexNameLength)
                longestIndexNameLength = thisLength;
        }

        unsigned int longestAllowed =
            std::min(int(NamespaceString::MaxNsCollectionLen),
                     int(NamespaceString::MaxNsLen) - 2 /*strlen(".$")*/ - longestIndexNameLength);
        if (target.size() > longestAllowed) {
            StringBuilder sb;
            sb << "collection name length of " << target.size() << " exceeds maximum length of "
               << longestAllowed << ", allowing for index names";
            return Status(ErrorCodes::InvalidLength, sb.str());
        }
    }

    BackgroundOperation::assertNoBgOpInProgForNs(source.ns());

    Database* const targetDB = dbHolder().openDb(opCtx, target.db());

    // Check if the target namespace exists and if dropTarget is true.
    // Return a non-OK status if target exists and dropTarget is not true or if the collection
    // is sharded.
    Collection* targetColl = targetDB->getCollection(opCtx, target);
    if (targetColl) {
        if (CollectionShardingState::get(opCtx, target)->getMetadata()) {
            return {ErrorCodes::IllegalOperation, "cannot rename to a sharded collection"};
        }

        if (!dropTarget) {
            return Status(ErrorCodes::NamespaceExists, "target namespace exists");
        }

    } else if (targetDB->getViewCatalog()->lookup(opCtx, target.ns())) {
        return Status(ErrorCodes::NamespaceExists,
                      str::stream() << "a view already exists with that name: " << target.ns());
    }

    auto sourceUUID = sourceColl->uuid(opCtx);
    // If we are renaming in the same database, just rename the namespace and we're done.
    if (sourceDB == targetDB) {
        MONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
            WriteUnitOfWork wunit(opCtx);
            OptionalCollectionUUID dropTargetUUID;
            if (targetColl) {
                // No logOp necessary because the entire renameCollection command is one logOp.
                repl::UnreplicatedWritesBlock uwb(opCtx);
                dropTargetUUID = targetColl->uuid(opCtx);
                Status s = targetDB->dropCollection(opCtx, target.ns());
                if (!s.isOK()) {
                    return s;
                }
            }

            Status s = targetDB->renameCollection(opCtx, source.ns(), target.ns(), stayTemp);
            if (!s.isOK()) {
                return s;
            }

            getGlobalServiceContext()->getOpObserver()->onRenameCollection(opCtx,
                                                                           NamespaceString(source),
                                                                           NamespaceString(target),
                                                                           sourceUUID,
                                                                           dropTarget,
                                                                           dropTargetUUID,
                                                                           /*dropSourceUUID*/ {},
                                                                           stayTemp);

            wunit.commit();
        }
        MONGO_WRITE_CONFLICT_RETRY_LOOP_END(opCtx, "renameCollection", target.ns());
        return Status::OK();
    }


    // If we get here, we are renaming across databases, so we must copy all the data and
    // indexes, then remove the source collection.

    // Create a temporary collection in the target database. It will be removed if we fail to copy
    // the collection, or on restart, so there is no need to replicate these writes.
    // A fixed name is not ideal, but there can never be more than one of these due to the global
    // write lock acquired at the top.
    NamespaceString tmpName(target.db(), "tmp.renameCollection");
    Collection* tmpColl = nullptr;
    OptionalCollectionUUID newUUID;
    {
        CollectionOptions options = sourceColl->getCatalogEntry()->getCollectionOptions(opCtx);
        // Renaming across databases will result in a new UUID, as otherwise we'd require
        // two collections with the same uuid (temporarily).
        options.temp = true;
        if (enableCollectionUUIDs) {
            newUUID = UUID::gen();
            options.uuid = newUUID;
        }

        MONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
            WriteUnitOfWork wunit(opCtx);

            // No logOp necessary because the entire renameCollection command is one logOp.
            repl::UnreplicatedWritesBlock uwb(opCtx);
            tmpColl = targetDB->createCollection(opCtx,
                                                 tmpName.ns(),
                                                 options,
                                                 false);  // _id index build with others later.

            wunit.commit();
        }
        MONGO_WRITE_CONFLICT_RETRY_LOOP_END(opCtx, "renameCollection", tmpName.ns());
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
        indexer.init(indexesToCopy);
    }

    {
        // Copy over all the data from source collection to target collection.
        auto cursor = sourceColl->getCursor(opCtx);
        while (auto record = cursor->next()) {
            opCtx->checkForInterrupt();

            const auto obj = record->data.releaseToBson();

            MONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
                WriteUnitOfWork wunit(opCtx);
                // No logOp necessary because the entire renameCollection command is one logOp.
                repl::UnreplicatedWritesBlock uwb(opCtx);
                Status status = tmpColl->insertDocument(opCtx, obj, indexers, true);
                if (!status.isOK())
                    return status;
                wunit.commit();
            }
            MONGO_WRITE_CONFLICT_RETRY_LOOP_END(opCtx, "renameCollection", tmpName.ns());
        }
    }

    Status status = indexer.doneInserting();
    if (!status.isOK())
        return status;

    // Getting here means we successfully built the target copy. We now do the final
    // in-place rename and remove the source collection.
    MONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
        WriteUnitOfWork wunit(opCtx);
        indexer.commit();
        OptionalCollectionUUID dropTargetUUID;
        {
            repl::UnreplicatedWritesBlock uwb(opCtx);
            Status status = Status::OK();
            if (targetColl) {
                dropTargetUUID = targetColl->uuid(opCtx);
                status = targetDB->dropCollection(opCtx, target.ns());
            }
            if (status.isOK())
                status = targetDB->renameCollection(opCtx, tmpName.ns(), target.ns(), stayTemp);
            if (status.isOK())
                status = sourceDB->dropCollection(opCtx, source.ns());

            if (!status.isOK())
                return status;
        }

        getGlobalServiceContext()->getOpObserver()->onRenameCollection(
            opCtx,
            source,
            target,
            newUUID,
            dropTarget,
            dropTargetUUID,
            /*dropSourceUUID*/ sourceUUID,
            stayTemp);

        wunit.commit();
    }
    MONGO_WRITE_CONFLICT_RETRY_LOOP_END(opCtx, "renameCollection", tmpName.ns());

    tmpCollectionDropper.Dismiss();
    return Status::OK();
}
}  // namespace mongo
