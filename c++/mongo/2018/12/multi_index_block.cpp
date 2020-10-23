
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
#include "mongo/db/catalog/multi_index_block_gen.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/index/index_build_interceptor.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/multi_key_path_tracker.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/repl_set_config.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/storage/write_unit_of_work.h"
#include "mongo/logger/redaction.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/fail_point_service.h"
#include "mongo/util/log.h"
#include "mongo/util/progress_meter.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/uuid.h"

namespace mongo {

namespace {

const StringData kBuildUUIDFieldName = "buildUUID"_sd;
const StringData kBuildingPhaseCompleteFieldName = "buildingPhaseComplete"_sd;
const StringData kRunTwoPhaseIndexBuildFieldName = "runTwoPhaseIndexBuild"_sd;
const StringData kCommitReadyMembersFieldName = "commitReadyMembers"_sd;

}  // namespace

MONGO_FAIL_POINT_DEFINE(crashAfterStartingIndexBuild);
MONGO_FAIL_POINT_DEFINE(hangAfterStartingIndexBuild);
MONGO_FAIL_POINT_DEFINE(hangAfterStartingIndexBuildUnlocked);
MONGO_FAIL_POINT_DEFINE(hangBeforeIndexBuildOf);
MONGO_FAIL_POINT_DEFINE(hangAfterIndexBuildOf);

MultiIndexBlock::MultiIndexBlock(OperationContext* opCtx, Collection* collection)
    : _collection(collection), _opCtx(opCtx) {}

MultiIndexBlock::~MultiIndexBlock() {
    if (!_needToCleanup && !_indexes.empty()) {
        _collection->infoCache()->clearQueryCache();
    }

    if (!_needToCleanup || _indexes.empty())
        return;

    // Make lock acquisition uninterruptible because onOpMessage() can take locks.
    UninterruptibleLockGuard noInterrupt(_opCtx->lockState());

    while (true) {
        try {
            WriteUnitOfWork wunit(_opCtx);
            // This cleans up all index builds. Because that may need to write, it is done inside of
            // a WUOW. Nothing inside this block can fail, and it is made fatal if it does.
            for (size_t i = 0; i < _indexes.size(); i++) {
                _indexes[i].block->fail();
            }

            auto replCoord = repl::ReplicationCoordinator::get(_opCtx);
            // Nodes building an index on behalf of a user (e.g: `createIndexes`, `applyOps`) may
            // fail, removing the existence of the index from the catalog. This update must be
            // timestamped. A failure from `createIndexes` should not have a commit timestamp and
            // instead write a noop entry. A foreground `applyOps` index build may have a commit
            // timestamp already set.
            if (_opCtx->recoveryUnit()->getCommitTimestamp().isNull() &&
                replCoord->canAcceptWritesForDatabase(_opCtx, "admin")) {
                _opCtx->getServiceContext()->getOpObserver()->onOpMessage(
                    _opCtx,
                    BSON("msg" << std::string(str::stream() << "Failing index builds. Coll: "
                                                            << _collection->ns().ns())));
            }
            wunit.commit();
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

void MultiIndexBlock::allowBackgroundBuilding() {
    _buildInBackground = true;
}

void MultiIndexBlock::allowInterruption() {
    _allowInterruption = true;
}

void MultiIndexBlock::ignoreUniqueConstraint() {
    _ignoreUnique = true;
}

StatusWith<std::vector<BSONObj>> MultiIndexBlock::init(const BSONObj& spec) {
    const auto indexes = std::vector<BSONObj>(1, spec);
    return init(indexes);
}

StatusWith<std::vector<BSONObj>> MultiIndexBlock::init(const std::vector<BSONObj>& indexSpecs) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot initialize index builder: "
                              << _collection->ns().ns()
                              << "("
                              << *_collection->uuid()
                              << "): "
                              << indexSpecs.size()
                              << " provided. First index spec: "
                              << (indexSpecs.empty() ? BSONObj() : indexSpecs[0])};
    }

    _updateCurOpOpDescription(false);

    WriteUnitOfWork wunit(_opCtx);

    invariant(_indexes.empty());

    // On rollback in init(), cleans up _indexes so that ~MultiIndexBlock doesn't try to clean up
    // _indexes manually (since the changes were already rolled back).
    // Due to this, it is thus legal to call init() again after it fails.
    _opCtx->recoveryUnit()->onRollback([this]() { _indexes.clear(); });

    const auto& ns = _collection->ns().ns();

    const auto idxCat = _collection->getIndexCatalog();
    invariant(idxCat);
    invariant(idxCat->ok());
    Status status = idxCat->checkUnfinished();
    if (!status.isOK())
        return status;

    for (size_t i = 0; i < indexSpecs.size(); i++) {
        BSONObj info = indexSpecs[i];

        // Any foreground indexes make all indexes be built in the foreground.
        _buildInBackground = (_buildInBackground && info["background"].trueValue());
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
            _collection->getIndexCatalog()->prepareSpecForCreate(_opCtx, info);
        Status status = statusWithInfo.getStatus();
        if (!status.isOK())
            return status;
        info = statusWithInfo.getValue();
        indexInfoObjs.push_back(info);

        IndexToBuild index;
        index.block = _collection->getIndexCatalog()->createIndexBuildBlock(_opCtx, info);
        status = index.block->init();
        if (!status.isOK())
            return status;

        index.real = index.block->getEntry()->accessMethod();
        status = index.real->initializeAsEmpty(_opCtx);
        if (!status.isOK())
            return status;

        // Foreground builds and background builds using an interceptor can use the bulk builder.
        const bool useBulk =
            !_buildInBackground || index.block->getEntry()->indexBuildInterceptor();
        if (useBulk) {
            // Bulk build process requires foreground building as it assumes nothing is changing
            // under it.
            index.bulk = index.real->initiateBulk(eachIndexBuildMaxMemoryUsageBytes);
        }

        const IndexDescriptor* descriptor = index.block->getEntry()->descriptor();

        _collection->getIndexCatalog()->prepareInsertDeleteOptions(
            _opCtx, descriptor, &index.options);
        // Allow duplicates when explicitly allowed or an interceptor is installed, which will
        // perform duplicate checking itself.
        index.options.dupsAllowed = index.options.dupsAllowed || _ignoreUnique ||
            index.block->getEntry()->indexBuildInterceptor();
        if (_ignoreUnique) {
            index.options.getKeysMode = IndexAccessMethod::GetKeysMode::kRelaxConstraints;
        }
        index.options.fromIndexBuilder = true;

        log() << "build index on: " << ns << " properties: " << descriptor->toString();
        if (index.bulk)
            log() << "\t building index using bulk method; build may temporarily use up to "
                  << eachIndexBuildMaxMemoryUsageBytes / 1024 / 1024 << " megabytes of RAM";

        index.filterExpression = index.block->getEntry()->getFilterExpression();

        // TODO SERVER-14888 Suppress this in cases we don't want to audit.
        audit::logCreateIndex(_opCtx->getClient(), &info, descriptor->indexName(), ns);

        _indexes.push_back(std::move(index));
    }

    if (_buildInBackground)
        _backgroundOperation.reset(new BackgroundOperation(ns));

    auto replCoord = repl::ReplicationCoordinator::get(_opCtx);
    if (_opCtx->recoveryUnit()->getCommitTimestamp().isNull() &&
        replCoord->canAcceptWritesForDatabase(_opCtx, "admin")) {
        // Only primaries must timestamp this write. Secondaries run this from within a
        // `TimestampBlock`. Primaries performing an index build via `applyOps` may have a
        // wrapping commit timestamp that will be used instead.
        _opCtx->getServiceContext()->getOpObserver()->onOpMessage(
            _opCtx, BSON("msg" << std::string(str::stream() << "Creating indexes. Coll: " << ns)));
    }

    wunit.commit();

    if (MONGO_FAIL_POINT(crashAfterStartingIndexBuild)) {
        log() << "Index build interrupted due to 'crashAfterStartingIndexBuild' failpoint. Exiting "
                 "after waiting for changes to become durable.";
        Locker::LockSnapshot lockInfo;
        invariant(_opCtx->lockState()->saveLockStateAndUnlock(&lockInfo));
        if (_opCtx->recoveryUnit()->waitUntilDurable()) {
            quickExit(EXIT_TEST);
        }
    }

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

Status MultiIndexBlock::insertAllDocumentsInCollection() {
    invariant(_opCtx->lockState()->isNoop() || !_opCtx->lockState()->inAWriteUnitOfWork());

    // Refrain from persisting any multikey updates as a result from building the index. Instead,
    // accumulate them in the `MultikeyPathTracker` and do the write as part of the update that
    // commits the index.
    auto stopTracker =
        MakeGuard([this] { MultikeyPathTracker::get(_opCtx).stopTrackingMultikeyPathInfo(); });
    if (MultikeyPathTracker::get(_opCtx).isTrackingMultikeyPathInfo()) {
        stopTracker.Dismiss();
    }
    MultikeyPathTracker::get(_opCtx).startTrackingMultikeyPathInfo();

    const char* curopMessage = _buildInBackground ? "Index Build (background)" : "Index Build";
    const auto numRecords = _collection->numRecords(_opCtx);
    stdx::unique_lock<Client> lk(*_opCtx->getClient());
    ProgressMeterHolder progress(
        CurOp::get(_opCtx)->setMessage_inlock(curopMessage, curopMessage, numRecords));
    lk.unlock();

    Timer t;

    unsigned long long n = 0;

    PlanExecutor::YieldPolicy yieldPolicy;
    if (_buildInBackground) {
        invariant(_allowInterruption);
        yieldPolicy = PlanExecutor::YIELD_AUTO;
    } else {
        yieldPolicy = PlanExecutor::WRITE_CONFLICT_RETRY_ONLY;
    }
    auto exec =
        _collection->makePlanExecutor(_opCtx, yieldPolicy, Collection::ScanDirection::kForward);

    // Hint to the storage engine that this collection scan should not keep data in the cache.
    // Do not use read-once cursors for background builds because saveState/restoreState is called
    // with every insert into the index, which resets the collection scan cursor between every call
    // to getNextSnapshotted(). With read-once cursors enabled, this can evict data we may need to
    // read again, incurring a significant performance penalty.
    // TODO: Enable this for all index builds when SERVER-37268 is complete.
    bool readOnce = !_buildInBackground && useReadOnceCursorsForIndexBuilds.load();
    _opCtx->recoveryUnit()->setReadOnce(readOnce);

    Snapshotted<BSONObj> objToIndex;
    RecordId loc;
    PlanExecutor::ExecState state;
    int retries = 0;  // non-zero when retrying our last document.
    while (retries ||
           (PlanExecutor::ADVANCED == (state = exec->getNextSnapshotted(&objToIndex, &loc))) ||
           MONGO_FAIL_POINT(hangAfterStartingIndexBuild)) {
        try {
            if (_allowInterruption && !_opCtx->checkForInterruptNoAssert().isOK())
                return _opCtx->checkForInterruptNoAssert();

            if (!retries && PlanExecutor::ADVANCED != state) {
                continue;
            }

            // Make sure we are working with the latest version of the document.
            if (objToIndex.snapshotId() != _opCtx->recoveryUnit()->getSnapshotId() &&
                !_collection->findDoc(_opCtx, loc, &objToIndex)) {
                // Document was deleted so don't index it.
                retries = 0;
                continue;
            }

            // Done before insert so we can retry document if it WCEs.
            progress->setTotalWhileRunning(_collection->numRecords(_opCtx));

            failPointHangDuringBuild(&hangBeforeIndexBuildOf, "before", objToIndex.value());

            WriteUnitOfWork wunit(_opCtx);
            Status ret = insert(objToIndex.value(), loc);
            if (_buildInBackground)
                exec->saveState();
            if (!ret.isOK()) {
                // Fail the index build hard.
                return ret;
            }
            wunit.commit();
            if (_buildInBackground) {
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
            CurOp::get(_opCtx)->debug().additiveMetrics.incrementWriteConflicts(1);
            retries++;  // logAndBackoff expects this to be 1 on first call.
            WriteConflictException::logAndBackoff(
                retries, "index creation", _collection->ns().ns());

            // Can't use writeConflictRetry since we need to save/restore exec around call to
            // abandonSnapshot.
            exec->saveState();
            _opCtx->recoveryUnit()->abandonSnapshot();
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

    if (MONGO_FAIL_POINT(hangAfterStartingIndexBuildUnlocked)) {
        // Unlock before hanging so replication recognizes we've completed.
        Locker::LockSnapshot lockInfo;
        invariant(_opCtx->lockState()->saveLockStateAndUnlock(&lockInfo));
        while (MONGO_FAIL_POINT(hangAfterStartingIndexBuildUnlocked)) {
            log() << "Hanging index build with no locks due to "
                     "'hangAfterStartingIndexBuildUnlocked' failpoint";
            sleepmillis(1000);
        }

        if (_buildInBackground) {
            _opCtx->lockState()->restoreLockState(_opCtx, lockInfo);
            _opCtx->recoveryUnit()->abandonSnapshot();
            return Status(ErrorCodes::OperationFailed,
                          "background index build aborted due to failpoint");
        } else {
            invariant(
                !"the hangAfterStartingIndexBuildUnlocked failpoint can't be turned off for foreground index builds");
        }
    }

    progress->finished();

    Status ret = dumpInsertsFromBulk();
    if (!ret.isOK())
        return ret;

    log() << "build index collection scan done.  scanned " << n << " total records. " << t.seconds()
          << " secs";

    return Status::OK();
}

Status MultiIndexBlock::insert(const BSONObj& doc, const RecordId& loc) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot insert document into index builder: "
                              << _collection->ns().ns()
                              << "("
                              << *_collection->uuid()
                              << "): "
                              << redact(doc)};
    }

    for (size_t i = 0; i < _indexes.size(); i++) {
        if (_indexes[i].filterExpression && !_indexes[i].filterExpression->matchesBSON(doc)) {
            continue;
        }

        InsertResult result;
        Status idxStatus(ErrorCodes::InternalError, "");
        if (_indexes[i].bulk) {
            idxStatus = _indexes[i].bulk->insert(_opCtx, doc, loc, _indexes[i].options);
        } else {
            idxStatus = _indexes[i].real->insert(_opCtx, doc, loc, _indexes[i].options, &result);
        }

        if (!idxStatus.isOK())
            return idxStatus;
    }
    return Status::OK();
}

Status MultiIndexBlock::dumpInsertsFromBulk() {
    return dumpInsertsFromBulk(nullptr);
}

Status MultiIndexBlock::dumpInsertsFromBulk(std::set<RecordId>* dupRecords) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot complete insertion phase: "
                              << _collection->ns().ns()
                              << "("
                              << *_collection->uuid()
                              << ")"};
    }

    invariant(_opCtx->lockState()->isNoop() || !_opCtx->lockState()->inAWriteUnitOfWork());
    for (size_t i = 0; i < _indexes.size(); i++) {
        if (_indexes[i].bulk == NULL)
            continue;

        // If 'dupRecords' is provided, it will be used to store all records that would result in
        // duplicate key errors. Only pass 'dupKeysInserted', which stores inserted duplicate keys,
        // when 'dupRecords' is not used because these two vectors are mutually incompatible.
        std::vector<BSONObj> dupKeysInserted;

        IndexCatalogEntry* entry = _indexes[i].block->getEntry();
        LOG(1) << "\t dumping from external sorter into index: "
               << entry->descriptor()->indexName();
        Status status = _indexes[i].real->commitBulk(_opCtx,
                                                     _indexes[i].bulk.get(),
                                                     _allowInterruption,
                                                     _indexes[i].options.dupsAllowed,
                                                     dupRecords,
                                                     (dupRecords) ? nullptr : &dupKeysInserted);
        if (!status.isOK()) {
            return status;
        }

        auto interceptor = entry->indexBuildInterceptor();
        if (!interceptor || _ignoreUnique) {
            continue;
        }

        // Record duplicate key insertions for later verification.
        if (dupKeysInserted.size()) {
            status = interceptor->recordDuplicateKeys(_opCtx, dupKeysInserted);
            if (!status.isOK()) {
                return status;
            }
        }
    }

    _updateCurOpOpDescription(true);
    return Status::OK();
}

Status MultiIndexBlock::drainBackgroundWritesIfNeeded() {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot complete drain phase: "
                              << _collection->ns().ns()
                              << "("
                              << *_collection->uuid()
                              << ")"};
    }

    invariant(!_opCtx->lockState()->inAWriteUnitOfWork());

    // Drain side-writes table for each index. This only drains what is visible. Assuming intent
    // locks are held on the user collection, more writes can come in after this drain completes.
    // Callers are responsible for stopping writes by holding an S or X lock while draining before
    // completing the index build.
    for (size_t i = 0; i < _indexes.size(); i++) {
        auto interceptor = _indexes[i].block->getEntry()->indexBuildInterceptor();
        if (!interceptor)
            continue;

        LOG(1) << "draining background writes on collection " << _collection->ns()
               << " into index: " << _indexes[i].block->getEntry()->descriptor()->indexName();

        auto status = interceptor->drainWritesIntoIndex(_opCtx, _indexes[i].options);
        if (!status.isOK()) {
            return status;
        }
    }
    return Status::OK();
}


void MultiIndexBlock::abortWithoutCleanup() {
    _setStateToAbortedIfNotCommitted("aborted without cleanup"_sd);
    _indexes.clear();
    _needToCleanup = false;
}

Status MultiIndexBlock::commit() {
    return commit({});
}

Status MultiIndexBlock::commit(stdx::function<void(const BSONObj& spec)> onCreateFn) {
    if (State::kAborted == _getState()) {
        return {ErrorCodes::IndexBuildAborted,
                str::stream() << "Index build aborted: " << _abortReason
                              << ". Cannot commit index builder: "
                              << _collection->ns().ns()
                              << "("
                              << *_collection->uuid()
                              << ")"};
    }

    // Do not interfere with writing multikey information when committing index builds.
    auto restartTracker =
        MakeGuard([this] { MultikeyPathTracker::get(_opCtx).startTrackingMultikeyPathInfo(); });
    if (!MultikeyPathTracker::get(_opCtx).isTrackingMultikeyPathInfo()) {
        restartTracker.Dismiss();
    }
    MultikeyPathTracker::get(_opCtx).stopTrackingMultikeyPathInfo();

    for (size_t i = 0; i < _indexes.size(); i++) {
        if (onCreateFn) {
            onCreateFn(_indexes[i].block->getSpec());
        }

        // Do this before calling success(), which unsets the interceptor pointer on the index
        // catalog entry.
        auto interceptor = _indexes[i].block->getEntry()->indexBuildInterceptor();
        if (interceptor) {
            auto multikeyPaths = interceptor->getMultikeyPaths();
            if (multikeyPaths) {
                _indexes[i].block->getEntry()->setMultikey(_opCtx, multikeyPaths.get());
            }
        }

        _indexes[i].block->success();

        // The bulk builder will track multikey information itself. Non-bulk builders re-use the
        // code path that a typical insert/update uses. State is altered on the non-bulk build
        // path to accumulate the multikey information on the `MultikeyPathTracker`.
        if (_indexes[i].bulk) {
            const auto& bulkBuilder = _indexes[i].bulk;
            if (bulkBuilder->isMultikey()) {
                _indexes[i].block->getEntry()->setMultikey(_opCtx, bulkBuilder->getMultikeyPaths());
            }
        } else {
            auto multikeyPaths =
                boost::optional<MultikeyPaths>(MultikeyPathTracker::get(_opCtx).getMultikeyPathInfo(
                    _collection->ns(), _indexes[i].block->getIndexName()));
            if (multikeyPaths) {
                _indexes[i].block->getEntry()->setMultikey(_opCtx, *multikeyPaths);
            }
        }
    }

    // The state of this index build is set to Committed only when the WUOW commits.
    // It is possible for abort() to be called after the check at the beginning of this function and
    // before the WUOW is committed. If the WUOW commits, the final state of this index builder will
    // be Committed. Otherwise, the index builder state will remain as Aborted and further attempts
    // to commit this index build will fail.
    _opCtx->recoveryUnit()->onCommit(
        [this](boost::optional<Timestamp> commitTime) { _setState(State::kCommitted); });

    // On rollback sets MultiIndexBlock::_needToCleanup to true.
    _opCtx->recoveryUnit()->onRollback([this]() { _needToCleanup = true; });
    _needToCleanup = false;

    return Status::OK();
}

bool MultiIndexBlock::isCommitted() const {
    return State::kCommitted == _getState();
}

void MultiIndexBlock::abort(StringData reason) {
    _setStateToAbortedIfNotCommitted(reason);
}


bool MultiIndexBlock::getBuildInBackground() const {
    return _buildInBackground;
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

void MultiIndexBlock::_updateCurOpOpDescription(bool isBuildingPhaseComplete) const {
    BSONObjBuilder builder;

    // TODO(SERVER-37980): Replace with index build UUID.
    auto buildUUID = UUID::gen();
    buildUUID.appendToBuilder(&builder, kBuildUUIDFieldName);

    builder.append(kBuildingPhaseCompleteFieldName, isBuildingPhaseComplete);

    builder.appendBool(kRunTwoPhaseIndexBuildFieldName, false);

    auto replCoord = repl::ReplicationCoordinator::get(_opCtx);
    if (replCoord->isReplEnabled()) {
        // TODO(SERVER-37939): Update the membersBuilder array to state the actual commit ready
        // members.
        BSONArrayBuilder membersBuilder;
        auto config = replCoord->getConfig();
        for (auto it = config.membersBegin(); it != config.membersEnd(); ++it) {
            const auto& memberConfig = *it;
            if (memberConfig.isArbiter()) {
                continue;
            }
            membersBuilder.append(memberConfig.getHostAndPort().toString());
        }
        builder.append(kCommitReadyMembersFieldName, membersBuilder.arr());
    }

    stdx::unique_lock<Client> lk(*_opCtx->getClient());
    auto curOp = CurOp::get(_opCtx);
    builder.appendElementsUnique(curOp->opDescription());
    auto opDescObj = builder.obj();
    curOp->setOpDescription_inlock(opDescObj);
    curOp->ensureStarted();
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

}  // namespace mongo
