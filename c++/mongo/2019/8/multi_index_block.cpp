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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kIndex

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/multi_index_block.h"

#include <ostream>

#include "mongo/base/error_codes.h"
#include "mongo/db/audit.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/index_timestamp_helper.h"
#include "mongo/db/catalog/multi_index_block_gen.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/index/index_build_interceptor.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/multi_key_path_tracker.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/query/collection_query_info.h"
#include "mongo/db/repl/repl_set_config.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/storage/write_unit_of_work.h"
#include "mongo/logger/redaction.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/log.h"
#include "mongo/util/progress_meter.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/uuid.h"

namespace mongo {

namespace {

/**
 * We do not need synchronization with step up and step down. Dropping the RSTL is important because
 * otherwise if we held the RSTL it would create deadlocks with prepared transactions on step up and
 * step down.  A deadlock could result if the index build was attempting to acquire a Collection S
 * or X lock while a prepared transaction held a Collection IX lock, and a step down was waiting to
 * acquire the RSTL in mode X.
 */
void _unlockRSTLForIndexCleanup(OperationContext* opCtx) {
    if (!serverGlobalParams.featureCompatibility.isVersionInitialized()) {
        return;
    }
    opCtx->lockState()->unlockRSTLforPrepare();
    invariant(!opCtx->lockState()->isRSTLLocked());
}

}  // namespace

MONGO_FAIL_POINT_DEFINE(hangAfterSettingUpIndexBuild);
MONGO_FAIL_POINT_DEFINE(hangAfterStartingIndexBuild);
MONGO_FAIL_POINT_DEFINE(hangAfterStartingIndexBuildUnlocked);
MONGO_FAIL_POINT_DEFINE(hangBeforeIndexBuildOf);
MONGO_FAIL_POINT_DEFINE(hangAfterIndexBuildOf);
MONGO_FAIL_POINT_DEFINE(hangAndThenFailIndexBuild);
MONGO_FAIL_POINT_DEFINE(leaveIndexBuildUnfinishedForShutdown);

MultiIndexBlock::~MultiIndexBlock() {
    invariant(_buildIsCleanedUp);
}

void MultiIndexBlock::cleanUpAfterBuild(OperationContext* opCtx, Collection* collection) {
    if (_collectionUUID) {
        // init() was previously called with a collection pointer, so ensure that the same
        // collection is being provided for clean up and the interface in not being abused.
        invariant(_collectionUUID.get() == collection->uuid());
    }

    if (_indexes.empty()) {
        _buildIsCleanedUp = true;
        return;
    }

    if (!_needToCleanup) {
        CollectionQueryInfo::get(collection).clearQueryCache();

        // The temp tables cannot be dropped in commit() because commit() can be called multiple
        // times on write conflict errors and the drop does not rollback in WUOWs.

        // Make lock acquisition uninterruptible.
        UninterruptibleLockGuard noInterrupt(opCtx->lockState());

        // Lock if it's not already locked, to ensure storage engine cannot be destructed out from
        // underneath us.
        boost::optional<Lock::GlobalLock> lk;
        if (!opCtx->lockState()->isWriteLocked()) {
            lk.emplace(opCtx, MODE_IS);
        }

        for (auto& index : _indexes) {
            index.block->deleteTemporaryTables(opCtx);
        }

        _buildIsCleanedUp = true;
        return;
    }

    // Make lock acquisition uninterruptible.
    UninterruptibleLockGuard noInterrupt(opCtx->lockState());
    // Lock the collection if it's not already locked.
    boost::optional<Lock::DBLock> dbLock;
    boost::optional<Lock::CollectionLock> collLock;

    auto nss = collection->ns();

    if (!opCtx->lockState()->isCollectionLockedForMode(nss, MODE_X)) {
        dbLock.emplace(opCtx, nss.db(), MODE_IX);
        // Since DBLock implicitly acquires RSTL, we release the RSTL after acquiring the database
        // lock. Additionally, the RSTL has to be released before acquiring a strong lock (MODE_X)
        // on the collection to avoid potential deadlocks.
        _unlockRSTLForIndexCleanup(opCtx);
        collLock.emplace(opCtx, nss, MODE_X);
    } else {
        _unlockRSTLForIndexCleanup(opCtx);
    }

    while (true) {
        try {
            WriteUnitOfWork wunit(opCtx);
            // This cleans up all index builds. Because that may need to write, it is done inside of
            // a WUOW. Nothing inside this block can fail, and it is made fatal if it does.
            for (size_t i = 0; i < _indexes.size(); i++) {
                _indexes[i].block->fail(opCtx, collection);
                _indexes[i].block->deleteTemporaryTables(opCtx);
            }

            // Nodes building an index on behalf of a user (e.g: `createIndexes`, `applyOps`) may
            // fail, removing the existence of the index from the catalog. This update must be
            // timestamped (unless the build is on an unreplicated collection). A failure from
            // `createIndexes` should not have a commit timestamp and instead write a noop entry. A
            // foreground `applyOps` index build may have a commit timestamp already set.
            if (opCtx->recoveryUnit()->getCommitTimestamp().isNull()) {
                // We must choose a timestamp to write with, as we don't have one handy in the
                // recovery unit already.

                // Simply get a timestamp to write with here; we can't write to the oplog.
                repl::UnreplicatedWritesBlock uwb(opCtx);
                if (!IndexTimestampHelper::setGhostCommitTimestampForCatalogWrite(opCtx, nss)) {
                    log() << "Did not timestamp index abort write.";
                }
            }
            wunit.commit();
            _buildIsCleanedUp = true;
            return;
        } catch (const WriteConflictException&) {
            continue;
        } catch (const DBException& e) {
            if (e.toStatus() == ErrorCodes::ExceededMemoryLimit)
                continue;
            error() << "Caught exception while cleaning up partially built indexes: " << redact(e);
        } catch (const std::exception& e) {
            error() << "Caught exception while cleaning up partially built indexes: " << e.what();
        } catch (...) {
            error() << "Caught unknown exception while cleaning up partially built indexes.";
        }
        fassertFailed(18644);
    }
}

bool MultiIndexBlock::areHybridIndexBuildsEnabled() {
    // The mobile storage engine does not suport dupsAllowed mode on bulk builders, which means that
    // it does not support hybrid builds. See SERVER-38550
    if (storageGlobalParams.engine == "mobile") {
        return false;
    }

    return enableHybridIndexBuilds.load();
}

void MultiIndexBlock::ignoreUniqueConstraint() {
    _ignoreUnique = true;
}

MultiIndexBlock::OnInitFn MultiIndexBlock::kNoopOnInitFn =
    [](std::vector<BSONObj>& specs) -> Status { return Status::OK(); };

MultiIndexBlock::OnInitFn MultiIndexBlock::makeTimestampedIndexOnInitFn(OperationContext* opCtx,
                                                                        const Collection* coll) {
    return [opCtx, ns = coll->ns()](std::vector<BSONObj>& specs) -> Status {
        // This function sets a timestamp for the initial catalog write when beginning an index
        // build, if necessary.  There are four scenarios:

        // 1. A timestamp is already set -- replication application sets a timestamp ahead of time.
        // This could include the phase of initial sync where it applies oplog entries.  Also,
        // primaries performing an index build via `applyOps` may have a wrapping commit timestamp.
        if (!opCtx->recoveryUnit()->getCommitTimestamp().isNull())
            return Status::OK();

        // 2. If the node is initial syncing, we do not set a timestamp.
        auto replCoord = repl::ReplicationCoordinator::get(opCtx);
        if (replCoord->isReplEnabled() && replCoord->getMemberState().startup2())
            return Status::OK();

        // 3. If the index build is on the local database, do not timestamp.
        if (ns.isLocal())
            return Status::OK();

        // 4. All other cases, we generate a timestamp by writing a no-op oplog entry.  This is
        // better than using a ghost timestamp.  Writing an oplog entry ensures this node is
        // primary.
        opCtx->getServiceContext()->getOpObserver()->onOpMessage(
            opCtx, BSON("msg" << std::string(str::stream() << "Creating indexes. Coll: " << ns)));
        return Status::OK();
    };
}

StatusWith<std::vector<BSONObj>> MultiIndexBlock::init(OperationContext* opCtx,
                                                       Collection* collection,
                                                       const BSONObj& spec,
                                                       OnInitFn onInit) {
    const auto indexes = std::vector<BSONObj>(1, spec);
    return init(opCtx, collection, indexes, onInit);
}

StatusWith<std::vector<BSONObj>> MultiIndexBlock::init(OperationContext* opCtx,
                                                       Collection* collection,
                                                       const std::vector<BSONObj>& indexSpecs,
                                                       OnInitFn onInit) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot initialize index builder: " << collection->ns() << " ("
                              << collection->uuid() << "): " << indexSpecs.size()
                              << " provided. First index spec: "
                              << (indexSpecs.empty() ? BSONObj() : indexSpecs[0])};
    }

    _collectionUUID = collection->uuid();

    _buildIsCleanedUp = false;

    WriteUnitOfWork wunit(opCtx);

    invariant(_indexes.empty());

    // On rollback in init(), cleans up _indexes so that ~MultiIndexBlock doesn't try to clean up
    // _indexes manually (since the changes were already rolled back).
    // Due to this, it is thus legal to call init() again after it fails.
    opCtx->recoveryUnit()->onRollback([this, opCtx]() {
        for (auto& index : _indexes) {
            index.block->deleteTemporaryTables(opCtx);
        }
        _indexes.clear();
    });

    const auto& ns = collection->ns().ns();

    const bool enableHybrid = areHybridIndexBuildsEnabled();

    // Parse the specs if this builder is not building hybrid indexes, otherwise log a message.
    for (size_t i = 0; i < indexSpecs.size(); i++) {
        BSONObj info = indexSpecs[i];
        if (enableHybrid) {
            if (info["background"].isBoolean() && !info["background"].Bool()) {
                log() << "ignoring obselete { background: false } index build option because all "
                         "indexes are built in the background with the hybrid method";
            }
            continue;
        }

        // A single foreground build makes the entire builder foreground.
        if (info["background"].trueValue() && _method != IndexBuildMethod::kForeground) {
            _method = IndexBuildMethod::kBackground;
        } else {
            _method = IndexBuildMethod::kForeground;
        }
    }

    std::vector<BSONObj> indexInfoObjs;
    indexInfoObjs.reserve(indexSpecs.size());
    std::size_t eachIndexBuildMaxMemoryUsageBytes = 0;
    if (!indexSpecs.empty()) {
        eachIndexBuildMaxMemoryUsageBytes =
            static_cast<std::size_t>(maxIndexBuildMemoryUsageMegabytes.load()) * 1024 * 1024 /
            indexSpecs.size();
    }

    for (size_t i = 0; i < indexSpecs.size(); i++) {
        BSONObj info = indexSpecs[i];
        StatusWith<BSONObj> statusWithInfo =
            collection->getIndexCatalog()->prepareSpecForCreate(opCtx, info);
        Status status = statusWithInfo.getStatus();
        if (!status.isOK()) {
            // If we were given two identical indexes to build, we will run into an error trying to
            // set up the same index a second time in this for-loop. This is the only way to
            // encounter this error because callers filter out ready/in-progress indexes and start
            // the build while holding a lock throughout.
            if (status == ErrorCodes::IndexBuildAlreadyInProgress) {
                invariant(indexSpecs.size() > 1);
                return {ErrorCodes::OperationFailed,
                        "Cannot build two identical indexes. Try again without duplicate indexes."};
            }
            return status;
        }
        info = statusWithInfo.getValue();
        indexInfoObjs.push_back(info);

        IndexToBuild index;
        index.block = std::make_unique<IndexBuildBlock>(
            collection->getIndexCatalog(), collection->ns(), info, _method);
        status = index.block->init(opCtx, collection);
        if (!status.isOK())
            return status;

        auto indexCleanupGuard =
            makeGuard([opCtx, &index] { index.block->deleteTemporaryTables(opCtx); });

        index.real = index.block->getEntry()->accessMethod();
        status = index.real->initializeAsEmpty(opCtx);
        if (!status.isOK())
            return status;

        // Hybrid builds and non-hybrid foreground builds use the bulk builder.
        const bool useBulk =
            _method == IndexBuildMethod::kHybrid || _method == IndexBuildMethod::kForeground;
        if (useBulk) {
            // Bulk build process requires foreground building as it assumes nothing is changing
            // under it.
            index.bulk = index.real->initiateBulk(eachIndexBuildMaxMemoryUsageBytes);
        }

        const IndexDescriptor* descriptor = index.block->getEntry()->descriptor();

        collection->getIndexCatalog()->prepareInsertDeleteOptions(
            opCtx, descriptor, &index.options);

        // Allow duplicates when explicitly allowed or when using hybrid builds, which will perform
        // duplicate checking itself.
        index.options.dupsAllowed = index.options.dupsAllowed || _ignoreUnique ||
            index.block->getEntry()->isHybridBuilding();
        if (_ignoreUnique) {
            index.options.getKeysMode = IndexAccessMethod::GetKeysMode::kRelaxConstraints;
        }
        index.options.fromIndexBuilder = true;

        log() << "index build: starting on " << ns << " properties: " << descriptor->toString()
              << " using method: " << _method;
        if (index.bulk)
            log() << "build may temporarily use up to "
                  << eachIndexBuildMaxMemoryUsageBytes / 1024 / 1024 << " megabytes of RAM";

        index.filterExpression = index.block->getEntry()->getFilterExpression();

        // TODO SERVER-14888 Suppress this in cases we don't want to audit.
        audit::logCreateIndex(opCtx->getClient(), &info, descriptor->indexName(), ns);

        indexCleanupGuard.dismiss();
        _indexes.push_back(std::move(index));
    }

    if (isBackgroundBuilding())
        _backgroundOperation.reset(new BackgroundOperation(ns));

    Status status = onInit(indexInfoObjs);
    if (!status.isOK()) {
        return status;
    }

    wunit.commit();

    _setState(State::kRunning);

    return indexInfoObjs;
}

void failPointHangDuringBuild(FailPoint* fp, StringData where, const BSONObj& doc) {
    MONGO_FAIL_POINT_BLOCK(*fp, data) {
        int i = doc.getIntField("i");
        if (data.getData()["i"].numberInt() == i) {
            log() << "Hanging " << where << " index build of i=" << i;
            MONGO_FAIL_POINT_PAUSE_WHILE_SET((*fp));
        }
    }
}

Status MultiIndexBlock::insertAllDocumentsInCollection(OperationContext* opCtx,
                                                       Collection* collection) {
    invariant(opCtx->lockState()->isNoop() || !opCtx->lockState()->inAWriteUnitOfWork());

    // UUIDs are not guaranteed during startup because the check happens after indexes are rebuilt.
    if (_collectionUUID) {
        invariant(_collectionUUID.get() == collection->uuid());
    }

    // Refrain from persisting any multikey updates as a result from building the index. Instead,
    // accumulate them in the `MultikeyPathTracker` and do the write as part of the update that
    // commits the index.
    auto stopTracker = makeGuard(
        [this, opCtx] { MultikeyPathTracker::get(opCtx).stopTrackingMultikeyPathInfo(); });
    if (MultikeyPathTracker::get(opCtx).isTrackingMultikeyPathInfo()) {
        stopTracker.dismiss();
    }
    MultikeyPathTracker::get(opCtx).startTrackingMultikeyPathInfo();

    const char* curopMessage = "Index Build: scanning collection";
    const auto numRecords = collection->numRecords(opCtx);
    ProgressMeterHolder progress;
    {
        stdx::unique_lock<Client> lk(*opCtx->getClient());
        progress.set(CurOp::get(opCtx)->setProgress_inlock(curopMessage, numRecords));
    }

    if (MONGO_FAIL_POINT(hangAfterSettingUpIndexBuild)) {
        // Hang the build after the BackgroundOperation and curOP info is set up.
        log() << "Hanging index build due to failpoint 'hangAfterSettingUpIndexBuild'";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterSettingUpIndexBuild);
    }

    if (MONGO_FAIL_POINT(hangAndThenFailIndexBuild)) {
        // Hang the build after the BackgroundOperation and curOP info is set up.
        log() << "Hanging index build due to failpoint 'hangAndThenFailIndexBuild'";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAndThenFailIndexBuild);
        return {ErrorCodes::InternalError,
                "Failed index build because of failpoint 'hangAndThenFailIndexBuild'"};
    }

    Timer t;

    unsigned long long n = 0;

    PlanExecutor::YieldPolicy yieldPolicy;
    if (isBackgroundBuilding()) {
        yieldPolicy = PlanExecutor::YIELD_AUTO;
    } else {
        yieldPolicy = PlanExecutor::WRITE_CONFLICT_RETRY_ONLY;
    }
    auto exec =
        collection->makePlanExecutor(opCtx, yieldPolicy, Collection::ScanDirection::kForward);

    // Hint to the storage engine that this collection scan should not keep data in the cache.
    // Do not use read-once cursors for background builds because saveState/restoreState is called
    // with every insert into the index, which resets the collection scan cursor between every call
    // to getNextSnapshotted(). With read-once cursors enabled, this can evict data we may need to
    // read again, incurring a significant performance penalty.
    // Note: This does not apply to hybrid builds because they write keys to the external sorter.
    bool readOnce =
        _method != IndexBuildMethod::kBackground && useReadOnceCursorsForIndexBuilds.load();
    opCtx->recoveryUnit()->setReadOnce(readOnce);

    Snapshotted<BSONObj> objToIndex;
    RecordId loc;
    PlanExecutor::ExecState state;
    int retries = 0;  // non-zero when retrying our last document.
    while (retries ||
           (PlanExecutor::ADVANCED == (state = exec->getNextSnapshotted(&objToIndex, &loc))) ||
           MONGO_FAIL_POINT(hangAfterStartingIndexBuild)) {
        try {
            auto interruptStatus = opCtx->checkForInterruptNoAssert();
            if (!interruptStatus.isOK())
                return opCtx->checkForInterruptNoAssert();

            if (!retries && PlanExecutor::ADVANCED != state) {
                continue;
            }

            // Make sure we are working with the latest version of the document.
            if (objToIndex.snapshotId() != opCtx->recoveryUnit()->getSnapshotId() &&
                !collection->findDoc(opCtx, loc, &objToIndex)) {
                // Document was deleted so don't index it.
                retries = 0;
                continue;
            }

            // Done before insert so we can retry document if it WCEs.
            progress->setTotalWhileRunning(collection->numRecords(opCtx));

            failPointHangDuringBuild(&hangBeforeIndexBuildOf, "before", objToIndex.value());

            WriteUnitOfWork wunit(opCtx);
            Status ret = insert(opCtx, objToIndex.value(), loc);
            if (_method == IndexBuildMethod::kBackground)
                exec->saveState();
            if (!ret.isOK()) {
                // Fail the index build hard.
                return ret;
            }
            wunit.commit();
            if (_method == IndexBuildMethod::kBackground) {
                try {
                    exec->restoreState();  // Handles any WCEs internally.
                } catch (...) {
                    return exceptionToStatus();
                }
            }

            failPointHangDuringBuild(&hangAfterIndexBuildOf, "after", objToIndex.value());

            // Go to the next document
            progress->hit();
            n++;
            retries = 0;
        } catch (const WriteConflictException&) {
            // Only background builds write inside transactions, and therefore should only ever
            // generate WCEs.
            invariant(_method == IndexBuildMethod::kBackground);

            CurOp::get(opCtx)->debug().additiveMetrics.incrementWriteConflicts(1);
            retries++;  // logAndBackoff expects this to be 1 on first call.
            WriteConflictException::logAndBackoff(retries, "index creation", collection->ns().ns());

            // Can't use writeConflictRetry since we need to save/restore exec around call to
            // abandonSnapshot.
            exec->saveState();
            opCtx->recoveryUnit()->abandonSnapshot();
            try {
                exec->restoreState();  // Handles any WCEs internally.
            } catch (...) {
                return exceptionToStatus();
            }
        }
    }

    if (state != PlanExecutor::IS_EOF) {
        return exec->getMemberObjectStatus(objToIndex.value());
    }

    if (MONGO_FAIL_POINT(leaveIndexBuildUnfinishedForShutdown)) {
        log() << "Index build interrupted due to 'leaveIndexBuildUnfinishedForShutdown' failpoint. "
                 "Mimicing shutdown error code.";
        return Status(
            ErrorCodes::InterruptedAtShutdown,
            "background index build interrupted due to failpoint. returning a shutdown error.");
    }

    if (MONGO_FAIL_POINT(hangAfterStartingIndexBuildUnlocked)) {
        // Unlock before hanging so replication recognizes we've completed.
        Locker::LockSnapshot lockInfo;
        invariant(opCtx->lockState()->saveLockStateAndUnlock(&lockInfo));

        log() << "Hanging index build with no locks due to "
                 "'hangAfterStartingIndexBuildUnlocked' failpoint";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterStartingIndexBuildUnlocked);

        if (isBackgroundBuilding()) {
            opCtx->lockState()->restoreLockState(opCtx, lockInfo);
            opCtx->recoveryUnit()->abandonSnapshot();
            return Status(ErrorCodes::OperationFailed,
                          "background index build aborted due to failpoint");
        } else {
            invariant(
                !"the hangAfterStartingIndexBuildUnlocked failpoint can't be turned off for foreground index builds");
        }
    }

    progress->finished();

    log() << "index build: collection scan done. scanned " << n << " total records in "
          << t.seconds() << " seconds";

    Status ret = dumpInsertsFromBulk(opCtx);
    if (!ret.isOK())
        return ret;

    return Status::OK();
}

Status MultiIndexBlock::insert(OperationContext* opCtx, const BSONObj& doc, const RecordId& loc) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason};
    }

    for (size_t i = 0; i < _indexes.size(); i++) {
        if (_indexes[i].filterExpression && !_indexes[i].filterExpression->matchesBSON(doc)) {
            continue;
        }

        InsertResult result;
        Status idxStatus = Status::OK();
        if (_indexes[i].bulk) {
            idxStatus = _indexes[i].bulk->insert(opCtx, doc, loc, _indexes[i].options);
        } else {
            idxStatus = _indexes[i].real->insert(opCtx, doc, loc, _indexes[i].options, &result);
        }

        if (!idxStatus.isOK())
            return idxStatus;
    }
    return Status::OK();
}

Status MultiIndexBlock::dumpInsertsFromBulk(OperationContext* opCtx) {
    return dumpInsertsFromBulk(opCtx, nullptr);
}

Status MultiIndexBlock::dumpInsertsFromBulk(OperationContext* opCtx,
                                            std::set<RecordId>* dupRecords) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason};
    }

    invariant(opCtx->lockState()->isNoop() || !opCtx->lockState()->inAWriteUnitOfWork());
    for (size_t i = 0; i < _indexes.size(); i++) {
        if (_indexes[i].bulk == nullptr)
            continue;

        // If 'dupRecords' is provided, it will be used to store all records that would result in
        // duplicate key errors. Only pass 'dupKeysInserted', which stores inserted duplicate keys,
        // when 'dupRecords' is not used because these two vectors are mutually incompatible.
        std::vector<BSONObj> dupKeysInserted;

        // When dupRecords is passed, 'dupsAllowed' should be passed to reflect whether or not the
        // index is unique.
        bool dupsAllowed = (dupRecords) ? !_indexes[i].block->getEntry()->descriptor()->unique()
                                        : _indexes[i].options.dupsAllowed;

        IndexCatalogEntry* entry = _indexes[i].block->getEntry();
        LOG(1) << "index build: inserting from external sorter into index: "
               << entry->descriptor()->indexName();
        Status status = _indexes[i].real->commitBulk(opCtx,
                                                     _indexes[i].bulk.get(),
                                                     dupsAllowed,
                                                     dupRecords,
                                                     (dupRecords) ? nullptr : &dupKeysInserted);
        if (!status.isOK()) {
            return status;
        }

        // Do not record duplicates when explicitly ignored. This may be the case on secondaries.
        auto interceptor = entry->indexBuildInterceptor();
        if (!interceptor || _ignoreUnique) {
            continue;
        }

        // Record duplicate key insertions for later verification.
        if (dupKeysInserted.size()) {
            status = interceptor->recordDuplicateKeys(opCtx, dupKeysInserted);
            if (!status.isOK()) {
                return status;
            }
        }
    }

    return Status::OK();
}

Status MultiIndexBlock::drainBackgroundWrites(OperationContext* opCtx,
                                              RecoveryUnit::ReadSource readSource) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot complete drain phase for index build"
                              << (_collectionUUID ? (" on collection '" +
                                                     _collectionUUID.get().toString() + "'")
                                                  : ".")};
    }

    invariant(!opCtx->lockState()->inAWriteUnitOfWork());

    // Drain side-writes table for each index. This only drains what is visible. Assuming intent
    // locks are held on the user collection, more writes can come in after this drain completes.
    // Callers are responsible for stopping writes by holding an S or X lock while draining before
    // completing the index build.
    for (size_t i = 0; i < _indexes.size(); i++) {
        auto interceptor = _indexes[i].block->getEntry()->indexBuildInterceptor();
        if (!interceptor)
            continue;

        auto status = interceptor->drainWritesIntoIndex(opCtx, _indexes[i].options, readSource);
        if (!status.isOK()) {
            return status;
        }
    }
    return Status::OK();
}

Status MultiIndexBlock::checkConstraints(OperationContext* opCtx) {
    _constraintsChecked = true;

    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot complete constraint checking for index build"
                              << (_collectionUUID ? (" on collection '" +
                                                     _collectionUUID.get().toString() + "'")
                                                  : ".")};
    }

    // For each index that may be unique, check that no recorded duplicates still exist. This can
    // only check what is visible on the index. Callers are responsible for ensuring all writes to
    // the collection are visible.
    for (size_t i = 0; i < _indexes.size(); i++) {
        auto interceptor = _indexes[i].block->getEntry()->indexBuildInterceptor();
        if (!interceptor)
            continue;

        auto status = interceptor->checkDuplicateKeyConstraints(opCtx);
        if (!status.isOK()) {
            return status;
        }
    }
    return Status::OK();
}

void MultiIndexBlock::abortWithoutCleanup(OperationContext* opCtx) {
    _setStateToAbortedIfNotCommitted("aborted without cleanup"_sd);

    UninterruptibleLockGuard noInterrupt(opCtx->lockState());
    // Lock if it's not already locked, to ensure storage engine cannot be destructed out from
    // underneath us.
    boost::optional<Lock::GlobalLock> lk;
    if (!opCtx->lockState()->isWriteLocked()) {
        lk.emplace(opCtx, MODE_IS);
    }

    for (auto& index : _indexes) {
        index.block->deleteTemporaryTables(opCtx);
    }
    _indexes.clear();
    _needToCleanup = false;
}

MultiIndexBlock::OnCreateEachFn MultiIndexBlock::kNoopOnCreateEachFn = [](const BSONObj& spec) {};
MultiIndexBlock::OnCommitFn MultiIndexBlock::kNoopOnCommitFn = []() {};

Status MultiIndexBlock::commit(OperationContext* opCtx,
                               Collection* collection,
                               OnCreateEachFn onCreateEach,
                               OnCommitFn onCommit) {
    // UUIDs are not guaranteed during startup because the check happens after indexes are rebuilt.
    if (_collectionUUID) {
        invariant(_collectionUUID.get() == collection->uuid());
    }

    if (State::kAborted == _getState()) {
        return {
            ErrorCodes::IndexBuildAborted,
            str::stream() << "Index build aborted: " << _abortReason
                          << ". Cannot commit index builder: " << collection->ns()
                          << (_collectionUUID ? (" (" + _collectionUUID->toString() + ")") : "")};
    }

    // Ensure that duplicate key constraints were checked at least once.
    invariant(_constraintsChecked);

    // Do not interfere with writing multikey information when committing index builds.
    auto restartTracker = makeGuard(
        [this, opCtx] { MultikeyPathTracker::get(opCtx).startTrackingMultikeyPathInfo(); });
    if (!MultikeyPathTracker::get(opCtx).isTrackingMultikeyPathInfo()) {
        restartTracker.dismiss();
    }
    MultikeyPathTracker::get(opCtx).stopTrackingMultikeyPathInfo();

    for (size_t i = 0; i < _indexes.size(); i++) {
        onCreateEach(_indexes[i].block->getSpec());

        // Do this before calling success(), which unsets the interceptor pointer on the index
        // catalog entry.
        auto interceptor = _indexes[i].block->getEntry()->indexBuildInterceptor();
        if (interceptor) {
            auto multikeyPaths = interceptor->getMultikeyPaths();
            if (multikeyPaths) {
                _indexes[i].block->getEntry()->setMultikey(opCtx, multikeyPaths.get());
            }
        }

        _indexes[i].block->success(opCtx, collection);

        // The bulk builder will track multikey information itself. Non-bulk builders re-use the
        // code path that a typical insert/update uses. State is altered on the non-bulk build
        // path to accumulate the multikey information on the `MultikeyPathTracker`.
        if (_indexes[i].bulk) {
            const auto& bulkBuilder = _indexes[i].bulk;
            if (bulkBuilder->isMultikey()) {
                _indexes[i].block->getEntry()->setMultikey(opCtx, bulkBuilder->getMultikeyPaths());
            }
        } else {
            auto multikeyPaths =
                boost::optional<MultikeyPaths>(MultikeyPathTracker::get(opCtx).getMultikeyPathInfo(
                    collection->ns(), _indexes[i].block->getIndexName()));
            if (multikeyPaths) {
                _indexes[i].block->getEntry()->setMultikey(opCtx, *multikeyPaths);
            }
        }
    }

    onCommit();

    // The state of this index build is set to Committed only when the WUOW commits.
    // It is possible for abort() to be called after the check at the beginning of this function and
    // before the WUOW is committed. If the WUOW commits, the final state of this index builder will
    // be Committed. Otherwise, the index builder state will remain as Aborted and further attempts
    // to commit this index build will fail.
    opCtx->recoveryUnit()->onCommit(
        [this](boost::optional<Timestamp> commitTime) { _setState(State::kCommitted); });

    // On rollback sets MultiIndexBlock::_needToCleanup to true.
    opCtx->recoveryUnit()->onRollback([this]() { _needToCleanup = true; });
    _needToCleanup = false;

    return Status::OK();
}

bool MultiIndexBlock::isCommitted() const {
    return State::kCommitted == _getState();
}

void MultiIndexBlock::abort(StringData reason) {
    _setStateToAbortedIfNotCommitted(reason);
}


bool MultiIndexBlock::isBackgroundBuilding() const {
    return _method == IndexBuildMethod::kBackground || _method == IndexBuildMethod::kHybrid;
}

MultiIndexBlock::State MultiIndexBlock::getState_forTest() const {
    return _getState();
}

MultiIndexBlock::State MultiIndexBlock::_getState() const {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    return _state;
}

void MultiIndexBlock::_setState(State newState) {
    invariant(State::kAborted != newState);
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    _state = newState;
}

void MultiIndexBlock::_setStateToAbortedIfNotCommitted(StringData reason) {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    if (State::kCommitted == _state) {
        return;
    }
    _state = State::kAborted;
    _abortReason = reason.toString();
}

std::ostream& operator<<(std::ostream& os, const MultiIndexBlock::State& state) {
    switch (state) {
        case MultiIndexBlock::State::kUninitialized:
            return os << "Uninitialized";
        case MultiIndexBlock::State::kRunning:
            return os << "Running";
        case MultiIndexBlock::State::kCommitted:
            return os << "Committed";
        case MultiIndexBlock::State::kAborted:
            return os << "Aborted";
    }
    MONGO_UNREACHABLE;
}

logger::LogstreamBuilder& operator<<(logger::LogstreamBuilder& out,
                                     const IndexBuildMethod& method) {
    switch (method) {
        case IndexBuildMethod::kHybrid:
            out.stream() << "Hybrid";
            break;
        case IndexBuildMethod::kBackground:
            out.stream() << "Background";
            break;
        case IndexBuildMethod::kForeground:
            out.stream() << "Foreground";
            break;
    }
    return out;
}

}  // namespace mongo
