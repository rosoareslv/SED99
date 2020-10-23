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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplicationInitialSync

#include "mongo/platform/basic.h"

#include "initial_syncer.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "mongo/base/counter.h"
#include "mongo/base/status.h"
#include "mongo/bson/simple_bsonobj_comparator.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/client/fetcher.h"
#include "mongo/client/remote_command_retry_scheduler.h"
#include "mongo/db/commands/feature_compatibility_version_parser.h"
#include "mongo/db/commands/server_status_metric.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/all_database_cloner.h"
#include "mongo/db/repl/initial_sync_state.h"
#include "mongo/db/repl/member_state.h"
#include "mongo/db/repl/oplog_buffer.h"
#include "mongo/db/repl/oplog_fetcher.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/repl_server_parameters_gen.h"
#include "mongo/db/repl/replication_consistency_markers.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/sync_source_selector.h"
#include "mongo/db/repl/transaction_oplog_application.h"
#include "mongo/db/session_txn_record_gen.h"
#include "mongo/executor/task_executor.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/rpc/metadata/repl_set_metadata.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/destructor_guard.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/str.h"
#include "mongo/util/time_support.h"
#include "mongo/util/timer.h"

namespace mongo {
namespace repl {

// Failpoint for initial sync
MONGO_FAIL_POINT_DEFINE(failInitialSyncWithBadHost);

// Failpoint which fails initial sync and leaves an oplog entry in the buffer.
MONGO_FAIL_POINT_DEFINE(failInitSyncWithBufferedEntriesLeft);

// Failpoint which causes the initial sync function to hang after getting the oldest active
// transaction timestamp from the sync source.
MONGO_FAIL_POINT_DEFINE(initialSyncHangAfterGettingBeginFetchingTimestamp);

// Failpoint which causes the initial sync function to hang before copying databases.
MONGO_FAIL_POINT_DEFINE(initialSyncHangBeforeCopyingDatabases);

// Failpoint which causes the initial sync function to hang before finishing.
MONGO_FAIL_POINT_DEFINE(initialSyncHangBeforeFinish);

// Failpoint which causes the initial sync function to hang before creating the oplog.
MONGO_FAIL_POINT_DEFINE(initialSyncHangBeforeCreatingOplog);

// Failpoint which stops the applier.
MONGO_FAIL_POINT_DEFINE(rsSyncApplyStop);

// Failpoint which causes the initial sync function to hang after cloning all databases.
MONGO_FAIL_POINT_DEFINE(initialSyncHangAfterDataCloning);

// Failpoint which skips clearing _initialSyncState after a successful initial sync attempt.
MONGO_FAIL_POINT_DEFINE(skipClearInitialSyncState);

// Failpoint which causes the initial sync function to fail and hang before starting a new attempt.
MONGO_FAIL_POINT_DEFINE(failAndHangInitialSync);

// Failpoint which fails initial sync before it applies the next batch of oplog entries.
MONGO_FAIL_POINT_DEFINE(failInitialSyncBeforeApplyingBatch);

// Failpoint which fasserts if applying a batch fails.
MONGO_FAIL_POINT_DEFINE(initialSyncFassertIfApplyingBatchFails);

// Failpoints for synchronization, shared with cloners.
extern FailPoint initialSyncFuzzerSynchronizationPoint1;
extern FailPoint initialSyncFuzzerSynchronizationPoint2;

namespace {
using namespace executor;
using CallbackArgs = executor::TaskExecutor::CallbackArgs;
using Event = executor::TaskExecutor::EventHandle;
using Handle = executor::TaskExecutor::CallbackHandle;
using QueryResponseStatus = StatusWith<Fetcher::QueryResponse>;
using UniqueLock = stdx::unique_lock<Latch>;
using LockGuard = stdx::lock_guard<Latch>;

// Used to reset the oldest timestamp during initial sync to a non-null timestamp.
const Timestamp kTimestampOne(0, 1);

// The number of initial sync attempts that have failed since server startup. Each instance of
// InitialSyncer may run multiple attempts to fulfill an initial sync request that is triggered
// when InitialSyncer::startup() is called.
Counter64 initialSyncFailedAttempts;

// The number of initial sync requests that have been requested and failed. Each instance of
// InitialSyncer (upon successful startup()) corresponds to a single initial sync request.
// This value does not include the number of times where a InitialSyncer is created successfully
// but failed in startup().
Counter64 initialSyncFailures;

// The number of initial sync requests that have been requested and completed successfully. Each
// instance of InitialSyncer corresponds to a single initial sync request.
Counter64 initialSyncCompletes;

ServerStatusMetricField<Counter64> displaySSInitialSyncFailedAttempts(
    "repl.initialSync.failedAttempts", &initialSyncFailedAttempts);
ServerStatusMetricField<Counter64> displaySSInitialSyncFailures("repl.initialSync.failures",
                                                                &initialSyncFailures);
ServerStatusMetricField<Counter64> displaySSInitialSyncCompleted("repl.initialSync.completed",
                                                                 &initialSyncCompletes);

ServiceContext::UniqueOperationContext makeOpCtx() {
    return cc().makeOperationContext();
}

StatusWith<OpTimeAndWallTime> parseOpTimeAndWallTime(const QueryResponseStatus& fetchResult) {
    if (!fetchResult.isOK()) {
        return fetchResult.getStatus();
    }
    const auto docs = fetchResult.getValue().documents;
    const auto hasDoc = docs.begin() != docs.end();
    if (!hasDoc) {
        return StatusWith<OpTimeAndWallTime>{ErrorCodes::NoMatchingDocument,
                                             "no oplog entry found"};
    }

    return OpTimeAndWallTime::parseOpTimeAndWallTimeFromOplogEntry(docs.front());
}

void pauseAtInitialSyncFuzzerSyncronizationPoints(std::string msg) {
    // Set and unset by the InitialSyncTest fixture to cause initial sync to pause so that the
    // Initial Sync Fuzzer can run commands on the sync source.
    if (MONGO_unlikely(initialSyncFuzzerSynchronizationPoint1.shouldFail())) {
        log() << msg;
        log() << "initialSyncFuzzerSynchronizationPoint1 fail point enabled.";
        initialSyncFuzzerSynchronizationPoint1.pauseWhileSet();
    }

    if (MONGO_unlikely(initialSyncFuzzerSynchronizationPoint2.shouldFail())) {
        log() << "initialSyncFuzzerSynchronizationPoint2 fail point enabled.";
        initialSyncFuzzerSynchronizationPoint2.pauseWhileSet();
    }
}

}  // namespace

InitialSyncer::InitialSyncer(
    InitialSyncerOptions opts,
    std::unique_ptr<DataReplicatorExternalState> dataReplicatorExternalState,
    ThreadPool* writerPool,
    StorageInterface* storage,
    ReplicationProcess* replicationProcess,
    const OnCompletionFn& onCompletion)
    : _fetchCount(0),
      _opts(opts),
      _dataReplicatorExternalState(std::move(dataReplicatorExternalState)),
      _exec(_dataReplicatorExternalState->getTaskExecutor()),
      _clonerExec(_exec),
      _writerPool(writerPool),
      _storage(storage),
      _replicationProcess(replicationProcess),
      _onCompletion(onCompletion),
      _createClientFn(
          [] { return std::make_unique<DBClientConnection>(true /* autoReconnect */); }) {
    uassert(ErrorCodes::BadValue, "task executor cannot be null", _exec);
    uassert(ErrorCodes::BadValue, "invalid storage interface", _storage);
    uassert(ErrorCodes::BadValue, "invalid replication process", _replicationProcess);
    uassert(ErrorCodes::BadValue, "invalid getMyLastOptime function", _opts.getMyLastOptime);
    uassert(ErrorCodes::BadValue, "invalid setMyLastOptime function", _opts.setMyLastOptime);
    uassert(ErrorCodes::BadValue, "invalid resetOptimes function", _opts.resetOptimes);
    uassert(ErrorCodes::BadValue, "invalid sync source selector", _opts.syncSourceSelector);
    uassert(ErrorCodes::BadValue, "callback function cannot be null", _onCompletion);
}

InitialSyncer::~InitialSyncer() {
    DESTRUCTOR_GUARD({
        shutdown().transitional_ignore();
        join();
    });
}

bool InitialSyncer::isActive() const {
    stdx::lock_guard<Latch> lock(_mutex);
    return _isActive_inlock();
}

bool InitialSyncer::_isActive_inlock() const {
    return State::kRunning == _state || State::kShuttingDown == _state;
}

Status InitialSyncer::startup(OperationContext* opCtx,
                              std::uint32_t initialSyncMaxAttempts) noexcept {
    invariant(opCtx);
    invariant(initialSyncMaxAttempts >= 1U);

    stdx::lock_guard<Latch> lock(_mutex);
    switch (_state) {
        case State::kPreStart:
            _state = State::kRunning;
            break;
        case State::kRunning:
            return Status(ErrorCodes::IllegalOperation, "initial syncer already started");
        case State::kShuttingDown:
            return Status(ErrorCodes::ShutdownInProgress, "initial syncer shutting down");
        case State::kComplete:
            return Status(ErrorCodes::ShutdownInProgress, "initial syncer completed");
    }

    _setUp_inlock(opCtx, initialSyncMaxAttempts);

    // Start first initial sync attempt.
    std::uint32_t initialSyncAttempt = 0;
    auto status = _scheduleWorkAndSaveHandle_inlock(
        [=](const executor::TaskExecutor::CallbackArgs& args) {
            _startInitialSyncAttemptCallback(args, initialSyncAttempt, initialSyncMaxAttempts);
        },
        &_startInitialSyncAttemptHandle,
        str::stream() << "_startInitialSyncAttemptCallback-" << initialSyncAttempt);

    if (!status.isOK()) {
        _state = State::kComplete;
        return status;
    }

    return Status::OK();
}

Status InitialSyncer::shutdown() {
    stdx::lock_guard<Latch> lock(_mutex);
    switch (_state) {
        case State::kPreStart:
            // Transition directly from PreStart to Complete if not started yet.
            _state = State::kComplete;
            return Status::OK();
        case State::kRunning:
            _state = State::kShuttingDown;
            break;
        case State::kShuttingDown:
        case State::kComplete:
            // Nothing to do if we are already in ShuttingDown or Complete state.
            return Status::OK();
    }

    _cancelRemainingWork_inlock();

    return Status::OK();
}

void InitialSyncer::_cancelRemainingWork_inlock() {
    _cancelHandle_inlock(_startInitialSyncAttemptHandle);
    _cancelHandle_inlock(_chooseSyncSourceHandle);
    _cancelHandle_inlock(_getBaseRollbackIdHandle);
    _cancelHandle_inlock(_getLastRollbackIdHandle);
    _cancelHandle_inlock(_getNextApplierBatchHandle);

    _shutdownComponent_inlock(_oplogFetcher);
    if (_sharedData) {
        // We actually hold the required lock, but the lock object itself is not passed through.
        _clearNetworkError(WithLock::withoutLock());
        stdx::lock_guard<InitialSyncSharedData> lock(*_sharedData);
        _sharedData->setInitialSyncStatusIfOK(
            lock, Status{ErrorCodes::CallbackCanceled, "Initial sync attempt canceled"});
    }
    if (_client) {
        _client->shutdownAndDisallowReconnect();
    }
    _shutdownComponent_inlock(_applier);
    _shutdownComponent_inlock(_fCVFetcher);
    _shutdownComponent_inlock(_lastOplogEntryFetcher);
    _shutdownComponent_inlock(_beginFetchingOpTimeFetcher);
}

void InitialSyncer::join() {
    stdx::unique_lock<Latch> lk(_mutex);
    _stateCondition.wait(lk, [this]() { return !_isActive_inlock(); });
}

InitialSyncer::State InitialSyncer::getState_forTest() const {
    stdx::lock_guard<Latch> lk(_mutex);
    return _state;
}

Date_t InitialSyncer::getWallClockTime_forTest() const {
    stdx::lock_guard<Latch> lk(_mutex);
    return _lastApplied.wallTime;
}

void InitialSyncer::setAllowedOutageDuration_forTest(Milliseconds allowedOutageDuration) {
    stdx::lock_guard<Latch> lk(_mutex);
    _allowedOutageDuration = allowedOutageDuration;
    if (_sharedData) {
        stdx::lock_guard<InitialSyncSharedData> lk(*_sharedData);
        _sharedData->setAllowedOutageDuration_forTest(lk, allowedOutageDuration);
    }
}

bool InitialSyncer::_isShuttingDown() const {
    stdx::lock_guard<Latch> lock(_mutex);
    return _isShuttingDown_inlock();
}

bool InitialSyncer::_isShuttingDown_inlock() const {
    return State::kShuttingDown == _state;
}

std::string InitialSyncer::getDiagnosticString() const {
    LockGuard lk(_mutex);
    str::stream out;
    out << "InitialSyncer -"
        << " opts: " << _opts.toString() << " oplogFetcher: " << _oplogFetcher->toString()
        << " opsBuffered: " << _oplogBuffer->getSize() << " active: " << _isActive_inlock()
        << " shutting down: " << _isShuttingDown_inlock();
    if (_initialSyncState) {
        out << " opsAppied: " << _initialSyncState->appliedOps;
    }

    return out;
}

BSONObj InitialSyncer::getInitialSyncProgress() const {
    LockGuard lk(_mutex);

    // We return an empty BSON object after an initial sync attempt has been successfully
    // completed. When an initial sync attempt completes successfully, initialSyncCompletes is
    // incremented and then _initialSyncState is cleared. We check that _initialSyncState has been
    // cleared because an initial sync attempt can fail even after initialSyncCompletes is
    // incremented, and we also check that initialSyncCompletes is positive because an initial sync
    // attempt can also fail before _initialSyncState is initialized.
    if (!_initialSyncState && initialSyncCompletes.get() > 0) {
        return BSONObj();
    }
    return _getInitialSyncProgress_inlock();
}

void InitialSyncer::_appendInitialSyncProgressMinimal_inlock(BSONObjBuilder* bob) const {
    _stats.append(bob);
    if (!_initialSyncState) {
        return;
    }
    bob->appendNumber("appliedOps", _initialSyncState->appliedOps);
    if (!_initialSyncState->beginApplyingTimestamp.isNull()) {
        bob->append("initialSyncOplogStart", _initialSyncState->beginApplyingTimestamp);
    }
    // Only include the beginFetchingTimestamp if it's different from the beginApplyingTimestamp.
    if (!_initialSyncState->beginFetchingTimestamp.isNull() &&
        _initialSyncState->beginFetchingTimestamp != _initialSyncState->beginApplyingTimestamp) {
        bob->append("initialSyncOplogFetchingStart", _initialSyncState->beginFetchingTimestamp);
    }
    if (!_initialSyncState->stopTimestamp.isNull()) {
        bob->append("initialSyncOplogEnd", _initialSyncState->stopTimestamp);
    }
}

BSONObj InitialSyncer::_getInitialSyncProgress_inlock() const {
    try {
        BSONObjBuilder bob;
        _appendInitialSyncProgressMinimal_inlock(&bob);
        if (_initialSyncState) {
            if (_initialSyncState->allDatabaseCloner) {
                BSONObjBuilder dbsBuilder(bob.subobjStart("databases"));
                _initialSyncState->allDatabaseCloner->getStats().append(&dbsBuilder);
                dbsBuilder.doneFast();
            }
        }
        return bob.obj();
    } catch (const DBException& e) {
        log() << "Error creating initial sync progress object: " << e.toString();
    }
    BSONObjBuilder bob;
    _appendInitialSyncProgressMinimal_inlock(&bob);
    return bob.obj();
}

void InitialSyncer::setCreateClientFn_forTest(const CreateClientFn& createClientFn) {
    LockGuard lk(_mutex);
    _createClientFn = createClientFn;
}

void InitialSyncer::setClonerExecutor_forTest(executor::TaskExecutor* clonerExec) {
    _clonerExec = clonerExec;
}

void InitialSyncer::waitForCloner_forTest() {
    _initialSyncState->allDatabaseClonerFuture.wait();
}

void InitialSyncer::_setUp_inlock(OperationContext* opCtx, std::uint32_t initialSyncMaxAttempts) {
    // 'opCtx' is passed through from startup().
    _replicationProcess->getConsistencyMarkers()->setInitialSyncFlag(opCtx);

    auto serviceCtx = opCtx->getServiceContext();
    _storage->setInitialDataTimestamp(serviceCtx, Timestamp::kAllowUnstableCheckpointsSentinel);
    _storage->setStableTimestamp(serviceCtx, Timestamp::min());

    LOG(1) << "Creating oplogBuffer.";
    _oplogBuffer = _dataReplicatorExternalState->makeInitialSyncOplogBuffer(opCtx);
    _oplogBuffer->startup(opCtx);

    _stats.initialSyncStart = _exec->now();
    _stats.maxFailedInitialSyncAttempts = initialSyncMaxAttempts;
    _stats.failedInitialSyncAttempts = 0;

    _allowedOutageDuration = Seconds(initialSyncTransientErrorRetryPeriodSeconds.load());
}

void InitialSyncer::_tearDown_inlock(OperationContext* opCtx,
                                     const StatusWith<OpTimeAndWallTime>& lastApplied) {
    _stats.initialSyncEnd = _exec->now();

    // This might not be necessary if we failed initial sync.
    invariant(_oplogBuffer);
    _oplogBuffer->shutdown(opCtx);

    if (!lastApplied.isOK()) {
        return;
    }
    const auto lastAppliedOpTime = lastApplied.getValue().opTime;
    auto initialDataTimestamp = lastAppliedOpTime.getTimestamp();

    // A node coming out of initial sync must guarantee at least one oplog document is visible
    // such that others can sync from this node. Oplog visibility is only advanced when applying
    // oplog entries during initial sync. Correct the visibility to match the initial sync time
    // before transitioning to steady state replication.
    const bool orderedCommit = true;
    _storage->oplogDiskLocRegister(opCtx, initialDataTimestamp, orderedCommit);

    reconstructPreparedTransactions(opCtx, repl::OplogApplication::Mode::kInitialSync);

    _replicationProcess->getConsistencyMarkers()->clearInitialSyncFlag(opCtx);

    // All updates that represent initial sync must be completed before setting the initial data
    // timestamp.
    _storage->setInitialDataTimestamp(opCtx->getServiceContext(), initialDataTimestamp);

    auto currentLastAppliedOpTime = _opts.getMyLastOptime();
    if (currentLastAppliedOpTime.isNull()) {
        _opts.setMyLastOptime(lastApplied.getValue(),
                              ReplicationCoordinator::DataConsistency::Consistent);
    } else {
        invariant(currentLastAppliedOpTime == lastAppliedOpTime);
    }

    log() << "initial sync done; took "
          << duration_cast<Seconds>(_stats.initialSyncEnd - _stats.initialSyncStart) << ".";
    initialSyncCompletes.increment();
}

void InitialSyncer::_startInitialSyncAttemptCallback(
    const executor::TaskExecutor::CallbackArgs& callbackArgs,
    std::uint32_t initialSyncAttempt,
    std::uint32_t initialSyncMaxAttempts) {
    auto status = _checkForShutdownAndConvertStatus_inlock(
        callbackArgs,
        str::stream() << "error while starting initial sync attempt " << (initialSyncAttempt + 1)
                      << " of " << initialSyncMaxAttempts);
    if (!status.isOK()) {
        _finishInitialSyncAttempt(status);
        return;
    }

    log() << "Starting initial sync (attempt " << (initialSyncAttempt + 1) << " of "
          << initialSyncMaxAttempts << ")";

    // This completion guard invokes _finishInitialSyncAttempt on destruction.
    auto cancelRemainingWorkInLock = [this]() { _cancelRemainingWork_inlock(); };
    auto finishInitialSyncAttemptFn = [this](const StatusWith<OpTimeAndWallTime>& lastApplied) {
        _finishInitialSyncAttempt(lastApplied);
    };
    auto onCompletionGuard =
        std::make_shared<OnCompletionGuard>(cancelRemainingWorkInLock, finishInitialSyncAttemptFn);

    // Lock guard must be declared after completion guard because completion guard destructor
    // has to run outside lock.
    stdx::lock_guard<Latch> lock(_mutex);

    _oplogApplier = {};

    LOG(2) << "Resetting sync source so a new one can be chosen for this initial sync attempt.";
    _syncSource = HostAndPort();

    LOG(2) << "Resetting all optimes before starting this initial sync attempt.";
    _opts.resetOptimes();
    _lastApplied = {OpTime(), Date_t()};
    _lastFetched = {};

    LOG(2) << "Resetting the oldest timestamp before starting this initial sync attempt.";
    auto storageEngine = getGlobalServiceContext()->getStorageEngine();
    if (storageEngine) {
        // Set the oldestTimestamp to one because WiredTiger does not allow us to set it to zero
        // since that would also set the all_durable point to zero. We specifically don't set
        // the stable timestamp here because that will trigger taking a first stable checkpoint even
        // though the initialDataTimestamp is still set to kAllowUnstableCheckpointsSentinel.
        storageEngine->setOldestTimestamp(kTimestampOne);
    }

    LOG(2) << "Resetting feature compatibility version to last-stable. If the sync source is in "
              "latest feature compatibility version, we will find out when we clone the "
              "server configuration collection (admin.system.version).";
    serverGlobalParams.featureCompatibility.reset();

    // Clear the oplog buffer.
    _oplogBuffer->clear(makeOpCtx().get());

    // Get sync source.
    std::uint32_t chooseSyncSourceAttempt = 0;
    std::uint32_t chooseSyncSourceMaxAttempts =
        static_cast<std::uint32_t>(numInitialSyncConnectAttempts.load());

    // _scheduleWorkAndSaveHandle_inlock() is shutdown-aware.
    status = _scheduleWorkAndSaveHandle_inlock(
        [=](const executor::TaskExecutor::CallbackArgs& args) {
            _chooseSyncSourceCallback(
                args, chooseSyncSourceAttempt, chooseSyncSourceMaxAttempts, onCompletionGuard);
        },
        &_chooseSyncSourceHandle,
        str::stream() << "_chooseSyncSourceCallback-" << chooseSyncSourceAttempt);
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_chooseSyncSourceCallback(
    const executor::TaskExecutor::CallbackArgs& callbackArgs,
    std::uint32_t chooseSyncSourceAttempt,
    std::uint32_t chooseSyncSourceMaxAttempts,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::unique_lock<Latch> lock(_mutex);
    // Cancellation should be treated the same as other errors. In this case, the most likely cause
    // of a failed _chooseSyncSourceCallback() task is a cancellation triggered by
    // InitialSyncer::shutdown() or the task executor shutting down.
    auto status =
        _checkForShutdownAndConvertStatus_inlock(callbackArgs, "error while choosing sync source");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    if (MONGO_unlikely(failInitialSyncWithBadHost.shouldFail())) {
        status = Status(ErrorCodes::InvalidSyncSource,
                        "initial sync failed - failInitialSyncWithBadHost failpoint is set.");
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    auto syncSource = _chooseSyncSource_inlock();
    if (!syncSource.isOK()) {
        if (chooseSyncSourceAttempt + 1 >= chooseSyncSourceMaxAttempts) {
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(
                lock,
                Status(ErrorCodes::InitialSyncOplogSourceMissing,
                       "No valid sync source found in current replica set to do an initial sync."));
            return;
        }

        auto when = _exec->now() + _opts.syncSourceRetryWait;
        LOG(1) << "Error getting sync source: '" << syncSource.getStatus() << "', trying again in "
               << _opts.syncSourceRetryWait << " at " << when.toString() << ". Attempt "
               << (chooseSyncSourceAttempt + 1) << " of " << numInitialSyncConnectAttempts.load();
        auto status = _scheduleWorkAtAndSaveHandle_inlock(
            when,
            [=](const executor::TaskExecutor::CallbackArgs& args) {
                _chooseSyncSourceCallback(args,
                                          chooseSyncSourceAttempt + 1,
                                          chooseSyncSourceMaxAttempts,
                                          onCompletionGuard);
            },
            &_chooseSyncSourceHandle,
            str::stream() << "_chooseSyncSourceCallback-" << (chooseSyncSourceAttempt + 1));
        if (!status.isOK()) {
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
            return;
        }
        return;
    }

    if (MONGO_unlikely(initialSyncHangBeforeCreatingOplog.shouldFail())) {
        // This log output is used in js tests so please leave it.
        log() << "initial sync - initialSyncHangBeforeCreatingOplog fail point "
                 "enabled. Blocking until fail point is disabled.";
        lock.unlock();
        while (MONGO_unlikely(initialSyncHangBeforeCreatingOplog.shouldFail()) &&
               !_isShuttingDown()) {
            mongo::sleepsecs(1);
        }
        lock.lock();
    }

    // There is no need to schedule separate task to create oplog collection since we are already in
    // a callback and we are certain there's no existing operation context (required for creating
    // collections and dropping user databases) attached to the current thread.
    status = _truncateOplogAndDropReplicatedDatabases();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    _syncSource = syncSource.getValue();

    // Schedule rollback ID checker.
    _rollbackChecker = std::make_unique<RollbackChecker>(_exec, _syncSource);
    auto scheduleResult = _rollbackChecker->reset([=](const RollbackChecker::Result& result) {
        return _rollbackCheckerResetCallback(result, onCompletionGuard);
    });
    status = scheduleResult.getStatus();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
    _getBaseRollbackIdHandle = scheduleResult.getValue();
}

Status InitialSyncer::_truncateOplogAndDropReplicatedDatabases() {
    // truncate oplog; drop user databases.
    LOG(1) << "About to truncate the oplog, if it exists, ns:" << _opts.localOplogNS
           << ", and drop all user databases (so that we can clone them).";

    auto opCtx = makeOpCtx();

    // We are not replicating nor validating these writes.
    UnreplicatedWritesBlock unreplicatedWritesBlock(opCtx.get());

    // 1.) Truncate the oplog.
    LOG(2) << "Truncating the existing oplog: " << _opts.localOplogNS;
    Timer timer;
    auto status = _storage->truncateCollection(opCtx.get(), _opts.localOplogNS);
    log() << "Initial syncer oplog truncation finished in: " << timer.millis() << "ms";
    if (!status.isOK()) {
        // 1a.) Create the oplog.
        LOG(2) << "Creating the oplog: " << _opts.localOplogNS;
        status = _storage->createOplog(opCtx.get(), _opts.localOplogNS);
        if (!status.isOK()) {
            return status;
        }
    }

    // 2.) Drop user databases.
    LOG(2) << "Dropping user databases";
    return _storage->dropReplicatedDatabases(opCtx.get());
}

void InitialSyncer::_rollbackCheckerResetCallback(
    const RollbackChecker::Result& result, std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::lock_guard<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(result.getStatus(),
                                                           "error while getting base rollback ID");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    status = _scheduleLastOplogEntryFetcher_inlock(
        [=](const StatusWith<mongo::Fetcher::QueryResponse>& response,
            mongo::Fetcher::NextAction*,
            mongo::BSONObjBuilder*) mutable {
            _lastOplogEntryFetcherCallbackForDefaultBeginFetchingOpTime(response,
                                                                        onCompletionGuard);
        });
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_lastOplogEntryFetcherCallbackForDefaultBeginFetchingOpTime(
    const StatusWith<Fetcher::QueryResponse>& result,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard) {

    stdx::unique_lock<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(
        result.getStatus(), "error while getting last oplog entry for begin timestamp");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    const auto opTimeResult = parseOpTimeAndWallTime(result);
    status = opTimeResult.getStatus();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    // This is the top of the oplog before we query for the oldest active transaction timestamp. If
    // that query returns that there are no active transactions, we will use this as the
    // beginFetchingTimestamp.
    const auto& defaultBeginFetchingOpTime = opTimeResult.getValue().opTime;

    std::string logMsg = str::stream() << "Initial Syncer got the defaultBeginFetchingTimestamp: "
                                       << defaultBeginFetchingOpTime.toString();
    pauseAtInitialSyncFuzzerSyncronizationPoints(logMsg);

    status = _scheduleGetBeginFetchingOpTime_inlock(onCompletionGuard, defaultBeginFetchingOpTime);
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

Status InitialSyncer::_scheduleGetBeginFetchingOpTime_inlock(
    std::shared_ptr<OnCompletionGuard> onCompletionGuard,
    const OpTime& defaultBeginFetchingOpTime) {

    const auto preparedState = DurableTxnState_serializer(DurableTxnStateEnum::kPrepared);
    const auto inProgressState = DurableTxnState_serializer(DurableTxnStateEnum::kInProgress);

    // Obtain the oldest active transaction timestamp from the remote by querying their
    // transactions table. To prevent oplog holes from causing this query to return an inaccurate
    // timestamp, we specify an afterClusterTime of Timestamp(0, 1) so that we wait for all previous
    // writes to be visible.
    BSONObjBuilder cmd;
    cmd.append("find", NamespaceString::kSessionTransactionsTableNamespace.coll().toString());
    cmd.append("filter",
               BSON("state" << BSON("$in" << BSON_ARRAY(preparedState << inProgressState))));
    cmd.append("sort", BSON(SessionTxnRecord::kStartOpTimeFieldName << 1));
    cmd.append("readConcern",
               BSON("level"
                    << "local"
                    << "afterClusterTime" << Timestamp(0, 1)));
    cmd.append("limit", 1);

    _beginFetchingOpTimeFetcher = std::make_unique<Fetcher>(
        _exec,
        _syncSource,
        NamespaceString::kSessionTransactionsTableNamespace.db().toString(),
        cmd.obj(),
        [=](const StatusWith<mongo::Fetcher::QueryResponse>& response,
            mongo::Fetcher::NextAction*,
            mongo::BSONObjBuilder*) mutable {
            _getBeginFetchingOpTimeCallback(
                response, onCompletionGuard, defaultBeginFetchingOpTime);
        },
        ReadPreferenceSetting::secondaryPreferredMetadata(),
        RemoteCommandRequest::kNoTimeout /* find network timeout */,
        RemoteCommandRequest::kNoTimeout /* getMore network timeout */,
        RemoteCommandRetryScheduler::makeRetryPolicy<ErrorCategory::RetriableError>(
            numInitialSyncOplogFindAttempts.load(), executor::RemoteCommandRequest::kNoTimeout));
    Status scheduleStatus = _beginFetchingOpTimeFetcher->schedule();
    if (!scheduleStatus.isOK()) {
        _beginFetchingOpTimeFetcher.reset();
    }
    return scheduleStatus;
}

void InitialSyncer::_getBeginFetchingOpTimeCallback(
    const StatusWith<Fetcher::QueryResponse>& result,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard,
    const OpTime& defaultBeginFetchingOpTime) {
    stdx::unique_lock<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(
        result.getStatus(),
        "error while getting oldest active transaction timestamp for begin fetching timestamp");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    const auto docs = result.getValue().documents;
    if (docs.size() > 1) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::TooManyMatchingDocuments,
                   str::stream() << "Expected to receive one document for the oldest active "
                                    "transaction entry, but received: "
                                 << docs.size() << ". First: " << redact(docs.front())
                                 << ". Last: " << redact(docs.back())));
        return;
    }

    // Set beginFetchingOpTime if the oldest active transaction timestamp actually exists. Otherwise
    // use the sync source's top of the oplog from before querying for the oldest active transaction
    // timestamp. This will mean that even if a transaction is started on the sync source after
    // querying for the oldest active transaction timestamp, the node will still fetch its oplog
    // entries.
    OpTime beginFetchingOpTime = defaultBeginFetchingOpTime;
    if (docs.size() != 0) {
        auto entry = SessionTxnRecord::parse(
            IDLParserErrorContext("oldest active transaction optime for initial sync"),
            docs.front());
        auto optime = entry.getStartOpTime();
        if (optime) {
            beginFetchingOpTime = optime.get();
        }
    }

    std::string logMsg = str::stream()
        << "Initial Syncer got the beginFetchingTimestamp: " << beginFetchingOpTime.toString();
    pauseAtInitialSyncFuzzerSyncronizationPoints(logMsg);

    if (MONGO_unlikely(initialSyncHangAfterGettingBeginFetchingTimestamp.shouldFail())) {
        log() << "initialSyncHangAfterGettingBeginFetchingTimestamp fail point enabled.";
        initialSyncHangAfterGettingBeginFetchingTimestamp.pauseWhileSet();
    }

    status = _scheduleLastOplogEntryFetcher_inlock(
        [=](const StatusWith<mongo::Fetcher::QueryResponse>& response,
            mongo::Fetcher::NextAction*,
            mongo::BSONObjBuilder*) mutable {
            _lastOplogEntryFetcherCallbackForBeginApplyingTimestamp(
                response, onCompletionGuard, beginFetchingOpTime);
        });
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_lastOplogEntryFetcherCallbackForBeginApplyingTimestamp(
    const StatusWith<Fetcher::QueryResponse>& result,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard,
    OpTime& beginFetchingOpTime) {
    stdx::unique_lock<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(
        result.getStatus(), "error while getting last oplog entry for begin timestamp");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    const auto opTimeResult = parseOpTimeAndWallTime(result);
    status = opTimeResult.getStatus();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    const auto& lastOpTime = opTimeResult.getValue().opTime;

    std::string logMsg = str::stream()
        << "Initial Syncer got the beginApplyingTimestamp: " << lastOpTime.toString();
    pauseAtInitialSyncFuzzerSyncronizationPoints(logMsg);

    BSONObjBuilder queryBob;
    queryBob.append("find", NamespaceString::kServerConfigurationNamespace.coll());
    auto filterBob = BSONObjBuilder(queryBob.subobjStart("filter"));
    filterBob.append("_id", FeatureCompatibilityVersionParser::kParameterName);
    filterBob.done();
    // As part of reading the FCV, we ensure the source node's all_durable timestamp has advanced
    // to at least the timestamp of the last optime that we found in the lastOplogEntryFetcher.
    // When document locking is used, there could be oplog "holes" which would result in
    // inconsistent initial sync data if we didn't do this.
    auto readConcernBob = BSONObjBuilder(queryBob.subobjStart("readConcern"));
    readConcernBob.append("afterClusterTime", lastOpTime.getTimestamp());
    readConcernBob.done();

    _fCVFetcher = std::make_unique<Fetcher>(
        _exec,
        _syncSource,
        NamespaceString::kServerConfigurationNamespace.db().toString(),
        queryBob.obj(),
        [=](const StatusWith<mongo::Fetcher::QueryResponse>& response,
            mongo::Fetcher::NextAction*,
            mongo::BSONObjBuilder*) mutable {
            _fcvFetcherCallback(response, onCompletionGuard, lastOpTime, beginFetchingOpTime);
        },
        ReadPreferenceSetting::secondaryPreferredMetadata(),
        RemoteCommandRequest::kNoTimeout /* find network timeout */,
        RemoteCommandRequest::kNoTimeout /* getMore network timeout */,
        RemoteCommandRetryScheduler::makeRetryPolicy<ErrorCategory::RetriableError>(
            numInitialSyncOplogFindAttempts.load(), executor::RemoteCommandRequest::kNoTimeout));
    Status scheduleStatus = _fCVFetcher->schedule();
    if (!scheduleStatus.isOK()) {
        _fCVFetcher.reset();
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, scheduleStatus);
        return;
    }
}

void InitialSyncer::_fcvFetcherCallback(const StatusWith<Fetcher::QueryResponse>& result,
                                        std::shared_ptr<OnCompletionGuard> onCompletionGuard,
                                        const OpTime& lastOpTime,
                                        OpTime& beginFetchingOpTime) {
    stdx::unique_lock<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(
        result.getStatus(), "error while getting the remote feature compatibility version");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    const auto docs = result.getValue().documents;
    if (docs.size() > 1) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::TooManyMatchingDocuments,
                   str::stream() << "Expected to receive one feature compatibility version "
                                    "document, but received: "
                                 << docs.size() << ". First: " << redact(docs.front())
                                 << ". Last: " << redact(docs.back())));
        return;
    }
    const auto hasDoc = docs.begin() != docs.end();
    if (!hasDoc) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::IncompatibleServerVersion,
                   "Sync source had no feature compatibility version document"));
        return;
    }

    auto fCVParseSW = FeatureCompatibilityVersionParser::parse(docs.front());
    if (!fCVParseSW.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, fCVParseSW.getStatus());
        return;
    }

    auto version = fCVParseSW.getValue();

    // Changing the featureCompatibilityVersion during initial sync is unsafe.
    if (version > ServerGlobalParams::FeatureCompatibility::Version::kFullyDowngradedTo42 &&
        version < ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo44) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::IncompatibleServerVersion,
                   str::stream() << "Sync source had unsafe feature compatibility version: "
                                 << FeatureCompatibilityVersionParser::toString(version)));
        return;
    }

    // This is where the flow of control starts to split into two parallel tracks:
    // - oplog fetcher
    // - data cloning and applier
    _sharedData =
        std::make_unique<InitialSyncSharedData>(version,
                                                _rollbackChecker->getBaseRBID(),
                                                _allowedOutageDuration,
                                                getGlobalServiceContext()->getFastClockSource());
    _client = _createClientFn();
    _initialSyncState = std::make_unique<InitialSyncState>(std::make_unique<AllDatabaseCloner>(
        _sharedData.get(), _syncSource, _client.get(), _storage, _writerPool));

    // Create oplog applier.
    auto consistencyMarkers = _replicationProcess->getConsistencyMarkers();
    OplogApplier::Options options(OplogApplication::Mode::kInitialSync);
    options.beginApplyingOpTime = lastOpTime;
    _oplogApplier = _dataReplicatorExternalState->makeOplogApplier(_oplogBuffer.get(),
                                                                   &noopOplogApplierObserver,
                                                                   consistencyMarkers,
                                                                   _storage,
                                                                   options,
                                                                   _writerPool);

    _initialSyncState->beginApplyingTimestamp = lastOpTime.getTimestamp();
    _initialSyncState->beginFetchingTimestamp = beginFetchingOpTime.getTimestamp();

    invariant(_initialSyncState->beginApplyingTimestamp >=
                  _initialSyncState->beginFetchingTimestamp,
              str::stream() << "beginApplyingTimestamp was less than beginFetchingTimestamp. "
                               "beginApplyingTimestamp: "
                            << _initialSyncState->beginApplyingTimestamp.toBSON()
                            << " beginFetchingTimestamp: "
                            << _initialSyncState->beginFetchingTimestamp.toBSON());

    invariant(!result.getValue().documents.empty());
    LOG(2) << "Setting begin applying timestamp to " << _initialSyncState->beginApplyingTimestamp
           << " using last oplog entry: " << redact(result.getValue().documents.front())
           << ", ns: " << _opts.localOplogNS << " and the begin fetching timestamp to "
           << _initialSyncState->beginFetchingTimestamp;

    const auto configResult = _dataReplicatorExternalState->getCurrentConfig();
    status = configResult.getStatus();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        _initialSyncState.reset();
        return;
    }

    const auto& config = configResult.getValue();
    _oplogFetcher = std::make_unique<OplogFetcher>(
        _exec,
        beginFetchingOpTime,
        _syncSource,
        _opts.remoteOplogNS,
        config,
        _opts.oplogFetcherMaxFetcherRestarts,
        _rollbackChecker->getBaseRBID(),
        false /* requireFresherSyncSource */,
        _dataReplicatorExternalState.get(),
        [=](Fetcher::Documents::const_iterator first,
            Fetcher::Documents::const_iterator last,
            const OplogFetcher::DocumentsInfo& info) {
            return _enqueueDocuments(first, last, info);
        },
        [=](const Status& s) { _oplogFetcherCallback(s, onCompletionGuard); },
        initialSyncOplogFetcherBatchSize,
        OplogFetcher::StartingPoint::kEnqueueFirstDoc);

    LOG(2) << "Starting OplogFetcher: " << _oplogFetcher->toString();

    // _startupComponent_inlock is shutdown-aware.
    status = _startupComponent_inlock(_oplogFetcher);
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        _initialSyncState->allDatabaseCloner.reset();
        return;
    }

    if (MONGO_unlikely(initialSyncHangBeforeCopyingDatabases.shouldFail())) {
        lock.unlock();
        // This could have been done with a scheduleWorkAt but this is used only by JS tests where
        // we run with multiple threads so it's fine to spin on this thread.
        // This log output is used in js tests so please leave it.
        log() << "initial sync - initialSyncHangBeforeCopyingDatabases fail point "
                 "enabled. Blocking until fail point is disabled.";
        while (MONGO_unlikely(initialSyncHangBeforeCopyingDatabases.shouldFail()) &&
               !_isShuttingDown()) {
            mongo::sleepsecs(1);
        }
        lock.lock();
    }

    LOG(2) << "Starting AllDatabaseCloner: " << _initialSyncState->allDatabaseCloner->toString();

    _initialSyncState->allDatabaseClonerFuture =
        _initialSyncState->allDatabaseCloner->runOnExecutor(_clonerExec)
            .onCompletion([this, onCompletionGuard](Status status) mutable {
                // The completion guard must run on the main executor.  This only makes a difference
                // for unit tests, but we always schedule it that way to avoid special casing test
                // code.
                stdx::unique_lock<Latch> lock(_mutex);
                auto exec_status = _exec->scheduleWork(
                    [this, status, onCompletionGuard](executor::TaskExecutor::CallbackArgs args) {
                        _allDatabaseClonerCallback(status, onCompletionGuard);
                    });
                if (!exec_status.isOK()) {
                    onCompletionGuard->setResultAndCancelRemainingWork_inlock(
                        lock, exec_status.getStatus());
                    // In the shutdown case, it is possible the completion guard will be run
                    // from this thread (since the lambda holding another copy didn't schedule).
                    // If it does, we will self-deadlock if we're holding the lock, so release it.
                    lock.unlock();
                }
                // In unit tests, this reset ensures the completion guard does not run during the
                // destruction of the lambda (which occurs on the wrong executor), except in the
                // shutdown case.
                onCompletionGuard.reset();
            });

    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_oplogFetcherCallback(const Status& oplogFetcherFinishStatus,
                                          std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::lock_guard<Latch> lock(_mutex);
    log() << "Finished fetching oplog during initial sync: " << redact(oplogFetcherFinishStatus)
          << ". Last fetched optime: " << _lastFetched.toString();

    auto status = _checkForShutdownAndConvertStatus_inlock(
        oplogFetcherFinishStatus, "error fetching oplog during initial sync");

    // When the OplogFetcher completes early (instead of being canceled at shutdown), we log and let
    // our reference to 'onCompletionGuard' go out of scope. Since we know the
    // DatabasesCloner/MultiApplier will still have a reference to it, the actual function within
    // the guard won't be fired yet.
    // It is up to the DatabasesCloner and MultiApplier to determine if they can proceed without any
    // additional data going into the oplog buffer.
    // It is not common for the OplogFetcher to return with an OK status. The only time it returns
    // an OK status is when the 'stopReplProducer' fail point is enabled, which causes the
    // OplogFetcher to ignore the current sync source response and return early.
    if (status.isOK()) {
        log() << "Finished fetching oplog fetching early. Last fetched optime: "
              << _lastFetched.toString();
        return;
    }

    // During normal operation, this call to onCompletion->setResultAndCancelRemainingWork_inlock
    // is a no-op because the other thread running the DatabasesCloner or MultiApplier will already
    // have called it with the success/failed status.
    // The OplogFetcher does not finish on its own because of the oplog tailing query it runs on the
    // sync source. The most common OplogFetcher completion status is CallbackCanceled due to either
    // a shutdown request or completion of the data cloning and oplog application phases.
    onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
}

void InitialSyncer::_allDatabaseClonerCallback(
    const Status& databaseClonerFinishStatus,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    log() << "Finished cloning data: " << redact(databaseClonerFinishStatus)
          << ". Beginning oplog replay.";
    _client->shutdownAndDisallowReconnect();

    if (MONGO_unlikely(initialSyncHangAfterDataCloning.shouldFail())) {
        // This could have been done with a scheduleWorkAt but this is used only by JS tests where
        // we run with multiple threads so it's fine to spin on this thread.
        // This log output is used in js tests so please leave it.
        log() << "initial sync - initialSyncHangAfterDataCloning fail point "
                 "enabled. Blocking until fail point is disabled.";
        while (MONGO_unlikely(initialSyncHangAfterDataCloning.shouldFail()) && !_isShuttingDown()) {
            mongo::sleepsecs(1);
        }
    }

    stdx::lock_guard<Latch> lock(_mutex);
    _client.reset();
    auto status = _checkForShutdownAndConvertStatus_inlock(databaseClonerFinishStatus,
                                                           "error cloning databases");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    status = _scheduleLastOplogEntryFetcher_inlock(
        [=](const StatusWith<mongo::Fetcher::QueryResponse>& status,
            mongo::Fetcher::NextAction*,
            mongo::BSONObjBuilder*) {
            _lastOplogEntryFetcherCallbackForStopTimestamp(status, onCompletionGuard);
        });
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_lastOplogEntryFetcherCallbackForStopTimestamp(
    const StatusWith<Fetcher::QueryResponse>& result,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    OpTimeAndWallTime resultOpTimeAndWallTime = {OpTime(), Date_t()};
    {
        stdx::lock_guard<Latch> lock(_mutex);
        auto status = _checkForShutdownAndConvertStatus_inlock(
            result.getStatus(), "error fetching last oplog entry for stop timestamp");
        if (!status.isOK()) {
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
            return;
        }

        auto&& optimeStatus = parseOpTimeAndWallTime(result);
        if (!optimeStatus.isOK()) {
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock,
                                                                      optimeStatus.getStatus());
            return;
        }
        resultOpTimeAndWallTime = optimeStatus.getValue();

        _initialSyncState->stopTimestamp = resultOpTimeAndWallTime.opTime.getTimestamp();

        // If the beginFetchingTimestamp is different from the stopTimestamp, it indicates that
        // there are oplog entries fetched by the oplog fetcher that need to be written to the oplog
        // and/or there are operations that need to be applied.
        if (_initialSyncState->beginFetchingTimestamp != _initialSyncState->stopTimestamp) {
            invariant(_lastApplied.opTime.isNull());
            _checkApplierProgressAndScheduleGetNextApplierBatch_inlock(lock, onCompletionGuard);
            return;
        }
    }

    // Oplog at sync source has not advanced since we started cloning databases, so we use the last
    // oplog entry to seed the oplog before checking the rollback ID.
    {
        const auto& documents = result.getValue().documents;
        invariant(!documents.empty());
        const BSONObj oplogSeedDoc = documents.front();
        LOG(2) << "Inserting oplog seed document: " << oplogSeedDoc;

        auto opCtx = makeOpCtx();
        // StorageInterface::insertDocument() has to be called outside the lock because we may
        // override its behavior in tests. See InitialSyncerReturnsCallbackCanceledAndDoesNot-
        // ScheduleRollbackCheckerIfShutdownAfterInsertingInsertOplogSeedDocument in
        // initial_syncer_test.cpp
        auto status = _storage->insertDocument(
            opCtx.get(),
            _opts.localOplogNS,
            TimestampedBSONObj{oplogSeedDoc, resultOpTimeAndWallTime.opTime.getTimestamp()},
            resultOpTimeAndWallTime.opTime.getTerm());
        if (!status.isOK()) {
            stdx::lock_guard<Latch> lock(_mutex);
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
            return;
        }
        const bool orderedCommit = true;
        _storage->oplogDiskLocRegister(
            opCtx.get(), resultOpTimeAndWallTime.opTime.getTimestamp(), orderedCommit);
    }

    stdx::lock_guard<Latch> lock(_mutex);
    _lastApplied = resultOpTimeAndWallTime;
    log() << "No need to apply operations. (currently at "
          << _initialSyncState->stopTimestamp.toBSON() << ")";

    // This sets the error in 'onCompletionGuard' and shuts down the OplogFetcher on error.
    _scheduleRollbackCheckerCheckForRollback_inlock(lock, onCompletionGuard);
}

void InitialSyncer::_getNextApplierBatchCallback(
    const executor::TaskExecutor::CallbackArgs& callbackArgs,
    std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::lock_guard<Latch> lock(_mutex);
    auto status =
        _checkForShutdownAndConvertStatus_inlock(callbackArgs, "error getting next applier batch");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    auto batchResult = _getNextApplierBatch_inlock();
    if (!batchResult.isOK()) {
        warning() << "Failure creating next apply batch: " << redact(batchResult.getStatus());
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, batchResult.getStatus());
        return;
    }

    std::string logMsg = str::stream()
        << "Initial Syncer is about to apply the next oplog batch of size: "
        << batchResult.getValue().size();
    pauseAtInitialSyncFuzzerSyncronizationPoints(logMsg);

    if (MONGO_unlikely(failInitialSyncBeforeApplyingBatch.shouldFail())) {
        log() << "initial sync - failInitialSyncBeforeApplyingBatch fail point enabled. Pausing"
              << "until fail point is disabled, then will fail initial sync.";
        failInitialSyncBeforeApplyingBatch.pauseWhileSet();
        status = Status(ErrorCodes::CallbackCanceled,
                        "failInitialSyncBeforeApplyingBatch fail point enabled");
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    // Schedule MultiApplier if we have operations to apply.
    const auto& ops = batchResult.getValue();
    if (!ops.empty()) {
        _fetchCount.store(0);
        MultiApplier::MultiApplyFn applyBatchOfOperationsFn = [this](OperationContext* opCtx,
                                                                     std::vector<OplogEntry> ops) {
            return _oplogApplier->applyOplogBatch(opCtx, std::move(ops));
        };
        OpTime lastApplied = ops.back().getOpTime();
        Date_t lastAppliedWall = ops.back().getWallClockTime();

        auto numApplied = ops.size();
        MultiApplier::CallbackFn onCompletionFn = [=](const Status& s) {
            return _multiApplierCallback(
                s, {lastApplied, lastAppliedWall}, numApplied, onCompletionGuard);
        };

        _applier = std::make_unique<MultiApplier>(
            _exec, ops, std::move(applyBatchOfOperationsFn), std::move(onCompletionFn));
        status = _startupComponent_inlock(_applier);
        if (!status.isOK()) {
            onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
            return;
        }
        return;
    }

    // If the oplog fetcher is no longer running (completed successfully) and the oplog buffer is
    // empty, we are not going to make any more progress with this initial sync. Report progress so
    // far and return a RemoteResultsUnavailable error.
    if (!_oplogFetcher->isActive()) {
        std::string msg = str::stream()
            << "The oplog fetcher is no longer running and we have applied all the oplog entries "
               "in the oplog buffer. Aborting this initial sync attempt. Last applied: "
            << _lastApplied.opTime.toString() << ". Last fetched: " << _lastFetched.toString()
            << ". Number of operations applied: " << _initialSyncState->appliedOps;
        log() << msg;
        status = Status(ErrorCodes::RemoteResultsUnavailable, msg);
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    // If there are no operations at the moment to apply and the oplog fetcher is still waiting on
    // the sync source, we'll check the oplog buffer again in
    // '_opts.getApplierBatchCallbackRetryWait' ms.
    auto when = _exec->now() + _opts.getApplierBatchCallbackRetryWait;
    status = _scheduleWorkAtAndSaveHandle_inlock(
        when,
        [=](const CallbackArgs& args) { _getNextApplierBatchCallback(args, onCompletionGuard); },
        &_getNextApplierBatchHandle,
        "_getNextApplierBatchCallback");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_multiApplierCallback(const Status& multiApplierStatus,
                                          OpTimeAndWallTime lastApplied,
                                          std::uint32_t numApplied,
                                          std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::lock_guard<Latch> lock(_mutex);
    auto status =
        _checkForShutdownAndConvertStatus_inlock(multiApplierStatus, "error applying batch");

    // Set to cause initial sync to fassert instead of restart if applying a batch fails, so that
    // tests can be robust to network errors but not oplog idempotency errors.
    if (MONGO_unlikely(initialSyncFassertIfApplyingBatchFails.shouldFail())) {
        log() << "initialSyncFassertIfApplyingBatchFails fail point enabled.";
        fassert(31210, status);
    }

    if (!status.isOK()) {
        error() << "Failed to apply batch due to '" << redact(status) << "'";
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    _initialSyncState->appliedOps += numApplied;
    _lastApplied = lastApplied;
    const auto lastAppliedOpTime = _lastApplied.opTime;
    _opts.setMyLastOptime(_lastApplied, ReplicationCoordinator::DataConsistency::Inconsistent);

    // Update oplog visibility after applying a batch so that while applying transaction oplog
    // entries, the TransactionHistoryIterator can get earlier oplog entries associated with the
    // transaction. Note that setting the oplog visibility timestamp here will be safe even if
    // initial sync was restarted because until initial sync ends, no one else will try to read our
    // oplog. It is also safe even if we tried to read from our own oplog because we never try to
    // read from the oplog before applying at least one batch and therefore setting a value for the
    // oplog visibility timestamp.
    auto opCtx = makeOpCtx();
    const bool orderedCommit = true;
    _storage->oplogDiskLocRegister(opCtx.get(), lastAppliedOpTime.getTimestamp(), orderedCommit);
    _checkApplierProgressAndScheduleGetNextApplierBatch_inlock(lock, onCompletionGuard);
}

void InitialSyncer::_rollbackCheckerCheckForRollbackCallback(
    const RollbackChecker::Result& result, std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    stdx::lock_guard<Latch> lock(_mutex);
    auto status = _checkForShutdownAndConvertStatus_inlock(result.getStatus(),
                                                           "error while getting last rollback ID");
    if (_shouldRetryNetworkError(lock, status)) {
        LOG(1) << "Retrying rollback checker because of network error " << status;
        _scheduleRollbackCheckerCheckForRollback_inlock(lock, onCompletionGuard);
        return;
    }

    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    auto hasHadRollback = result.getValue();
    if (hasHadRollback) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::UnrecoverableRollbackError,
                   str::stream() << "Rollback occurred on our sync source " << _syncSource
                                 << " during initial sync"));
        return;
    }

    // Success!
    onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, _lastApplied);
}

void InitialSyncer::_finishInitialSyncAttempt(const StatusWith<OpTimeAndWallTime>& lastApplied) {
    // Since _finishInitialSyncAttempt can be called from any component's callback function or
    // scheduled task, it is possible that we may not be in a TaskExecutor-managed thread when this
    // function is invoked.
    // For example, if CollectionCloner fails while inserting documents into the
    // CollectionBulkLoader, we will get here via one of CollectionCloner's TaskRunner callbacks
    // which has an active OperationContext bound to the current Client. This would lead to an
    // invariant when we attempt to create a new OperationContext for _tearDown(opCtx).
    // To avoid this, we schedule _finishCallback against the TaskExecutor rather than calling it
    // here synchronously.

    // Unless dismissed, a scope guard will schedule _finishCallback() upon exiting this function.
    // Since it is a requirement that _finishCallback be called outside the lock (which is possible
    // if the task scheduling fails and we have to invoke _finishCallback() synchronously), we
    // declare the scope guard before the lock guard.
    auto result = lastApplied;
    auto finishCallbackGuard = makeGuard([this, &result] {
        auto scheduleResult = _exec->scheduleWork(
            [=](const mongo::executor::TaskExecutor::CallbackArgs&) { _finishCallback(result); });
        if (!scheduleResult.isOK()) {
            warning() << "Unable to schedule initial syncer completion task due to "
                      << redact(scheduleResult.getStatus())
                      << ". Running callback on current thread.";
            _finishCallback(result);
        }
    });

    log() << "Initial sync attempt finishing up.";

    stdx::lock_guard<Latch> lock(_mutex);
    log() << "Initial Sync Attempt Statistics: " << redact(_getInitialSyncProgress_inlock());

    auto runTime = _initialSyncState ? _initialSyncState->timer.millis() : 0;
    _stats.initialSyncAttemptInfos.emplace_back(
        InitialSyncer::InitialSyncAttemptInfo{runTime, result.getStatus(), _syncSource});

    if (MONGO_unlikely(failAndHangInitialSync.shouldFail())) {
        log() << "failAndHangInitialSync fail point enabled.";
        failAndHangInitialSync.pauseWhileSet();
        result = Status(ErrorCodes::InternalError, "failAndHangInitialSync fail point enabled");
    }

    if (result.isOK()) {
        // Scope guard will invoke _finishCallback().
        return;
    }


    // This increments the number of failed attempts for the current initial sync request.
    ++_stats.failedInitialSyncAttempts;

    // This increments the number of failed attempts across all initial sync attempts since process
    // startup.
    initialSyncFailedAttempts.increment();

    error() << "Initial sync attempt failed -- attempts left: "
            << (_stats.maxFailedInitialSyncAttempts - _stats.failedInitialSyncAttempts)
            << " cause: " << redact(result.getStatus());

    // Check if need to do more retries.
    if (_stats.failedInitialSyncAttempts >= _stats.maxFailedInitialSyncAttempts) {
        const std::string err =
            "The maximum number of retries have been exhausted for initial sync.";
        severe() << err;

        initialSyncFailures.increment();

        // Scope guard will invoke _finishCallback().
        return;
    }

    auto when = _exec->now() + _opts.initialSyncRetryWait;
    auto status = _scheduleWorkAtAndSaveHandle_inlock(
        when,
        [=](const executor::TaskExecutor::CallbackArgs& args) {
            _startInitialSyncAttemptCallback(
                args, _stats.failedInitialSyncAttempts, _stats.maxFailedInitialSyncAttempts);
        },
        &_startInitialSyncAttemptHandle,
        str::stream() << "_startInitialSyncAttemptCallback-" << _stats.failedInitialSyncAttempts);

    if (!status.isOK()) {
        result = status;

        // Scope guard will invoke _finishCallback().
        return;
    }

    // Next initial sync attempt scheduled successfully and we do not need to call _finishCallback()
    // until the next initial sync attempt finishes.
    finishCallbackGuard.dismiss();
}

void InitialSyncer::_finishCallback(StatusWith<OpTimeAndWallTime> lastApplied) {
    // After running callback function, clear '_onCompletion' to release any resources that might be
    // held by this function object.
    // '_onCompletion' must be moved to a temporary copy and destroyed outside the lock in case
    // there is any logic that's invoked at the function object's destruction that might call into
    // this InitialSyncer. 'onCompletion' must be destroyed outside the lock and this should happen
    // before we transition the state to Complete.
    decltype(_onCompletion) onCompletion;
    {
        stdx::lock_guard<Latch> lock(_mutex);
        auto opCtx = makeOpCtx();
        _tearDown_inlock(opCtx.get(), lastApplied);

        invariant(_onCompletion);
        std::swap(_onCompletion, onCompletion);
    }

    if (MONGO_unlikely(initialSyncHangBeforeFinish.shouldFail())) {
        // This log output is used in js tests so please leave it.
        log() << "initial sync - initialSyncHangBeforeFinish fail point "
                 "enabled. Blocking until fail point is disabled.";
        while (MONGO_unlikely(initialSyncHangBeforeFinish.shouldFail()) && !_isShuttingDown()) {
            mongo::sleepsecs(1);
        }
    }

    // Completion callback must be invoked outside mutex.
    try {
        onCompletion(lastApplied);
    } catch (...) {
        warning() << "initial syncer finish callback threw exception: "
                  << redact(exceptionToStatus());
    }

    // Destroy the remaining reference to the completion callback before we transition the state to
    // Complete so that callers can expect any resources bound to '_onCompletion' to be released
    // before InitialSyncer::join() returns.
    onCompletion = {};

    stdx::lock_guard<Latch> lock(_mutex);
    invariant(_state != State::kComplete);
    _state = State::kComplete;
    _stateCondition.notify_all();

    // Clear the initial sync progress after an initial sync attempt has been successfully
    // completed.
    if (lastApplied.isOK() && !MONGO_unlikely(skipClearInitialSyncState.shouldFail())) {
        _initialSyncState.reset();
    }
}

Status InitialSyncer::_scheduleLastOplogEntryFetcher_inlock(Fetcher::CallbackFn callback) {
    BSONObj query = BSON("find" << _opts.remoteOplogNS.coll() << "sort" << BSON("$natural" << -1)
                                << "limit" << 1);

    _lastOplogEntryFetcher = std::make_unique<Fetcher>(
        _exec,
        _syncSource,
        _opts.remoteOplogNS.db().toString(),
        query,
        callback,
        ReadPreferenceSetting::secondaryPreferredMetadata(),
        RemoteCommandRequest::kNoTimeout /* find network timeout */,
        RemoteCommandRequest::kNoTimeout /* getMore network timeout */,
        RemoteCommandRetryScheduler::makeRetryPolicy<ErrorCategory::RetriableError>(
            numInitialSyncOplogFindAttempts.load(), executor::RemoteCommandRequest::kNoTimeout));
    Status scheduleStatus = _lastOplogEntryFetcher->schedule();
    if (!scheduleStatus.isOK()) {
        _lastOplogEntryFetcher.reset();
    }

    return scheduleStatus;
}

void InitialSyncer::_checkApplierProgressAndScheduleGetNextApplierBatch_inlock(
    const stdx::lock_guard<Latch>& lock, std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    // We should check our current state because shutdown() could have been called before
    // we re-acquired the lock.
    if (_isShuttingDown_inlock()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::CallbackCanceled,
                   "failed to schedule applier to check for "
                   "rollback: initial syncer is shutting down"));
        return;
    }

    // Basic sanity check on begin/stop timestamps.
    if (_initialSyncState->beginApplyingTimestamp > _initialSyncState->stopTimestamp) {
        std::string msg = str::stream()
            << "Possible rollback on sync source " << _syncSource.toString() << ". Currently at "
            << _initialSyncState->stopTimestamp.toBSON() << ". Started at "
            << _initialSyncState->beginApplyingTimestamp.toBSON();
        error() << msg;
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock, Status(ErrorCodes::OplogOutOfOrder, msg));
        return;
    }

    if (_lastApplied.opTime.isNull()) {
        // Check if any ops occurred while cloning or any ops need to be fetched.
        invariant(_initialSyncState->beginFetchingTimestamp < _initialSyncState->stopTimestamp);
        log() << "Writing to the oplog and applying operations until "
              << _initialSyncState->stopTimestamp.toBSON()
              << " before initial sync can complete. (started fetching at "
              << _initialSyncState->beginFetchingTimestamp.toBSON() << " and applying at "
              << _initialSyncState->beginApplyingTimestamp.toBSON() << ")";
        // Fall through to scheduling _getNextApplierBatchCallback().
    } else if (_lastApplied.opTime.getTimestamp() >= _initialSyncState->stopTimestamp) {
        // Check for rollback if we have applied far enough to be consistent.
        invariant(!_lastApplied.opTime.getTimestamp().isNull());
        _scheduleRollbackCheckerCheckForRollback_inlock(lock, onCompletionGuard);
        return;
    }

    // Get another batch to apply.
    // _scheduleWorkAndSaveHandle_inlock() is shutdown-aware.
    auto status = _scheduleWorkAndSaveHandle_inlock(
        [=](const executor::TaskExecutor::CallbackArgs& args) {
            return _getNextApplierBatchCallback(args, onCompletionGuard);
        },
        &_getNextApplierBatchHandle,
        "_getNextApplierBatchCallback");
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }
}

void InitialSyncer::_scheduleRollbackCheckerCheckForRollback_inlock(
    const stdx::lock_guard<Latch>& lock, std::shared_ptr<OnCompletionGuard> onCompletionGuard) {
    // We should check our current state because shutdown() could have been called before
    // we re-acquired the lock.
    if (_isShuttingDown_inlock()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(
            lock,
            Status(ErrorCodes::CallbackCanceled,
                   "failed to schedule rollback checker to check "
                   "for rollback: initial syncer is shutting "
                   "down"));
        return;
    }

    auto scheduleResult =
        _rollbackChecker->checkForRollback([=](const RollbackChecker::Result& result) {
            _rollbackCheckerCheckForRollbackCallback(result, onCompletionGuard);
        });

    auto status = scheduleResult.getStatus();
    if (!status.isOK()) {
        onCompletionGuard->setResultAndCancelRemainingWork_inlock(lock, status);
        return;
    }

    _getLastRollbackIdHandle = scheduleResult.getValue();
    return;
}

bool InitialSyncer::_shouldRetryNetworkError(WithLock lk, Status status) {
    if (ErrorCodes::isNetworkError(status)) {
        stdx::lock_guard<InitialSyncSharedData> sharedDataLock(*_sharedData);
        return _sharedData->shouldRetryOperation(sharedDataLock, &_retryingOperation);
    }
    // The status was OK or some error other than a network error, so clear the network error
    // state and indicate that we should not retry.
    _clearNetworkError(lk);
    return false;
}

void InitialSyncer::_clearNetworkError(WithLock lk) {
    _retryingOperation = boost::none;
}

Status InitialSyncer::_checkForShutdownAndConvertStatus_inlock(
    const executor::TaskExecutor::CallbackArgs& callbackArgs, const std::string& message) {
    return _checkForShutdownAndConvertStatus_inlock(callbackArgs.status, message);
}

Status InitialSyncer::_checkForShutdownAndConvertStatus_inlock(const Status& status,
                                                               const std::string& message) {

    if (_isShuttingDown_inlock()) {
        return Status(ErrorCodes::CallbackCanceled, message + ": initial syncer is shutting down");
    }

    return status.withContext(message);
}

Status InitialSyncer::_scheduleWorkAndSaveHandle_inlock(
    executor::TaskExecutor::CallbackFn work,
    executor::TaskExecutor::CallbackHandle* handle,
    const std::string& name) {
    invariant(handle);
    if (_isShuttingDown_inlock()) {
        return Status(ErrorCodes::CallbackCanceled,
                      str::stream() << "failed to schedule work " << name
                                    << ": initial syncer is shutting down");
    }
    auto result = _exec->scheduleWork(std::move(work));
    if (!result.isOK()) {
        return result.getStatus().withContext(str::stream() << "failed to schedule work " << name);
    }
    *handle = result.getValue();
    return Status::OK();
}

Status InitialSyncer::_scheduleWorkAtAndSaveHandle_inlock(
    Date_t when,
    executor::TaskExecutor::CallbackFn work,
    executor::TaskExecutor::CallbackHandle* handle,
    const std::string& name) {
    invariant(handle);
    if (_isShuttingDown_inlock()) {
        return Status(ErrorCodes::CallbackCanceled,
                      str::stream() << "failed to schedule work " << name << " at "
                                    << when.toString() << ": initial syncer is shutting down");
    }
    auto result = _exec->scheduleWorkAt(when, std::move(work));
    if (!result.isOK()) {
        return result.getStatus().withContext(str::stream() << "failed to schedule work " << name
                                                            << " at " << when.toString());
    }
    *handle = result.getValue();
    return Status::OK();
}

void InitialSyncer::_cancelHandle_inlock(executor::TaskExecutor::CallbackHandle handle) {
    if (!handle) {
        return;
    }
    _exec->cancel(handle);
}

template <typename Component>
Status InitialSyncer::_startupComponent_inlock(Component& component) {
    if (_isShuttingDown_inlock()) {
        component.reset();
        return Status(ErrorCodes::CallbackCanceled,
                      "initial syncer shutdown while trying to call startup() on component");
    }
    auto status = component->startup();
    if (!status.isOK()) {
        component.reset();
    }
    return status;
}

template <typename Component>
void InitialSyncer::_shutdownComponent_inlock(Component& component) {
    if (!component) {
        return;
    }
    component->shutdown();
}

StatusWith<std::vector<OplogEntry>> InitialSyncer::_getNextApplierBatch_inlock() {
    // If the fail-point is active, delay the apply batch by returning an empty batch so that
    // _getNextApplierBatchCallback() will reschedule itself at a later time.
    // See InitialSyncerOptions::getApplierBatchCallbackRetryWait.
    if (MONGO_unlikely(rsSyncApplyStop.shouldFail())) {
        return std::vector<OplogEntry>();
    }

    // Obtain next batch of operations from OplogApplier.
    auto opCtx = makeOpCtx();
    OplogApplier::BatchLimits batchLimits;
    batchLimits.bytes = replBatchLimitBytes.load();
    batchLimits.ops = getBatchLimitOplogEntries();
    // We want a batch boundary after the beginApplyingTimestamp, to make sure all oplog entries
    // that are part of a transaction before that timestamp are written out before we start applying
    // entries after them.  This is because later entries may be commit or prepare and thus
    // expect to read the partial entries from the oplog.
    batchLimits.forceBatchBoundaryAfter = _initialSyncState->beginApplyingTimestamp;
    return _oplogApplier->getNextApplierBatch(opCtx.get(), batchLimits);
}

StatusWith<HostAndPort> InitialSyncer::_chooseSyncSource_inlock() {
    auto syncSource = _opts.syncSourceSelector->chooseNewSyncSource(_lastFetched);
    if (syncSource.empty()) {
        return Status{ErrorCodes::InvalidSyncSource,
                      str::stream() << "No valid sync source available. Our last fetched optime: "
                                    << _lastFetched.toString()};
    }
    return syncSource;
}

Status InitialSyncer::_enqueueDocuments(Fetcher::Documents::const_iterator begin,
                                        Fetcher::Documents::const_iterator end,
                                        const OplogFetcher::DocumentsInfo& info) {
    if (info.toApplyDocumentCount == 0) {
        return Status::OK();
    }

    if (_isShuttingDown()) {
        return Status::OK();
    }

    invariant(_oplogBuffer);

    // Wait for enough space.
    _oplogApplier->waitForSpace(makeOpCtx().get(), info.toApplyDocumentBytes);

    // Buffer docs for later application.
    _oplogApplier->enqueue(makeOpCtx().get(), begin, end);

    _lastFetched = info.lastDocument;

    // TODO: updates metrics with "info".
    return Status::OK();
}

std::string InitialSyncer::Stats::toString() const {
    return toBSON().toString();
}

BSONObj InitialSyncer::Stats::toBSON() const {
    BSONObjBuilder bob;
    append(&bob);
    return bob.obj();
}

void InitialSyncer::Stats::append(BSONObjBuilder* builder) const {
    builder->appendNumber("failedInitialSyncAttempts",
                          static_cast<long long>(failedInitialSyncAttempts));
    builder->appendNumber("maxFailedInitialSyncAttempts",
                          static_cast<long long>(maxFailedInitialSyncAttempts));
    if (initialSyncStart != Date_t()) {
        builder->appendDate("initialSyncStart", initialSyncStart);
        if (initialSyncEnd != Date_t()) {
            builder->appendDate("initialSyncEnd", initialSyncEnd);
            auto elapsed = initialSyncEnd - initialSyncStart;
            long long elapsedMillis = duration_cast<Milliseconds>(elapsed).count();
            builder->appendNumber("initialSyncElapsedMillis", elapsedMillis);
        }
    }
    BSONArrayBuilder arrBuilder(builder->subarrayStart("initialSyncAttempts"));
    for (unsigned int i = 0; i < initialSyncAttemptInfos.size(); ++i) {
        arrBuilder.append(initialSyncAttemptInfos[i].toBSON());
    }
    arrBuilder.doneFast();
}

std::string InitialSyncer::InitialSyncAttemptInfo::toString() const {
    return toBSON().toString();
}

BSONObj InitialSyncer::InitialSyncAttemptInfo::toBSON() const {
    BSONObjBuilder bob;
    append(&bob);
    return bob.obj();
}

void InitialSyncer::InitialSyncAttemptInfo::append(BSONObjBuilder* builder) const {
    builder->appendNumber("durationMillis", durationMillis);
    builder->append("status", status.toString());
    builder->append("syncSource", syncSource.toString());
}

}  // namespace repl
}  // namespace mongo
