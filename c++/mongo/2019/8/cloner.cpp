/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/db/cloner.h"
#include "mongo/db/cloner_gen.h"

#include "mongo/base/status.h"
#include "mongo/bson/unordered_fields_bsonobj_comparator.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/bson/util/builder.h"
#include "mongo/client/authenticate.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/catalog/multi_index_block.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/list_collections_filter.h"
#include "mongo/db/commands/rename_collection.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/ops/insert.h"
#include "mongo/db/repl/isself.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/service_context.h"
#include "mongo/db/storage/durable_catalog.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/fail_point_service.h"
#include "mongo/util/log.h"
#include "mongo/util/str.h"

namespace mongo {

using std::endl;
using std::list;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

using IndexVersion = IndexDescriptor::IndexVersion;

MONGO_FAIL_POINT_DEFINE(movePrimaryFailPoint);

BSONElement getErrField(const BSONObj& o);

BSONObj Cloner::getIdIndexSpec(const std::list<BSONObj>& indexSpecs) {
    for (auto&& indexSpec : indexSpecs) {
        BSONElement indexName;
        uassertStatusOK(bsonExtractTypedField(
            indexSpec, IndexDescriptor::kIndexNameFieldName, String, &indexName));
        if (indexName.valueStringData() == "_id_"_sd) {
            return indexSpec;
        }
    }
    return BSONObj();
}

Cloner::Cloner() {}

struct Cloner::Fun {
    Fun(OperationContext* opCtx, const string& dbName)
        : lastLog(0), opCtx(opCtx), _dbName(dbName) {}

    void operator()(DBClientCursorBatchIterator& i) {
        // XXX: can probably take dblock instead
        unique_ptr<Lock::GlobalWrite> globalWriteLock(new Lock::GlobalWrite(opCtx));
        uassert(
            ErrorCodes::NotMaster,
            str::stream() << "Not primary while cloning collection " << from_collection.ns()
                          << " to " << to_collection.ns(),
            !opCtx->writesAreReplicated() ||
                repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(opCtx, to_collection));

        // Make sure database still exists after we resume from the temp release
        auto databaseHolder = DatabaseHolder::get(opCtx);
        auto db = databaseHolder->openDb(opCtx, _dbName);

        bool createdCollection = false;
        Collection* collection = nullptr;

        collection = db->getCollection(opCtx, to_collection);
        if (!collection) {
            massert(17321,
                    str::stream() << "collection dropped during clone [" << to_collection.ns()
                                  << "]",
                    !createdCollection);
            writeConflictRetry(opCtx, "createCollection", to_collection.ns(), [&] {
                opCtx->checkForInterrupt();

                WriteUnitOfWork wunit(opCtx);
                const bool createDefaultIndexes = true;
                CollectionOptions collectionOptions = uassertStatusOK(CollectionOptions::parse(
                    from_options, CollectionOptions::ParseKind::parseForCommand));
                invariant(db->userCreateNS(opCtx,
                                           to_collection,
                                           collectionOptions,
                                           createDefaultIndexes,
                                           from_id_index),
                          str::stream() << "collection creation failed during clone ["
                                        << to_collection.ns() << "]");
                wunit.commit();
                collection = db->getCollection(opCtx, to_collection);
                invariant(collection,
                          str::stream()
                              << "Missing collection during clone [" << to_collection.ns() << "]");
            });
        }

        const bool isSystemViewsClone = to_collection.isSystemDotViews();

        while (i.moreInCurrentBatch()) {
            if (numSeen % 128 == 127) {
                time_t now = time(nullptr);
                if (now - lastLog >= 60) {
                    // report progress
                    if (lastLog)
                        log() << "clone " << to_collection << ' ' << numSeen;
                    lastLog = now;
                }
                opCtx->checkForInterrupt();

                globalWriteLock.reset();

                CurOp::get(opCtx)->yielded();

                globalWriteLock.reset(new Lock::GlobalWrite(opCtx));

                // Check if everything is still all right.
                if (opCtx->writesAreReplicated()) {
                    uassert(ErrorCodes::PrimarySteppedDown,
                            str::stream() << "Cannot write to ns: " << to_collection.ns()
                                          << " after yielding",
                            repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(
                                opCtx, to_collection));
                }

                // TODO: SERVER-16598 abort if original db or collection is gone.
                db = databaseHolder->getDb(opCtx, _dbName);
                uassert(28593,
                        str::stream() << "Database " << _dbName << " dropped while cloning",
                        db != nullptr);

                collection = db->getCollection(opCtx, to_collection);
                uassert(28594,
                        str::stream()
                            << "Collection " << to_collection.ns() << " dropped while cloning",
                        collection != nullptr);
            }

            BSONObj tmp = i.nextSafe();

            // If copying the system.views collection to a database with a different name, then any
            // view definitions must be modified to refer to the 'to' database.
            if (isSystemViewsClone && from_collection.db() != to_collection.db()) {
                BSONObjBuilder bob;
                for (auto&& item : tmp) {
                    if (item.fieldNameStringData() == "_id") {
                        auto viewNss = NamespaceString(item.checkAndGetStringData());

                        bob.append("_id",
                                   NamespaceString(to_collection.db(), viewNss.coll()).toString());
                    } else {
                        bob.append(item);
                    }
                }
                tmp = bob.obj();
            }

            /* assure object is valid.  note this will slow us down a little. */
            // Use the latest BSON validation version. We allow cloning of collections containing
            // decimal data even if decimal is disabled.
            const Status status = validateBSON(tmp.objdata(), tmp.objsize(), BSONVersion::kLatest);
            if (!status.isOK()) {
                str::stream ss;
                ss << "Cloner: found corrupt document in " << from_collection.toString() << ": "
                   << redact(status);
                if (gSkipCorruptDocumentsWhenCloning.load()) {
                    warning() << ss.ss.str() << "; skipping";
                    continue;
                }
                msgasserted(28531, ss);
            }

            verify(collection);
            ++numSeen;

            writeConflictRetry(opCtx, "cloner insert", to_collection.ns(), [&] {
                opCtx->checkForInterrupt();

                WriteUnitOfWork wunit(opCtx);

                BSONObj doc = tmp;
                OpDebug* const nullOpDebug = nullptr;
                Status status =
                    collection->insertDocument(opCtx, InsertStatement(doc), nullOpDebug, true);
                if (!status.isOK() && status.code() != ErrorCodes::DuplicateKey) {
                    error() << "error: exception cloning object in " << from_collection << ' '
                            << redact(status) << " obj:" << redact(doc);
                    uassertStatusOK(status);
                }
                if (status.isOK()) {
                    wunit.commit();
                }
            });

            static Rarely sampler;
            if (sampler.tick() && (time(nullptr) - saveLast > 60)) {
                log() << numSeen << " objects cloned so far from collection " << from_collection;
                saveLast = time(nullptr);
            }
        }
    }

    time_t lastLog;
    OperationContext* opCtx;
    const string _dbName;

    int64_t numSeen;
    NamespaceString from_collection;
    BSONObj from_options;
    BSONObj from_id_index;
    NamespaceString to_collection;
    time_t saveLast;
    CloneOptions _opts;
};

/* copy the specified collection
 */
void Cloner::copy(OperationContext* opCtx,
                  const string& toDBName,
                  const NamespaceString& from_collection,
                  const BSONObj& from_opts,
                  const BSONObj& from_id_index,
                  const NamespaceString& to_collection,
                  const CloneOptions& opts,
                  Query query) {
    LOG(2) << "\t\tcloning collection " << from_collection << " to " << to_collection << " on "
           << _conn->getServerAddress() << " with filter " << redact(query.toString());

    Fun f(opCtx, toDBName);
    f.numSeen = 0;
    f.from_collection = from_collection;
    f.from_options = from_opts;
    f.from_id_index = from_id_index;
    f.to_collection = to_collection;
    f.saveLast = time(nullptr);
    f._opts = opts;

    int options = QueryOption_NoCursorTimeout | (opts.slaveOk ? QueryOption_SlaveOk : 0) |
        QueryOption_Exhaust;
    {
        Lock::TempRelease tempRelease(opCtx->lockState());
        _conn->query(std::function<void(DBClientCursorBatchIterator&)>(f),
                     from_collection,
                     query,
                     nullptr,
                     options);
    }

    uassert(ErrorCodes::PrimarySteppedDown,
            str::stream() << "Not primary while cloning collection " << from_collection.ns()
                          << " to " << to_collection.ns() << " with filter " << query.toString(),
            !opCtx->writesAreReplicated() ||
                repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(opCtx, to_collection));
}

void Cloner::copyIndexes(OperationContext* opCtx,
                         const string& toDBName,
                         const NamespaceString& from_collection,
                         const BSONObj& from_opts,
                         const std::list<BSONObj>& from_indexes,
                         const NamespaceString& to_collection) {
    LOG(2) << "\t\t copyIndexes " << from_collection << " to " << to_collection << " on "
           << _conn->getServerAddress();

    uassert(ErrorCodes::PrimarySteppedDown,
            str::stream() << "Not primary while copying indexes from " << from_collection.ns()
                          << " to " << to_collection.ns() << " (Cloner)",
            !opCtx->writesAreReplicated() ||
                repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(opCtx, to_collection));

    if (from_indexes.empty())
        return;

    // We are under lock here again, so reload the database in case it may have disappeared
    // during the temp release
    auto databaseHolder = DatabaseHolder::get(opCtx);
    auto db = databaseHolder->openDb(opCtx, toDBName);

    Collection* collection = db->getCollection(opCtx, to_collection);
    if (!collection) {
        writeConflictRetry(opCtx, "createCollection", to_collection.ns(), [&] {
            opCtx->checkForInterrupt();

            WriteUnitOfWork wunit(opCtx);
            CollectionOptions collectionOptions = uassertStatusOK(
                CollectionOptions::parse(from_opts, CollectionOptions::ParseKind::parseForCommand));
            const bool createDefaultIndexes = true;
            invariant(db->userCreateNS(opCtx,
                                       to_collection,
                                       collectionOptions,
                                       createDefaultIndexes,
                                       getIdIndexSpec(from_indexes)),
                      str::stream()
                          << "Collection creation failed while copying indexes from "
                          << from_collection.ns() << " to " << to_collection.ns() << " (Cloner)");
            wunit.commit();
            collection = db->getCollection(opCtx, to_collection);
            invariant(collection,
                      str::stream() << "Missing collection " << to_collection.ns() << " (Cloner)");
        });
    }

    auto indexCatalog = collection->getIndexCatalog();
    auto indexesToBuild = indexCatalog->removeExistingIndexesNoChecks(
        opCtx, {std::begin(from_indexes), std::end(from_indexes)});
    if (indexesToBuild.empty()) {
        return;
    }

    // TODO pass the MultiIndexBlock when inserting into the collection rather than building the
    // indexes after the fact. This depends on holding a lock on the collection the whole time
    // from creation to completion without yielding to ensure the index and the collection
    // matches. It also wouldn't work on non-empty collections so we would need both
    // implementations anyway as long as that is supported.
    MultiIndexBlock indexer;

    // The code below throws, so ensure build cleanup occurs.
    ON_BLOCK_EXIT([&] { indexer.cleanUpAfterBuild(opCtx, collection); });

    auto indexInfoObjs = uassertStatusOK(
        indexer.init(opCtx, collection, indexesToBuild, MultiIndexBlock::kNoopOnInitFn));
    uassertStatusOK(indexer.insertAllDocumentsInCollection(opCtx, collection));
    uassertStatusOK(indexer.checkConstraints(opCtx));

    WriteUnitOfWork wunit(opCtx);
    uassertStatusOK(indexer.commit(
        opCtx, collection, MultiIndexBlock::kNoopOnCreateEachFn, MultiIndexBlock::kNoopOnCommitFn));
    if (opCtx->writesAreReplicated()) {
        for (auto&& infoObj : indexInfoObjs) {
            getGlobalServiceContext()->getOpObserver()->onCreateIndex(
                opCtx, collection->ns(), collection->uuid(), infoObj, false);
        }
    }
    wunit.commit();
}

bool Cloner::copyCollection(OperationContext* opCtx,
                            const string& ns,
                            const BSONObj& query,
                            string& errmsg,
                            bool shouldCopyIndexes,
                            CollectionOptions::ParseKind optionsParser) {
    const NamespaceString nss(ns);
    const string dbname = nss.db().toString();

    // config
    BSONObj filter = BSON("name" << nss.coll().toString());
    list<BSONObj> collList = _conn->getCollectionInfos(dbname, filter);
    BSONObjBuilder optionsBob;
    bool shouldCreateCollection = false;

    if (!collList.empty()) {
        invariant(collList.size() <= 1);
        shouldCreateCollection = true;
        BSONObj col = collList.front();

        // Confirm that 'col' is not a view.
        {
            std::string namespaceType;
            auto status = bsonExtractStringField(col, "type", &namespaceType);

            uassert(ErrorCodes::InternalError,
                    str::stream() << "Collection 'type' expected to be a string: " << col,
                    ErrorCodes::TypeMismatch != status.code());

            uassert(ErrorCodes::CommandNotSupportedOnView,
                    str::stream() << "copyCollection not supported for views. ns: "
                                  << col["name"].valueStringData(),
                    !(status.isOK() && namespaceType == "view"));
        }

        if (col["options"].isABSONObj()) {
            optionsBob.appendElements(col["options"].Obj());
        }
        if ((optionsParser == CollectionOptions::parseForStorage) && col["info"].isABSONObj()) {
            auto info = col["info"].Obj();
            if (info.hasField("uuid")) {
                optionsBob.append(info.getField("uuid"));
            }
        }
    }
    BSONObj options = optionsBob.obj();

    auto sourceIndexes = _conn->getIndexSpecs(nss.ns(), QueryOption_SlaveOk);
    auto idIndexSpec = getIdIndexSpec(sourceIndexes);

    Lock::DBLock dbWrite(opCtx, dbname, MODE_X);

    uassert(ErrorCodes::PrimarySteppedDown,
            str::stream() << "Not primary while copying collection " << ns << " (Cloner)",
            !opCtx->writesAreReplicated() ||
                repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(opCtx, nss));

    auto databaseHolder = DatabaseHolder::get(opCtx);
    auto db = databaseHolder->openDb(opCtx, dbname);

    if (shouldCreateCollection) {
        bool result = writeConflictRetry(opCtx, "createCollection", ns, [&] {
            opCtx->checkForInterrupt();

            WriteUnitOfWork wunit(opCtx);
            CollectionOptions collectionOptions =
                uassertStatusOK(CollectionOptions::parse(options, optionsParser));
            const bool createDefaultIndexes = true;
            Status status =
                db->userCreateNS(opCtx, nss, collectionOptions, createDefaultIndexes, idIndexSpec);
            if (!status.isOK()) {
                errmsg = status.toString();
                // abort write unit of work
                return false;
            }

            wunit.commit();
            return true;
        });

        if (!result) {
            return result;
        }
    } else {
        LOG(1) << "No collection info found for ns:" << nss.toString()
               << ", host:" << _conn->getServerAddress();
    }

    // main data
    CloneOptions opts;
    opts.slaveOk = true;
    copy(opCtx, dbname, nss, options, idIndexSpec, nss, opts, Query(query));

    /* TODO : copyIndexes bool does not seem to be implemented! */
    if (!shouldCopyIndexes) {
        log() << "ERROR copy collection shouldCopyIndexes not implemented? " << ns;
    }

    // indexes
    copyIndexes(opCtx, dbname, NamespaceString(ns), options, sourceIndexes, NamespaceString(ns));

    return true;
}

StatusWith<std::vector<BSONObj>> Cloner::filterCollectionsForClone(
    const CloneOptions& opts, const std::list<BSONObj>& initialCollections) {
    std::vector<BSONObj> finalCollections;
    for (auto&& collection : initialCollections) {
        LOG(2) << "\t cloner got " << collection;

        BSONElement collectionOptions = collection["options"];
        if (collectionOptions.isABSONObj()) {
            auto statusWithCollectionOptions = CollectionOptions::parse(
                collectionOptions.Obj(), CollectionOptions::ParseKind::parseForCommand);
            if (!statusWithCollectionOptions.isOK()) {
                return statusWithCollectionOptions.getStatus();
            }
        }

        std::string collectionName;
        auto status = bsonExtractStringField(collection, "name", &collectionName);
        if (!status.isOK()) {
            return status;
        }

        const NamespaceString ns(opts.fromDB, collectionName.c_str());

        if (ns.isSystem()) {
            if (!ns.isLegalClientSystemNS()) {
                LOG(2) << "\t\t not cloning because system collection";
                continue;
            }
        }

        finalCollections.push_back(collection.getOwned());
    }
    return finalCollections;
}

Status Cloner::createCollectionsForDb(
    OperationContext* opCtx,
    const std::vector<CreateCollectionParams>& createCollectionParams,
    const std::string& dbName,
    const CloneOptions& opts) {
    auto databaseHolder = DatabaseHolder::get(opCtx);
    auto db = databaseHolder->openDb(opCtx, dbName);
    invariant(opCtx->lockState()->isDbLockedForMode(dbName, MODE_X));

    auto collCount = 0;
    for (auto&& params : createCollectionParams) {
        if (MONGO_FAIL_POINT(movePrimaryFailPoint) && collCount > 0) {
            return Status(ErrorCodes::CommandFailed, "movePrimary failed due to failpoint");
        }
        collCount++;

        BSONObjBuilder optionsBuilder;
        optionsBuilder.appendElements(params.collectionInfo["options"].Obj());

        const NamespaceString nss(dbName, params.collectionName);

        uassertStatusOK(userAllowedCreateNS(dbName, params.collectionName));
        Status status = writeConflictRetry(opCtx, "createCollection", nss.ns(), [&] {
            opCtx->checkForInterrupt();
            WriteUnitOfWork wunit(opCtx);

            Collection* collection = db->getCollection(opCtx, nss);
            if (collection) {
                if (!params.shardedColl) {
                    // If the collection is unsharded then we want to fail when a collection
                    // we're trying to create already exists.
                    return Status(ErrorCodes::NamespaceExists,
                                  str::stream() << "unsharded collection with same namespace "
                                                << nss.ns() << " already exists.");
                }

                // If the collection is sharded and a collection with the same name already
                // exists on the target, we check if the existing collection's UUID matches
                // that of the one we're trying to create. If it does, we treat the create
                // as a no-op; if it doesn't match, we return an error.
                auto existingOpts =
                    DurableCatalog::get(opCtx)->getCollectionOptions(opCtx, collection->ns());
                const UUID clonedUUID =
                    uassertStatusOK(UUID::parse(params.collectionInfo["info"]["uuid"]));

                if (clonedUUID == existingOpts.uuid)
                    return Status::OK();

                return Status(ErrorCodes::InvalidOptions,
                              str::stream()
                                  << "sharded collection with same namespace " << nss.ns()
                                  << " already exists, but UUIDs don't match. Existing UUID is "
                                  << existingOpts.uuid << " and new UUID is " << clonedUUID);
            }

            // If the collection does not already exist and is sharded, we create a new
            // collection on the target shard with the UUID of the original collection and
            // copy the options and secondary indexes. If the collection does not already
            // exist and is unsharded, we create a new collection with its own UUID and
            // copy the options and secondary indexes of the original collection.

            if (params.shardedColl) {
                optionsBuilder.append(params.collectionInfo["info"]["uuid"]);
            }

            const bool createDefaultIndexes = true;
            auto options = optionsBuilder.obj();

            CollectionOptions collectionOptions = uassertStatusOK(
                CollectionOptions::parse(options, CollectionOptions::ParseKind::parseForStorage));
            Status createStatus = db->userCreateNS(
                opCtx, nss, collectionOptions, createDefaultIndexes, params.idIndexSpec);
            if (!createStatus.isOK()) {
                return createStatus;
            }

            wunit.commit();
            return Status::OK();
        });

        // Break early if one of the creations fails.
        if (!status.isOK()) {
            return status;
        }
    }

    return Status::OK();
}

Status Cloner::copyDb(OperationContext* opCtx,
                      const std::string& toDBName,
                      const string& masterHost,
                      const CloneOptions& opts,
                      set<string>* clonedColls,
                      std::vector<BSONObj> collectionsToClone) {
    massert(10289,
            "useReplAuth is not written to replication log",
            !opts.useReplAuth || !opCtx->writesAreReplicated());

    auto statusWithMasterHost = ConnectionString::parse(masterHost);
    if (!statusWithMasterHost.isOK()) {
        return statusWithMasterHost.getStatus();
    }

    const ConnectionString cs(statusWithMasterHost.getValue());

    bool masterSameProcess = false;
    std::vector<HostAndPort> csServers = cs.getServers();
    for (std::vector<HostAndPort>::const_iterator iter = csServers.begin(); iter != csServers.end();
         ++iter) {
        if (!repl::isSelf(*iter, opCtx->getServiceContext()))
            continue;

        masterSameProcess = true;
        break;
    }

    if (masterSameProcess) {
        if (opts.fromDB == toDBName) {
            // Guard against re-entrance
            return Status(ErrorCodes::IllegalOperation, "can't clone from self (localhost)");
        }
    }

    {
        // setup connection
        if (_conn.get()) {
            // nothing to do
        } else if (!masterSameProcess) {
            std::string errmsg;
            unique_ptr<DBClientBase> con(cs.connect(StringData(), errmsg));
            if (!con.get()) {
                return Status(ErrorCodes::HostUnreachable, errmsg);
            }

            if (auth::isInternalAuthSet()) {
                auto authStatus = con->authenticateInternalUser();
                if (!authStatus.isOK()) {
                    return authStatus;
                }
            }

            _conn = std::move(con);
        } else {
            _conn.reset(new DBDirectClient(opCtx));
        }
    }

    // Gather the list of collections to clone
    std::vector<BSONObj> toClone;
    if (clonedColls) {
        clonedColls->clear();
    }

    if (opts.createCollections) {
        // getCollectionInfos may make a remote call, which may block indefinitely, so release
        // the global lock that we are entering with.
        Lock::TempRelease tempRelease(opCtx->lockState());

        std::list<BSONObj> initialCollections = _conn->getCollectionInfos(
            opts.fromDB, ListCollectionsFilter::makeTypeCollectionFilter());

        auto status = filterCollectionsForClone(opts, initialCollections);
        if (!status.isOK()) {
            return status.getStatus();
        }
        toClone = status.getValue();
    } else {
        toClone = collectionsToClone;
    }

    std::vector<CreateCollectionParams> createCollectionParams;
    for (auto&& collection : toClone) {
        CreateCollectionParams params;
        params.collectionName = collection["name"].String();
        params.collectionInfo = collection;
        if (auto idIndex = collection["idIndex"]) {
            params.idIndexSpec = idIndex.Obj();
        }

        const NamespaceString ns(opts.fromDB, params.collectionName);
        if (opts.shardedColls.find(ns.ns()) != opts.shardedColls.end()) {
            params.shardedColl = true;
        }
        createCollectionParams.push_back(params);
    }

    // Get index specs for each collection.
    std::map<StringData, std::list<BSONObj>> collectionIndexSpecs;
    {
        Lock::TempRelease tempRelease(opCtx->lockState());
        for (auto&& params : createCollectionParams) {
            const NamespaceString nss(opts.fromDB, params.collectionName);
            auto indexSpecs =
                _conn->getIndexSpecs(nss.ns(), opts.slaveOk ? QueryOption_SlaveOk : 0);

            collectionIndexSpecs[params.collectionName] = indexSpecs;

            if (params.idIndexSpec.isEmpty()) {
                params.idIndexSpec = getIdIndexSpec(indexSpecs);
            }
        }
    }

    uassert(
        ErrorCodes::NotMaster,
        str::stream() << "Not primary while cloning database " << opts.fromDB
                      << " (after getting list of collections to clone)",
        !opCtx->writesAreReplicated() ||
            repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesForDatabase(opCtx, toDBName));

    if (opts.syncData) {
        if (opts.createCollections) {
            Status status = createCollectionsForDb(opCtx, createCollectionParams, toDBName, opts);
            if (!status.isOK()) {
                return status;
            }
        }

        for (auto&& params : createCollectionParams) {
            if (params.shardedColl) {
                continue;
            }

            LOG(2) << "  really will clone: " << params.collectionInfo;

            const NamespaceString from_name(opts.fromDB, params.collectionName);
            const NamespaceString to_name(toDBName, params.collectionName);

            if (clonedColls) {
                clonedColls->insert(from_name.ns());
            }

            LOG(1) << "\t\t cloning " << from_name << " -> " << to_name;

            copy(opCtx,
                 toDBName,
                 from_name,
                 params.collectionInfo["options"].Obj(),
                 params.idIndexSpec,
                 to_name,
                 opts,
                 Query());
        }
    }

    // now build the secondary indexes
    if (opts.syncIndexes) {
        for (auto&& params : createCollectionParams) {
            log() << "copying indexes for: " << params.collectionInfo;

            const NamespaceString from_name(opts.fromDB, params.collectionName);
            const NamespaceString to_name(toDBName, params.collectionName);


            copyIndexes(opCtx,
                        toDBName,
                        from_name,
                        params.collectionInfo["options"].Obj(),
                        collectionIndexSpecs[params.collectionName],
                        to_name);
        }
    }

    return Status::OK();
}

}  // namespace mongo
