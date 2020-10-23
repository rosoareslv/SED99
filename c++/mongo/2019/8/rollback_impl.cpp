
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplicationRollback

#include "mongo/platform/basic.h"

#include "mongo/db/repl/rollback_impl.h"
#include "mongo/db/repl/rollback_impl_gen.h"

#include <fmt/format.h>

#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/background.h"
#include "mongo/db/catalog/collection_catalog.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/commands.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/concurrency/replication_state_transition_lock_guard.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/index_builds_coordinator.h"
#include "mongo/db/kill_sessions_local.h"
#include "mongo/db/logical_time_validator.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/apply_ops.h"
#include "mongo/db/repl/drop_pending_collection_reaper.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/roll_back_local_operations.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/transaction_oplog_application.h"
#include "mongo/db/s/shard_identity_rollback_notifier.h"
#include "mongo/db/s/type_shard_identity.h"
#include "mongo/db/server_recovery.h"
#include "mongo/db/session_catalog_mongod.h"
#include "mongo/db/session_txn_record_gen.h"
#include "mongo/db/storage/remove_saver.h"
#include "mongo/db/transaction_history_iterator.h"
#include "mongo/s/catalog/type_config_version.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace repl {

using namespace fmt::literals;

MONGO_FAIL_POINT_DEFINE(rollbackHangAfterTransitionToRollback);

namespace {

// Used to set RollbackImpl::_newCounts to force a collection scan to fix count.
constexpr long long kCollectionScanRequired = -1;

RollbackImpl::Listener kNoopListener;

// The name of the insert, update and delete commands as found in oplog command entries.
constexpr auto kInsertCmdName = "insert"_sd;
constexpr auto kUpdateCmdName = "update"_sd;
constexpr auto kDeleteCmdName = "delete"_sd;
constexpr auto kNumRecordsFieldName = "numRecords"_sd;
constexpr auto kToFieldName = "to"_sd;
constexpr auto kDropTargetFieldName = "dropTarget"_sd;

/**
 * Parses the o2 field of a drop or rename oplog entry for the count of the collection that was
 * dropped.
 */
boost::optional<long long> _parseDroppedCollectionCount(const OplogEntry& oplogEntry) {
    auto commandType = oplogEntry.getCommandType();
    auto desc = OplogEntry::CommandType::kDrop == commandType ? "drop oplog entry"_sd
                                                              : "rename oplog entry"_sd;

    auto obj2 = oplogEntry.getObject2();
    if (!obj2) {
        warning() << "Unable to get collection count from " << desc
                  << " without the o2 "
                     "field. oplog op: "
                  << redact(oplogEntry.toBSON());
        return boost::none;
    }

    long long count = 0;
    // TODO: Use IDL to parse o2 object. See txn_cmds.idl for example.
    auto status = bsonExtractIntegerField(*obj2, kNumRecordsFieldName, &count);
    if (!status.isOK()) {
        warning() << "Failed to parse " << desc << " for collection count: " << status
                  << ". oplog op: " << redact(oplogEntry.toBSON());
        return boost::none;
    }

    if (count < 0) {
        warning() << "Invalid collection count found in " << desc << ": " << count
                  << ". oplog op: " << redact(oplogEntry.toBSON());
        return boost::none;
    }

    LOG(2) << "Parsed collection count of " << count << " from " << desc
           << ". oplog op: " << redact(oplogEntry.toBSON());
    return count;
}

}  // namespace

constexpr const char* RollbackImpl::kRollbackRemoveSaverType;
constexpr const char* RollbackImpl::kRollbackRemoveSaverWhy;

bool RollbackImpl::shouldCreateDataFiles() {
    return gCreateRollbackDataFiles.load();
}

RollbackImpl::RollbackImpl(OplogInterface* localOplog,
                           OplogInterface* remoteOplog,
                           StorageInterface* storageInterface,
                           ReplicationProcess* replicationProcess,
                           ReplicationCoordinator* replicationCoordinator,
                           Listener* listener)
    : _listener(listener),
      _localOplog(localOplog),
      _remoteOplog(remoteOplog),
      _storageInterface(storageInterface),
      _replicationProcess(replicationProcess),
      _replicationCoordinator(replicationCoordinator) {

    invariant(localOplog);
    invariant(remoteOplog);
    invariant(storageInterface);
    invariant(replicationProcess);
    invariant(replicationCoordinator);
    invariant(listener);
}

RollbackImpl::RollbackImpl(OplogInterface* localOplog,
                           OplogInterface* remoteOplog,
                           StorageInterface* storageInterface,
                           ReplicationProcess* replicationProcess,
                           ReplicationCoordinator* replicationCoordinator)
    : RollbackImpl(localOplog,
                   remoteOplog,
                   storageInterface,
                   replicationProcess,
                   replicationCoordinator,
                   &kNoopListener) {}

RollbackImpl::~RollbackImpl() {
    shutdown();
}

Status RollbackImpl::runRollback(OperationContext* opCtx) {
    _rollbackStats.startTime = opCtx->getServiceContext()->getFastClockSource()->now();

    auto status = _transitionToRollback(opCtx);
    if (!status.isOK()) {
        return status;
    }
    _listener->onTransitionToRollback();

    if (MONGO_FAIL_POINT(rollbackHangAfterTransitionToRollback)) {
        log() << "rollbackHangAfterTransitionToRollback fail point enabled. Blocking until fail "
                 "point is disabled (rollback_impl).";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET_OR_INTERRUPTED(opCtx,
                                                        rollbackHangAfterTransitionToRollback);
    }

    // We clear the SizeRecoveryState before we recover to a stable timestamp. This ensures that we
    // only use size adjustment markings from the storage and replication recovery processes in this
    // rollback.
    sizeRecoveryState(opCtx->getServiceContext()).clearStateBeforeRecovery();

    // After successfully transitioning to the ROLLBACK state, we must always transition back to
    // SECONDARY, even if we fail at any point during the rollback process.
    ON_BLOCK_EXIT([this, opCtx] { _transitionFromRollbackToSecondary(opCtx); });
    ON_BLOCK_EXIT([this, opCtx] { _summarizeRollback(opCtx); });

    // Wait for all background index builds to complete before starting the rollback process.
    status = _awaitBgIndexCompletion(opCtx);
    if (!status.isOK()) {
        return status;
    }
    _listener->onBgIndexesComplete();

    auto commonPointSW = _findCommonPoint(opCtx);
    if (!commonPointSW.isOK()) {
        return commonPointSW.getStatus();
    }

    const auto commonPoint = commonPointSW.getValue();
    const OpTime commonPointOpTime = commonPoint.getOpTime();
    _rollbackStats.commonPoint = commonPointOpTime;
    _listener->onCommonPointFound(commonPointOpTime.getTimestamp());

    // Now that we have found the common point, we make sure to proceed only if the rollback
    // period is not too long.
    status = _checkAgainstTimeLimit(commonPoint);
    if (!status.isOK()) {
        return status;
    }

    // Increment the Rollback ID of this node. The Rollback ID is a natural number that it is
    // incremented by 1 every time a rollback occurs. Note that the Rollback ID must be incremented
    // before modifying any local data.
    status = _replicationProcess->incrementRollbackID(opCtx);
    if (!status.isOK()) {
        return status;
    }
    _rollbackStats.rollbackId = _replicationProcess->getRollbackID();
    _listener->onRollbackIDIncremented();

    // This function cannot fail without terminating the process.
    _runPhaseFromAbortToReconstructPreparedTxns(opCtx, commonPoint);
    _listener->onPreparedTransactionsReconstructed();

    // We can now accept interruptions again.
    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }

    // At this point, the last applied and durable optimes on this node still point to ops on
    // the divergent branch of history. We therefore update the last optimes to the top of the
    // oplog, which should now be at the common point.
    _replicationCoordinator->resetLastOpTimesFromOplog(
        opCtx, ReplicationCoordinator::DataConsistency::Consistent);
    status = _triggerOpObserver(opCtx);
    if (!status.isOK()) {
        return status;
    }
    _listener->onRollbackOpObserver(_observerInfo);

    log() << "Rollback complete";

    return Status::OK();
}

void RollbackImpl::shutdown() {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    _inShutdown = true;
}

bool RollbackImpl::_isInShutdown() const {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    return _inShutdown;
}

namespace {
void killAllUserOperations(OperationContext* opCtx) {
    invariant(opCtx);
    ServiceContext* serviceCtx = opCtx->getServiceContext();
    invariant(serviceCtx);

    int numOpsKilled = 0;

    for (ServiceContext::LockedClientsCursor cursor(serviceCtx); Client* client = cursor.next();) {
        stdx::lock_guard<Client> lk(*client);
        if (client->isFromSystemConnection() && !client->shouldKillSystemOperation(lk)) {
            continue;
        }

        OperationContext* toKill = client->getOperationContext();

        if (toKill && toKill->getOpID() == opCtx->getOpID()) {
            // Don't kill the rollback thread.
            continue;
        }

        if (toKill && !toKill->isKillPending()) {
            serviceCtx->killOperation(lk, toKill, ErrorCodes::InterruptedDueToReplStateChange);
            numOpsKilled++;
        }
    }

    log() << "Killed {} operation(s) while transitioning to ROLLBACK"_format(numOpsKilled);
}
}  // namespace

Status RollbackImpl::_transitionToRollback(OperationContext* opCtx) {
    invariant(opCtx);
    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }

    log() << "transition to ROLLBACK";
    {
        ReplicationStateTransitionLockGuard rstlLock(
            opCtx, MODE_X, ReplicationStateTransitionLockGuard::EnqueueOnly());

        // Kill all user operations to ensure we can successfully acquire the RSTL. Since the node
        // must be a secondary, this is only killing readers, whose connections will be closed
        // shortly regardless.
        killAllUserOperations(opCtx);

        rstlLock.waitForLockUntil(Date_t::max());

        auto status =
            _replicationCoordinator->setFollowerModeStrict(opCtx, MemberState::RS_ROLLBACK);
        if (!status.isOK()) {
            status.addContext(str::stream()
                              << "Cannot transition from "
                              << _replicationCoordinator->getMemberState().toString() << " to "
                              << MemberState(MemberState::RS_ROLLBACK).toString());
            log() << status;
            return status;
        }
    }
    return Status::OK();
}

Status RollbackImpl::_awaitBgIndexCompletion(OperationContext* opCtx) {
    invariant(opCtx);
    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }

    // Get a list of all databases.
    StorageEngine* storageEngine = opCtx->getServiceContext()->getStorageEngine();
    std::vector<std::string> dbs;
    {
        Lock::GlobalLock lk(opCtx, MODE_IS);
        dbs = storageEngine->listDatabases();
    }

    // Wait for all background operations to complete by waiting on each database.
    std::vector<StringData> dbNames(dbs.begin(), dbs.end());
    log() << "Waiting for all background operations to complete before starting rollback";
    for (auto db : dbNames) {
        auto numInProg = BackgroundOperation::numInProgForDb(db);
        auto numInProgInCoordinator = IndexBuildsCoordinator::get(opCtx)->numInProgForDb(db);
        if (numInProg > 0 || numInProgInCoordinator > 0) {
            LOG(1) << "Waiting for "
                   << (numInProg > numInProgInCoordinator ? numInProg : numInProgInCoordinator)
                   << " background operations to complete on database '" << db << "'";
            BackgroundOperation::awaitNoBgOpInProgForDb(db);
            IndexBuildsCoordinator::get(opCtx)->awaitNoBgOpInProgForDb(db);
        }

        // Check for shutdown again.
        if (_isInShutdown()) {
            return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
        }
    }

    log() << "Finished waiting for background operations to complete before rollback";
    return Status::OK();
}

StatusWith<std::set<NamespaceString>> RollbackImpl::_namespacesForOp(const OplogEntry& oplogEntry) {
    NamespaceString opNss = oplogEntry.getNss();
    OpTypeEnum opType = oplogEntry.getOpType();
    std::set<NamespaceString> namespaces;

    // No namespaces for a no-op.
    if (opType == OpTypeEnum::kNoop) {
        return std::set<NamespaceString>();
    }

    // CRUD ops have the proper namespace in the operation 'ns' field.
    if (opType == OpTypeEnum::kInsert || opType == OpTypeEnum::kUpdate ||
        opType == OpTypeEnum::kDelete) {
        return std::set<NamespaceString>({opNss});
    }

    // If the operation is a command, then we need to extract the appropriate namespaces from the
    // command object, as opposed to just using the 'ns' field of the oplog entry itself.
    if (opType == OpTypeEnum::kCommand) {
        auto obj = oplogEntry.getObject();
        auto firstElem = obj.firstElement();

        // Does not handle 'applyOps' entries.
        invariant(oplogEntry.getCommandType() != OplogEntry::CommandType::kApplyOps,
                  "_namespacesForOp does not handle 'applyOps' oplog entries.");

        switch (oplogEntry.getCommandType()) {
            case OplogEntry::CommandType::kRenameCollection: {
                // Add both the 'from' and 'to' namespaces.
                namespaces.insert(NamespaceString(firstElem.valuestrsafe()));
                namespaces.insert(NamespaceString(obj.getStringField("to")));
                break;
            }
            case OplogEntry::CommandType::kDropDatabase: {
                // There is no specific namespace to save for a drop database operation.
                break;
            }
            case OplogEntry::CommandType::kDbCheck:
            case OplogEntry::CommandType::kConvertToCapped:
            case OplogEntry::CommandType::kEmptyCapped: {
                // These commands do not need to be supported by rollback. 'convertToCapped' should
                // always be converted to lower level DDL operations, and 'emptycapped' is a
                // testing-only command.
                std::string message = str::stream()
                    << "Encountered unsupported command type '" << firstElem.fieldName()
                    << "' during rollback.";
                return Status(ErrorCodes::UnrecoverableRollbackError, message);
            }
            case OplogEntry::CommandType::kCreate:
            case OplogEntry::CommandType::kDrop:
            case OplogEntry::CommandType::kCreateIndexes:
            case OplogEntry::CommandType::kDropIndexes:
            case OplogEntry::CommandType::kCollMod: {
                // For all other command types, we should be able to parse the collection name from
                // the first command argument.
                try {
                    auto cmdNss = CommandHelpers::parseNsCollectionRequired(opNss.db(), obj);
                    namespaces.insert(cmdNss);
                } catch (const DBException& ex) {
                    return ex.toStatus();
                }
                break;
            }
            // TODO(SERVER-39451): Ignore no-op startIndexBuild and commitIndexBuild commands.
            // Revisit when we are ready to implement rollback logic.
            case OplogEntry::CommandType::kStartIndexBuild:
            case OplogEntry::CommandType::kCommitIndexBuild:
            case OplogEntry::CommandType::kCommitTransaction:
            case OplogEntry::CommandType::kAbortTransaction: {
                break;
            }
            case OplogEntry::CommandType::kApplyOps:
            default:
                // Every possible command type should be handled above.
                MONGO_UNREACHABLE
        }
    }

    return namespaces;
}

void RollbackImpl::_runPhaseFromAbortToReconstructPreparedTxns(
    OperationContext* opCtx, RollBackLocalOperations::RollbackCommonPoint commonPoint) noexcept {
    // Before computing record store counts, abort all active transactions. This ensures that
    // the count adjustments are based on correct values where no prepared transactions are
    // active and all in-memory counts have been rolled-back.
    // Before calling recoverToStableTimestamp, we must abort the storage transaction of any
    // prepared transaction. This will require us to scan all sessions and call
    // abortPreparedTransactionForRollback() on any txnParticipant with a prepared transaction.
    killSessionsAbortAllPreparedTransactions(opCtx);

    // Ask the record store for the pre-rollback counts of any collections whose counts will
    // change and create a map with the adjusted counts for post-rollback. While finding the
    // common point, we keep track of how much each collection's count will change during the
    // rollback. Note: these numbers are relative to the common point, not the stable timestamp,
    // and thus must be set after recovering from the oplog.
    auto status = _findRecordStoreCounts(opCtx);
    fassert(31227, status);

    if (shouldCreateDataFiles()) {
        // Write a rollback file for each namespace that has documents that would be deleted by
        // rollback. We need to do this after aborting prepared transactions. Otherwise, we risk
        // unecessary prepare conflicts when trying to read documents that were modified by
        // those prepared transactions, which we know we will abort anyway.
        status = _writeRollbackFiles(opCtx);
        fassert(31228, status);
    } else {
        log() << "Not writing rollback files. 'createRollbackDataFiles' set to false.";
    }

    // If there were rolled back operations on any session, invalidate all sessions.
    // We invalidate sessions before we recover so that we avoid invalidating sessions that had
    // just recovered prepared transactions.
    if (!_observerInfo.rollbackSessionIds.empty()) {
        MongoDSessionCatalog::invalidateAllSessions(opCtx);
    }

    // Recover to the stable timestamp.
    auto stableTimestampSW = _recoverToStableTimestamp(opCtx);
    fassert(31049, stableTimestampSW);

    _rollbackStats.stableTimestamp = stableTimestampSW.getValue();
    _listener->onRecoverToStableTimestamp(stableTimestampSW.getValue());

    // Log the total number of insert and update operations that have been rolled back as a
    // result of recovering to the stable timestamp.
    log() << "Rollback reverted " << _observerInfo.rollbackCommandCounts[kInsertCmdName]
          << " insert operations, " << _observerInfo.rollbackCommandCounts[kUpdateCmdName]
          << " update operations and " << _observerInfo.rollbackCommandCounts[kDeleteCmdName]
          << " delete operations.";

    // During replication recovery, we truncate all oplog entries with timestamps greater than
    // or equal to the oplog truncate after point. As a result, we must find the oplog entry
    // after the common point so we do not truncate the common point itself. If we entered
    // rollback, we are guaranteed to have at least one oplog entry after the common point.
    Timestamp truncatePoint = _findTruncateTimestamp(opCtx, commonPoint);

    // Persist the truncate point to the 'oplogTruncateAfterPoint' document. We save this value so
    // that the replication recovery logic knows where to truncate the oplog. We save this value
    // durably to match the behavior during startup recovery. This must occur after we successfully
    // recover to a stable timestamp. If recovering to a stable timestamp fails and we still
    // truncate the oplog then the oplog will not match the data files. If we crash at any earlier
    // point, we will recover, find a new sync source, and restart roll back (if necessary on the
    // new sync source). This is safe because a crash before this point would recover to a stable
    // checkpoint anyways at or earlier than the stable timestamp.
    //
    // Note that storage engine timestamp recovery only restores the database *data* to a stable
    // timestamp, but does not revert the oplog, which must be done as part of the rollback process.
    _replicationProcess->getConsistencyMarkers()->setOplogTruncateAfterPoint(opCtx, truncatePoint);
    _rollbackStats.truncateTimestamp = truncatePoint;
    _listener->onSetOplogTruncateAfterPoint(truncatePoint);

    // Align the drop pending reaper state with what's on disk. Oplog recovery depends on those
    // being consistent.
    _resetDropPendingState(opCtx);

    // Run the recovery process.
    _replicationProcess->getReplicationRecovery()->recoverFromOplog(opCtx,
                                                                    stableTimestampSW.getValue());
    _listener->onRecoverFromOplog();

    // Sets the correct post-rollback counts on any collections whose counts changed during the
    // rollback.
    _correctRecordStoreCounts(opCtx);

    // Reconstruct prepared transactions after counts have been adjusted. Since prepared
    // transactions were aborted (i.e. the in-memory counts were rolled-back) before computing
    // collection counts, reconstruct the prepared transactions now, adding on any additional counts
    // to the now corrected record store.
    reconstructPreparedTransactions(opCtx, OplogApplication::Mode::kRecovering);
}

void RollbackImpl::_correctRecordStoreCounts(OperationContext* opCtx) {
    // This function explicitly does not check for shutdown since a clean shutdown post oplog
    // truncation is not allowed to occur until the record store counts are corrected.
    const auto& catalog = CollectionCatalog::get(opCtx);
    for (const auto& uiCount : _newCounts) {
        const auto uuid = uiCount.first;
        const auto coll = catalog.lookupCollectionByUUID(uuid);
        invariant(coll,
                  str::stream() << "The collection with UUID " << uuid
                                << " is unexpectedly missing in the CollectionCatalog");
        const auto nss = coll->ns();
        invariant(!nss.isEmpty(),
                  str::stream() << "The collection with UUID " << uuid << " has no namespace.");
        const auto ident = coll->getRecordStore()->getIdent();
        invariant(!ident.empty(),
                  str::stream() << "The collection with UUID " << uuid << " has no ident.");

        auto newCount = uiCount.second;
        // If the collection is marked for size adjustment, then we made sure the collection size
        // was accurate at the stable timestamp and we can trust replication recovery to keep it
        // correct. This is necessary for capped collections whose deletions will be untracked
        // if we just set the collection count here.
        if (sizeRecoveryState(opCtx->getServiceContext())
                .collectionAlwaysNeedsSizeAdjustment(ident)) {
            LOG(2) << "Not setting collection count to " << newCount << " for " << nss.ns() << " ("
                   << uuid.toString() << ") [" << ident
                   << "] because it is marked for size adjustment.";
            continue;
        }

        // If _findRecordStoreCounts() is unable to determine the correct count from the oplog
        // (most likely due to a 4.0 drop oplog entry without the count information), we will
        // determine the correct count here post-recovery using a collection scan.
        if (kCollectionScanRequired == newCount) {
            log() << "Scanning collection " << nss.ns() << " (" << uuid.toString()
                  << ") to fix collection count.";
            AutoGetCollectionForRead autoCollToScan(opCtx, nss);
            auto collToScan = autoCollToScan.getCollection();
            invariant(coll == collToScan,
                      str::stream() << "Catalog returned invalid collection: " << nss.ns() << " ("
                                    << uuid.toString() << ")");
            auto exec = collToScan->makePlanExecutor(
                opCtx, PlanExecutor::INTERRUPT_ONLY, Collection::ScanDirection::kForward);
            long long countFromScan = 0;
            PlanExecutor::ExecState state;
            while (PlanExecutor::ADVANCED == (state = exec->getNext(nullptr, nullptr))) {
                ++countFromScan;
            }
            if (PlanExecutor::IS_EOF != state) {
                // We ignore errors here because crashing or leaving rollback would only leave
                // collection counts more inaccurate.
                warning() << "Failed to set count of " << nss.ns() << " (" << uuid.toString()
                          << ") [" << ident
                          << "] due to failed collection scan: " << exec->statestr(state);
                continue;
            }
            newCount = countFromScan;
        }

        auto status =
            _storageInterface->setCollectionCount(opCtx, {nss.db().toString(), uuid}, newCount);
        if (!status.isOK()) {
            // We ignore errors here because crashing or leaving rollback would only leave
            // collection counts more inaccurate.
            warning() << "Failed to set count of " << nss.ns() << " (" << uuid.toString() << ") ["
                      << ident << "] to " << newCount << ". Received: " << status;
        } else {
            LOG(2) << "Set collection count of " << nss.ns() << " (" << uuid.toString() << ") ["
                   << ident << "] to " << newCount << ".";
        }
    }
}

Status RollbackImpl::_findRecordStoreCounts(OperationContext* opCtx) {
    const auto& catalog = CollectionCatalog::get(opCtx);
    auto storageEngine = opCtx->getServiceContext()->getStorageEngine();

    log() << "finding record store counts";
    for (const auto& uiCount : _countDiffs) {
        auto uuid = uiCount.first;
        auto countDiff = uiCount.second;
        if (countDiff == 0) {
            continue;
        }

        auto nss = catalog.lookupNSSByUUID(uuid);
        StorageInterface::CollectionCount oldCount = 0;

        // Drop-pending collections are not visible to rollback via the catalog when they are
        // managed by the storage engine. See StorageEngine::supportsPendingDrops().
        if (!nss) {
            invariant(storageEngine->supportsPendingDrops(),
                      str::stream() << "The collection with UUID " << uuid
                                    << " is unexpectedly missing in the CollectionCatalog");
            auto it = _pendingDrops.find(uuid);
            if (it == _pendingDrops.end()) {
                _newCounts[uuid] = kCollectionScanRequired;
                continue;
            }
            const auto& dropPendingInfo = it->second;
            nss = dropPendingInfo.nss;
            invariant(dropPendingInfo.count >= 0,
                      str::stream() << "The collection with UUID " << uuid
                                    << " was dropped with a negative collection count of "
                                    << dropPendingInfo.count
                                    << " in the drop or rename oplog entry. Unable to reset "
                                       "collection count during rollback.");
            oldCount = static_cast<StorageInterface::CollectionCount>(dropPendingInfo.count);
        } else {
            auto countSW = _storageInterface->getCollectionCount(opCtx, *nss);
            if (!countSW.isOK()) {
                return countSW.getStatus();
            }
            oldCount = countSW.getValue();
        }

        if (oldCount > static_cast<uint64_t>(std::numeric_limits<long long>::max())) {
            warning() << "Count for " << nss->ns() << " (" << uuid.toString() << ") was "
                      << oldCount
                      << " which is larger than the maximum int64_t value. Not attempting to fix "
                         "count during rollback.";
            continue;
        }

        long long oldCountSigned = static_cast<long long>(oldCount);
        auto newCount = oldCountSigned + countDiff;

        if (newCount < 0) {
            warning() << "Attempted to set count for " << nss->ns() << " (" << uuid.toString()
                      << ") to " << newCount
                      << " but set it to 0 instead. This is likely due to the count previously "
                         "becoming inconsistent from an unclean shutdown or a rollback that could "
                         "not fix the count correctly. Old count: "
                      << oldCount << ". Count change: " << countDiff;
            newCount = 0;
        }
        LOG(2) << "Record count of " << nss->ns() << " (" << uuid.toString()
               << ") before rollback is " << oldCount << ". Setting it to " << newCount
               << ", due to change of " << countDiff;
        _newCounts[uuid] = newCount;
    }

    return Status::OK();
}

/**
 * Process a single oplog entry that is getting rolled back and update the necessary rollback info
 * structures.
 */
Status RollbackImpl::_processRollbackOp(OperationContext* opCtx, const OplogEntry& oplogEntry) {
    ++_observerInfo.numberOfEntriesObserved;

    NamespaceString opNss = oplogEntry.getNss();
    OpTypeEnum opType = oplogEntry.getOpType();

    // For applyOps entries, we process each sub-operation individually.
    if (oplogEntry.getCommandType() == OplogEntry::CommandType::kApplyOps) {
        if (oplogEntry.shouldPrepare()) {
            // Uncommitted prepared transactions are always aborted before rollback begins, which
            // rolls back collection counts. Processing the operation here would result in
            // double-counting the sub-operations when correcting collection counts later.
            // Additionally, this logic makes an assumption that transactions are only ever
            // committed when the prepare operation is majority committed. This implies that when a
            // prepare oplog entry is rolled-back, it is guaranteed that it has never committed.
            return Status::OK();
        }
        if (oplogEntry.isPartialTransaction()) {
            // This oplog entry will be processed when we rollback the implicit commit for the
            // unprepared transaction (applyOps without partialTxn field).
            return Status::OK();
        }
        // Follow chain on applyOps oplog entries to process entire unprepared transaction.
        // The beginning of the applyOps chain may precede the common point.
        auto status = _processRollbackOpForApplyOps(opCtx, oplogEntry);
        if (const auto prevOpTime = oplogEntry.getPrevWriteOpTimeInTransaction()) {
            for (TransactionHistoryIterator iter(*prevOpTime); status.isOK() && iter.hasNext();) {
                status = _processRollbackOpForApplyOps(opCtx, iter.next(opCtx));
            }
        }
        return status;
    }

    // No information to record for a no-op.
    if (opType == OpTypeEnum::kNoop) {
        return Status::OK();
    }

    // Extract the appropriate namespaces from the oplog operation.
    auto namespacesSW = _namespacesForOp(oplogEntry);
    if (!namespacesSW.isOK()) {
        return namespacesSW.getStatus();
    } else {
        _observerInfo.rollbackNamespaces.insert(namespacesSW.getValue().begin(),
                                                namespacesSW.getValue().end());
    }

    // If the operation being rolled back has a session id, then we add it to the set of
    // sessions that had operations rolled back.
    OperationSessionInfo opSessionInfo = oplogEntry.getOperationSessionInfo();
    auto sessionId = opSessionInfo.getSessionId();
    if (sessionId) {
        _observerInfo.rollbackSessionIds.insert(sessionId->getId());
    }

    // Keep track of the _ids of inserted and updated documents, as we may need to write them out to
    // a rollback file.
    if (opType == OpTypeEnum::kInsert || opType == OpTypeEnum::kUpdate) {
        const auto uuid = oplogEntry.getUuid();
        invariant(uuid,
                  str::stream() << "Oplog entry to roll back is unexpectedly missing a UUID: "
                                << redact(oplogEntry.toBSON()));
        const auto idElem = oplogEntry.getIdElement();
        if (!idElem.eoo()) {
            // We call BSONElement::wrap() on each _id element to create a new BSONObj with an owned
            // buffer, as the underlying storage may be gone when we access this map to write
            // rollback files.
            _observerInfo.rollbackDeletedIdsMap[uuid.get()].insert(idElem.wrap());
            const auto cmdName = opType == OpTypeEnum::kInsert ? kInsertCmdName : kUpdateCmdName;
            ++_observerInfo.rollbackCommandCounts[cmdName];
        }
    }

    if (opType == OpTypeEnum::kInsert) {
        auto idVal = oplogEntry.getObject().getStringField("_id");
        if (serverGlobalParams.clusterRole == ClusterRole::ShardServer &&
            opNss == NamespaceString::kServerConfigurationNamespace &&
            idVal == ShardIdentityType::IdName) {
            // Check if the creation of the shard identity document is being rolled back.
            _observerInfo.shardIdentityRolledBack = true;
            warning() << "Shard identity document rollback detected. oplog op: "
                      << redact(oplogEntry.toBSON());
        } else if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer &&
                   opNss == VersionType::ConfigNS) {
            // Check if the creation of the config server config version document is being rolled
            // back.
            _observerInfo.configServerConfigVersionRolledBack = true;
            warning() << "Config version document rollback detected. oplog op: "
                      << redact(oplogEntry.toBSON());
        }

        // Rolling back an insert must decrement the count by 1.
        _countDiffs[oplogEntry.getUuid().get()] -= 1;
    } else if (opType == OpTypeEnum::kDelete) {
        // Rolling back a delete must increment the count by 1.
        _countDiffs[oplogEntry.getUuid().get()] += 1;
    } else if (opType == OpTypeEnum::kCommand) {
        if (oplogEntry.getCommandType() == OplogEntry::CommandType::kCreate) {
            // If we roll back a create, then we do not need to change the size of that uuid.
            _countDiffs.erase(oplogEntry.getUuid().get());
            _pendingDrops.erase(oplogEntry.getUuid().get());
            _newCounts.erase(oplogEntry.getUuid().get());
        } else if (oplogEntry.getCommandType() == OplogEntry::CommandType::kDrop) {
            // If we roll back a collection drop, parse the o2 field for the collection count for
            // use later by _findRecordStoreCounts().
            // This will be used to reconcile collection counts in the case where the drop-pending
            // collection is managed by the storage engine and is not accessible through the UUID
            // catalog.
            // Adding a _newCounts entry ensures that the count will be set after the rollback.
            const auto uuid = oplogEntry.getUuid().get();
            invariant(_countDiffs.find(uuid) == _countDiffs.end(),
                      str::stream() << "Unexpected existing count diff for " << uuid.toString()
                                    << " op: " << redact(oplogEntry.toBSON()));
            if (auto countResult = _parseDroppedCollectionCount(oplogEntry)) {
                PendingDropInfo info;
                info.count = *countResult;
                const auto& opNss = oplogEntry.getNss();
                info.nss =
                    CommandHelpers::parseNsCollectionRequired(opNss.db(), oplogEntry.getObject());
                _pendingDrops[uuid] = info;
                _newCounts[uuid] = info.count;
            } else {
                _newCounts[uuid] = kCollectionScanRequired;
            }
        } else if (oplogEntry.getCommandType() == OplogEntry::CommandType::kRenameCollection &&
                   oplogEntry.getObject()[kDropTargetFieldName].trueValue()) {
            // If we roll back a rename with a dropped target collection, parse the o2 field for the
            // target collection count for use later by _findRecordStoreCounts().
            // This will be used to reconcile collection counts in the case where the drop-pending
            // collection is managed by the storage engine and is not accessible through the UUID
            // catalog.
            // Adding a _newCounts entry ensures that the count will be set after the rollback.
            auto dropTargetUUID = invariant(
                UUID::parse(oplogEntry.getObject()[kDropTargetFieldName]),
                str::stream()
                    << "Oplog entry to roll back is unexpectedly missing dropTarget UUID: "
                    << redact(oplogEntry.toBSON()));
            invariant(_countDiffs.find(dropTargetUUID) == _countDiffs.end(),
                      str::stream()
                          << "Unexpected existing count diff for " << dropTargetUUID.toString()
                          << " op: " << redact(oplogEntry.toBSON()));
            if (auto countResult = _parseDroppedCollectionCount(oplogEntry)) {
                PendingDropInfo info;
                info.count = *countResult;
                info.nss = NamespaceString(oplogEntry.getObject()[kToFieldName].String());
                _pendingDrops[dropTargetUUID] = info;
                _newCounts[dropTargetUUID] = info.count;
            } else {
                _newCounts[dropTargetUUID] = kCollectionScanRequired;
            }
        } else if (oplogEntry.getCommandType() == OplogEntry::CommandType::kCommitTransaction) {
            // If we are rolling-back the commit of a prepared transaction, use the prepare oplog
            // entry to compute size adjustments. After recovering to the stable timestamp, prepared
            // transactions are reconstituted and any count adjustments will be replayed and
            // committed again.
            if (const auto prevOpTime = oplogEntry.getPrevWriteOpTimeInTransaction()) {
                for (TransactionHistoryIterator iter(*prevOpTime); iter.hasNext();) {
                    auto nextOplogEntry = iter.next(opCtx);
                    if (nextOplogEntry.getCommandType() != OplogEntry::CommandType::kApplyOps) {
                        continue;
                    }
                    auto status = _processRollbackOpForApplyOps(opCtx, nextOplogEntry);
                    if (!status.isOK()) {
                        return status;
                    }
                }
            }
            return Status::OK();
        }
    }

    // Keep count of major commands that will be rolled back.
    if (opType == OpTypeEnum::kCommand) {
        ++_observerInfo.rollbackCommandCounts[oplogEntry.getObject().firstElementFieldName()];
    }
    if (opType == OpTypeEnum::kDelete) {
        ++_observerInfo.rollbackCommandCounts[kDeleteCmdName];
    }

    return Status::OK();
}

Status RollbackImpl::_processRollbackOpForApplyOps(OperationContext* opCtx,
                                                   const OplogEntry& oplogEntry) {
    invariant(oplogEntry.getCommandType() == OplogEntry::CommandType::kApplyOps);

    try {
        auto subOps = ApplyOps::extractOperations(oplogEntry);
        for (auto& subOp : subOps) {
            auto subStatus = _processRollbackOp(opCtx, subOp);
            if (!subStatus.isOK()) {
                return subStatus;
            }
        }
        return Status::OK();
    } catch (DBException& e) {
        return e.toStatus();
    }

    return Status::OK();
}

StatusWith<RollBackLocalOperations::RollbackCommonPoint> RollbackImpl::_findCommonPoint(
    OperationContext* opCtx) {
    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }

    log() << "finding common point";

    // We save some aggregate information about all operations that are rolled back, so that we can
    // pass this information to the rollback op observer. In most cases, other subsystems do not
    // need to know extensive details about every operation that rolled back, so to reduce
    // complexity by adding observer methods for every operation type, we provide a set of
    // information that should be suitable for most other subsystems to take the necessary actions
    // on a rollback event. This rollback info is kept in memory, so if we crash after we collect
    // it, it may be lost. However, if we crash any time between recovering to a stable timestamp
    // and completing oplog recovery, we assume that this information is not needed, since the node
    // restarting will have cleared out any invalid in-memory state anyway.
    auto onLocalOplogEntryFn = [&](const BSONObj& operation) {
        OplogEntry oplogEntry(operation);
        return _processRollbackOp(opCtx, oplogEntry);
    };

    // Calls syncRollBackLocalOperations to find the common point and run onLocalOplogEntryFn on
    // each oplog entry up until the common point. We only need the Timestamp of the common point
    // for the oplog truncate after point. Along the way, we save some information about the
    // rollback ops.
    auto commonPointSW =
        syncRollBackLocalOperations(*_localOplog, *_remoteOplog, onLocalOplogEntryFn);
    if (!commonPointSW.isOK()) {
        return commonPointSW.getStatus();
    }

    OpTime commonPointOpTime = commonPointSW.getValue().getOpTime();
    OpTime lastCommittedOpTime = _replicationCoordinator->getLastCommittedOpTime();
    OpTime committedSnapshot = _replicationCoordinator->getCurrentCommittedSnapshotOpTime();
    auto stableTimestamp =
        _storageInterface->getLastStableRecoveryTimestamp(opCtx->getServiceContext());

    log() << "Rollback common point is " << commonPointOpTime;

    // Rollback common point should be >= the replication commit point.
    invariant(commonPointOpTime.getTimestamp() >= lastCommittedOpTime.getTimestamp());
    invariant(commonPointOpTime >= lastCommittedOpTime);

    // Rollback common point should be >= the committed snapshot optime.
    invariant(commonPointOpTime.getTimestamp() >= committedSnapshot.getTimestamp());
    invariant(commonPointOpTime >= committedSnapshot);

    // Rollback common point should be >= the stable timestamp.
    invariant(stableTimestamp);
    if (commonPointOpTime.getTimestamp() < *stableTimestamp) {
        // This is an fassert rather than an invariant, since it can happen if the server was
        // recently upgraded to enableMajorityReadConcern=true.
        severe() << "Common point must be at least stable timestamp, common point: "
                 << commonPointOpTime.getTimestamp() << ", stable timestamp: " << *stableTimestamp;
        fassertFailedNoTrace(51121);
    }

    return commonPointSW.getValue();
}

Status RollbackImpl::_checkAgainstTimeLimit(
    RollBackLocalOperations::RollbackCommonPoint commonPoint) {

    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }

    auto localOplogIter = _localOplog->makeIterator();
    const auto topOfOplogSW = localOplogIter->next();
    if (!topOfOplogSW.isOK()) {
        return Status(ErrorCodes::OplogStartMissing, "no oplog during rollback");
    }
    const auto topOfOplogBSON = topOfOplogSW.getValue().first;
    const auto topOfOplog = uassertStatusOK(OplogEntry::parse(topOfOplogBSON));

    _rollbackStats.lastLocalOptime = topOfOplog.getOpTime();

    auto topOfOplogWallOpt = topOfOplog.getWallClockTime();
    // We check the difference between the top of the oplog and the first oplog entry after the
    // common point when computing the rollback time limit.
    auto firstOpWallClockTimeAfterCommonPointOpt =
        commonPoint.getFirstOpWallClockTimeAfterCommonPoint();


    // Only compute the difference if both the top of the oplog and the first oplog entry after the
    // common point have wall clock times.
    if (firstOpWallClockTimeAfterCommonPointOpt && topOfOplogWallOpt) {
        auto topOfOplogWallTime = topOfOplogWallOpt.get();
        auto firstOpWallClockTimeAfterCommonPoint = firstOpWallClockTimeAfterCommonPointOpt.get();

        if (topOfOplogWallTime >= firstOpWallClockTimeAfterCommonPoint) {

            unsigned long long diff = durationCount<Seconds>(
                Milliseconds(topOfOplogWallTime - firstOpWallClockTimeAfterCommonPoint));

            _rollbackStats.lastLocalWallClockTime = topOfOplogWallTime;
            _rollbackStats.firstOpWallClockTimeAfterCommonPoint =
                firstOpWallClockTimeAfterCommonPoint;

            auto timeLimit = static_cast<unsigned long long>(gRollbackTimeLimitSecs.loadRelaxed());
            if (diff > timeLimit) {
                return Status(ErrorCodes::UnrecoverableRollbackError,
                              str::stream() << "not willing to roll back more than " << timeLimit
                                            << " seconds of data. Have: " << diff << " seconds.");
            }

        } else {
            warning()
                << "Wall clock times on oplog entries not monotonically increasing. This "
                   "might indicate a backward clock skew. Time at first oplog after common point: "
                << firstOpWallClockTimeAfterCommonPoint
                << ". Time at top of oplog: " << topOfOplogWallTime;
        }
    }

    return Status::OK();
}

Timestamp RollbackImpl::_findTruncateTimestamp(
    OperationContext* opCtx, RollBackLocalOperations::RollbackCommonPoint commonPoint) const {

    AutoGetCollectionForRead oplog(opCtx, NamespaceString::kRsOplogNamespace);
    invariant(oplog.getCollection());
    auto oplogCursor = oplog.getCollection()->getCursor(opCtx, /*forward=*/true);

    auto commonPointRecord = oplogCursor->seekExact(commonPoint.getRecordId());
    auto commonPointOpTime = commonPoint.getOpTime();
    // Check that we've found the right document for the common point.
    invariant(commonPointRecord);
    auto commonPointTime = OpTime::parseFromOplogEntry(commonPointRecord->data.releaseToBson());
    invariant(commonPointTime.getStatus());
    invariant(commonPointTime.getValue() == commonPointOpTime,
              str::stream() << "Common point: " << commonPointOpTime.toString()
                            << ", record found: " << commonPointTime.getValue().toString());

    // Get the next document, which will be the first document to truncate.
    auto truncatePointRecord = oplogCursor->next();
    invariant(truncatePointRecord);
    auto truncatePointTime = OpTime::parseFromOplogEntry(truncatePointRecord->data.releaseToBson());
    invariant(truncatePointTime.getStatus());

    log() << "Marking to truncate all oplog entries with timestamps greater than or equal to "
          << truncatePointTime.getValue();
    return truncatePointTime.getValue().getTimestamp();
}

boost::optional<BSONObj> RollbackImpl::_findDocumentById(OperationContext* opCtx,
                                                         UUID uuid,
                                                         NamespaceString nss,
                                                         BSONElement id) {
    auto document = _storageInterface->findById(opCtx, {nss.db().toString(), uuid}, id);
    if (document.isOK()) {
        return document.getValue();
    } else if (document.getStatus().code() == ErrorCodes::NoSuchKey) {
        return boost::none;
    } else {
        severe() << "Rollback failed to read document with " << redact(id) << " in namespace "
                 << nss.ns() << " with uuid " << uuid.toString() << causedBy(document.getStatus());
        fassert(50751, document.getStatus());
    }

    MONGO_UNREACHABLE;
}

Status RollbackImpl::_writeRollbackFiles(OperationContext* opCtx) {
    const auto& catalog = CollectionCatalog::get(opCtx);
    auto storageEngine = opCtx->getServiceContext()->getStorageEngine();
    for (auto&& entry : _observerInfo.rollbackDeletedIdsMap) {
        const auto& uuid = entry.first;
        const auto nss = catalog.lookupNSSByUUID(uuid);

        // Drop-pending collections are not visible to rollback via the catalog when they are
        // managed by the storage engine. See StorageEngine::supportsPendingDrops().
        if (!nss && storageEngine->supportsPendingDrops()) {
            log() << "The collection with UUID " << uuid
                  << " is missing in the CollectionCatalog. This could be due to a dropped "
                     " collection. Not writing rollback file for uuid "
                  << uuid;
            continue;
        }

        invariant(nss,
                  str::stream() << "The collection with UUID " << uuid
                                << " is unexpectedly missing in the CollectionCatalog");

        _writeRollbackFileForNamespace(opCtx, uuid, *nss, entry.second);
    }

    return Status::OK();
}

void RollbackImpl::_writeRollbackFileForNamespace(OperationContext* opCtx,
                                                  UUID uuid,
                                                  NamespaceString nss,
                                                  const SimpleBSONObjUnorderedSet& idSet) {
    RemoveSaver removeSaver(kRollbackRemoveSaverType, uuid.toString(), kRollbackRemoveSaverWhy);
    log() << "Preparing to write deleted documents to a rollback file for collection " << nss.ns()
          << " with uuid " << uuid.toString() << " to " << removeSaver.file().generic_string();

    // The RemoveSaver will save the data files in a directory structure similar to the following:
    //
    //     rollback
    //     ├── uuid
    //     │   └── removed.2018-03-20T20-23-01.21.bson
    //     ├── otheruuid
    //     │   ├── removed.2018-03-20T20-23-01.18.bson
    //     │   └── removed.2018-03-20T20-23-01.19.bson
    //
    // If this is the first data directory created, we save the full directory path in
    // _rollbackStats. Otherwise, we store the longest common prefix of the two directories.
    const auto& newDirectoryPath = removeSaver.root().generic_string();
    if (!_rollbackStats.rollbackDataFileDirectory) {
        _rollbackStats.rollbackDataFileDirectory = newDirectoryPath;
    } else {
        const auto& existingDirectoryPath = *_rollbackStats.rollbackDataFileDirectory;
        const auto& prefixEnd = std::mismatch(newDirectoryPath.begin(),
                                              newDirectoryPath.end(),
                                              existingDirectoryPath.begin(),
                                              existingDirectoryPath.end())
                                    .first;
        _rollbackStats.rollbackDataFileDirectory = std::string(newDirectoryPath.begin(), prefixEnd);
    }

    for (auto&& id : idSet) {
        // StorageInterface::findById() does not respect the collation, but because we are using
        // exact _id fields recorded in the oplog, we can get away with binary string
        // comparisons.
        auto document = _findDocumentById(opCtx, uuid, nss, id.firstElement());
        if (document) {
            fassert(50750, removeSaver.goingToDelete(*document));
        }
    }
    _listener->onRollbackFileWrittenForNamespace(std::move(uuid), std::move(nss));
}

StatusWith<Timestamp> RollbackImpl::_recoverToStableTimestamp(OperationContext* opCtx) {
    // Recover to the stable timestamp while holding the global exclusive lock. This may throw,
    // which the caller must handle.
    Lock::GlobalWrite globalWrite(opCtx);
    return _storageInterface->recoverToStableTimestamp(opCtx);
}

Status RollbackImpl::_triggerOpObserver(OperationContext* opCtx) {
    if (_isInShutdown()) {
        return Status(ErrorCodes::ShutdownInProgress, "rollback shutting down");
    }
    log() << "Triggering the rollback op observer";
    opCtx->getServiceContext()->getOpObserver()->onReplicationRollback(opCtx, _observerInfo);
    return Status::OK();
}

void RollbackImpl::_transitionFromRollbackToSecondary(OperationContext* opCtx) {
    invariant(opCtx);
    invariant(_replicationCoordinator->getMemberState() == MemberState(MemberState::RS_ROLLBACK));

    log() << "transition to SECONDARY";

    ReplicationStateTransitionLockGuard transitionGuard(opCtx, MODE_X);

    auto status = _replicationCoordinator->setFollowerMode(MemberState::RS_SECONDARY);
    if (!status.isOK()) {
        severe() << "Failed to transition into " << MemberState(MemberState::RS_SECONDARY)
                 << "; expected to be in state " << MemberState(MemberState::RS_ROLLBACK)
                 << "; found self in " << _replicationCoordinator->getMemberState()
                 << causedBy(status);
        fassertFailedNoTrace(40408);
    }
}

void RollbackImpl::_resetDropPendingState(OperationContext* opCtx) {
    // TODO(SERVER-38671): Remove this line when drop-pending idents are always supported with this
    // rolback method. Until then, we should assume that pending drops can be handled by either the
    // replication subsystem or the storage engine.
    DropPendingCollectionReaper::get(opCtx)->clearDropPendingState();

    // After recovering to a timestamp, the list of drop-pending idents maintained by the storage
    // engine is no longer accurate and needs to be cleared.
    auto storageEngine = opCtx->getServiceContext()->getStorageEngine();
    storageEngine->clearDropPendingState();

    std::vector<std::string> dbNames = storageEngine->listDatabases();
    auto databaseHolder = DatabaseHolder::get(opCtx);
    for (const auto& dbName : dbNames) {
        Lock::DBLock dbLock(opCtx, dbName, MODE_X);
        auto db = databaseHolder->openDb(opCtx, dbName);
        db->checkForIdIndexesAndDropPendingCollections(opCtx);
    }
}

void RollbackImpl::_summarizeRollback(OperationContext* opCtx) const {
    log() << "Rollback summary:";
    log() << "\tstart time: " << _rollbackStats.startTime;
    log() << "\tend time: " << opCtx->getServiceContext()->getFastClockSource()->now();
    log() << "\tsync source: " << _remoteOplog->hostAndPort().toString();
    log() << "\trollback data file directory: "
          << _rollbackStats.rollbackDataFileDirectory.value_or("none; no files written");
    if (_rollbackStats.rollbackId) {
        log() << "\trollback id: " << *_rollbackStats.rollbackId;
    }
    if (_rollbackStats.lastLocalOptime) {
        log() << "\tlast optime on branch of history rolled back: "
              << *_rollbackStats.lastLocalOptime;
    }
    if (_rollbackStats.commonPoint) {
        log() << "\tcommon point optime: " << *_rollbackStats.commonPoint;
    }
    if (_rollbackStats.lastLocalWallClockTime &&
        _rollbackStats.firstOpWallClockTimeAfterCommonPoint) {

        auto lastWall = *_rollbackStats.lastLocalWallClockTime;
        auto firstOpWallClockTimeAfterCommonPoint =
            *_rollbackStats.firstOpWallClockTimeAfterCommonPoint;
        unsigned long long diff =
            durationCount<Seconds>(Milliseconds(lastWall - firstOpWallClockTimeAfterCommonPoint));

        log() << "\tlast wall clock time on the branch of history rolled back: " << lastWall;
        log() << "\twall clock time of the first operation after the common point: "
              << firstOpWallClockTimeAfterCommonPoint;
        log() << "\tdifference in wall clock times: " << diff << " second(s)";
    }
    if (_rollbackStats.truncateTimestamp) {
        log() << "\ttruncate timestamp: " << *_rollbackStats.truncateTimestamp;
    }
    if (_rollbackStats.stableTimestamp) {
        log() << "\tstable timestamp: " << *_rollbackStats.stableTimestamp;
    }
    log() << "\tshard identity document rolled back: " << std::boolalpha
          << _observerInfo.shardIdentityRolledBack;
    log() << "\tconfig server config version document rolled back: " << std::boolalpha
          << _observerInfo.configServerConfigVersionRolledBack;
    log() << "\taffected sessions: " << (_observerInfo.rollbackSessionIds.empty() ? "none" : "");
    for (const auto& sessionId : _observerInfo.rollbackSessionIds) {
        log() << "\t\t" << sessionId;
    }
    log() << "\taffected namespaces: " << (_observerInfo.rollbackNamespaces.empty() ? "none" : "");
    for (const auto& nss : _observerInfo.rollbackNamespaces) {
        log() << "\t\t" << nss.ns();
    }
    log() << "\tcounts of interesting commands rolled back: "
          << (_observerInfo.rollbackCommandCounts.empty() ? "none" : "");
    for (const auto& entry : _observerInfo.rollbackCommandCounts) {
        log() << "\t\t" << entry.first << ": " << entry.second;
    }
    log() << "\ttotal number of entries rolled back (including no-ops): "
          << _observerInfo.numberOfEntriesObserved;
}

}  // namespace repl
}  // namespace mongo
