/**
 *    Copyright (C) 2017 MongoDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/db/session.h"

#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/commands/feature_compatibility_version_documentation.h"
#include "mongo/db/commands/test_commands_enabled.h"
#include "mongo/db/concurrency/lock_state.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/index/index_access_method.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/ops/update.h"
#include "mongo/db/query/get_executor.h"
#include "mongo/db/repl/read_concern_args.h"
#include "mongo/db/retryable_writes_stats.h"
#include "mongo/db/server_options.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/stats/fill_locker_info.h"
#include "mongo/db/transaction_history_iterator.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/fail_point_service.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/sock.h"

namespace mongo {

// Server parameter that dictates the lifetime given to each transaction.
// Transactions must eventually expire to preempt storage cache pressure immobilizing the system.
server_parameter_storage_type<int, ServerParameterType::kStartupAndRuntime>::value_type
    transactionLifetimeLimitSeconds(60);

const OperationContext::Decoration<Session::TransactionState> Session::TransactionState::get =
    OperationContext::declareDecoration<Session::TransactionState>();

Session::CursorKillFunction Session::_cursorKillFunction;
Session::CursorExistsFunction Session::_cursorExistsFunction;

/**
 * Implements a validation function for server parameter 'transactionLifetimeLimitSeconds'
 * instantiated above. 'transactionLifetimeLimitSeconds' can only be set to >= 1.
 */
class ExportedTransactionLifetimeLimitSeconds
    : public ExportedServerParameter<std::int32_t, ServerParameterType::kStartupAndRuntime> {
public:
    ExportedTransactionLifetimeLimitSeconds()
        : ExportedServerParameter<std::int32_t, ServerParameterType::kStartupAndRuntime>(
              ServerParameterSet::getGlobal(),
              "transactionLifetimeLimitSeconds",
              &transactionLifetimeLimitSeconds) {}

    Status validate(const std::int32_t& potentialNewValue) override {
        if (potentialNewValue < 1) {
            return Status(ErrorCodes::BadValue,
                          "transactionLifetimeLimitSeconds must be greater than or equal to 1s");
        }

        return Status::OK();
    }

} exportedTransactionLifetimeLimitSeconds;

namespace {

void fassertOnRepeatedExecution(const LogicalSessionId& lsid,
                                TxnNumber txnNumber,
                                StmtId stmtId,
                                const repl::OpTime& firstOpTime,
                                const repl::OpTime& secondOpTime) {
    severe() << "Statement id " << stmtId << " from transaction [ " << lsid.toBSON() << ":"
             << txnNumber << " ] was committed once with opTime " << firstOpTime
             << " and a second time with opTime " << secondOpTime
             << ". This indicates possible data corruption or server bug and the process will be "
                "terminated.";
    fassertFailed(40526);
}

struct ActiveTransactionHistory {
    boost::optional<SessionTxnRecord> lastTxnRecord;
    Session::CommittedStatementTimestampMap committedStatements;
    bool transactionCommitted{false};
    bool hasIncompleteHistory{false};
};

ActiveTransactionHistory fetchActiveTransactionHistory(OperationContext* opCtx,
                                                       const LogicalSessionId& lsid) {
    ActiveTransactionHistory result;

    result.lastTxnRecord = [&]() -> boost::optional<SessionTxnRecord> {
        DBDirectClient client(opCtx);
        auto result =
            client.findOne(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                           {BSON(SessionTxnRecord::kSessionIdFieldName << lsid.toBSON())});
        if (result.isEmpty()) {
            return boost::none;
        }

        return SessionTxnRecord::parse(IDLParserErrorContext("parse latest txn record for session"),
                                       result);
    }();

    if (!result.lastTxnRecord) {
        return result;
    }

    auto it = TransactionHistoryIterator(result.lastTxnRecord->getLastWriteOpTime());
    while (it.hasNext()) {
        try {
            const auto entry = it.next(opCtx);
            invariant(entry.getStatementId());

            if (*entry.getStatementId() == kIncompleteHistoryStmtId) {
                // Only the dead end sentinel can have this id for oplog write history
                invariant(entry.getObject2());
                invariant(entry.getObject2()->woCompare(Session::kDeadEndSentinel) == 0);
                result.hasIncompleteHistory = true;
                continue;
            }

            const auto insertRes =
                result.committedStatements.emplace(*entry.getStatementId(), entry.getOpTime());
            if (!insertRes.second) {
                const auto& existingOpTime = insertRes.first->second;
                fassertOnRepeatedExecution(lsid,
                                           result.lastTxnRecord->getTxnNum(),
                                           *entry.getStatementId(),
                                           existingOpTime,
                                           entry.getOpTime());
            }

            // applyOps oplog entry marks the commit of a transaction.
            if (entry.isCommand() &&
                entry.getCommandType() == repl::OplogEntry::CommandType::kApplyOps) {
                result.transactionCommitted = true;
            }
        } catch (const DBException& ex) {
            if (ex.code() == ErrorCodes::IncompleteTransactionHistory) {
                result.hasIncompleteHistory = true;
                break;
            }

            throw;
        }
    }

    return result;
}

void updateSessionEntry(OperationContext* opCtx, const UpdateRequest& updateRequest) {
    // Current code only supports replacement update.
    dassert(UpdateDriver::isDocReplacement(updateRequest.getUpdates()));

    AutoGetCollection autoColl(opCtx, NamespaceString::kSessionTransactionsTableNamespace, MODE_IX);

    uassert(40527,
            str::stream() << "Unable to persist transaction state because the session transaction "
                             "collection is missing. This indicates that the "
                          << NamespaceString::kSessionTransactionsTableNamespace.ns()
                          << " collection has been manually deleted.",
            autoColl.getCollection());

    WriteUnitOfWork wuow(opCtx);

    auto collection = autoColl.getCollection();
    auto idIndex = collection->getIndexCatalog()->findIdIndex(opCtx);

    uassert(40672,
            str::stream() << "Failed to fetch _id index for "
                          << NamespaceString::kSessionTransactionsTableNamespace.ns(),
            idIndex);

    auto indexAccess = collection->getIndexCatalog()->getIndex(idIndex);
    // Since we are looking up a key inside the _id index, create a key object consisting of only
    // the _id field.
    auto idToFetch = updateRequest.getQuery().firstElement();
    auto toUpdateIdDoc = idToFetch.wrap();
    dassert(idToFetch.fieldNameStringData() == "_id"_sd);
    auto recordId = indexAccess->findSingle(opCtx, toUpdateIdDoc);
    auto startingSnapshotId = opCtx->recoveryUnit()->getSnapshotId();

    if (recordId.isNull()) {
        // Upsert case.
        auto status = collection->insertDocument(
            opCtx, InsertStatement(updateRequest.getUpdates()), nullptr, true, false);

        if (status == ErrorCodes::DuplicateKey) {
            throw WriteConflictException();
        }

        uassertStatusOK(status);
        wuow.commit();
        return;
    }

    auto originalRecordData = collection->getRecordStore()->dataFor(opCtx, recordId);
    auto originalDoc = originalRecordData.toBson();

    invariant(collection->getDefaultCollator() == nullptr);
    boost::intrusive_ptr<ExpressionContext> expCtx(new ExpressionContext(opCtx, nullptr));

    auto matcher =
        fassert(40673, MatchExpressionParser::parse(updateRequest.getQuery(), std::move(expCtx)));
    if (!matcher->matchesBSON(originalDoc)) {
        // Document no longer match what we expect so throw WCE to make the caller re-examine.
        throw WriteConflictException();
    }

    OplogUpdateEntryArgs args;
    args.nss = NamespaceString::kSessionTransactionsTableNamespace;
    args.uuid = collection->uuid();
    args.update = updateRequest.getUpdates();
    args.criteria = toUpdateIdDoc;
    args.fromMigrate = false;

    collection->updateDocument(opCtx,
                               recordId,
                               Snapshotted<BSONObj>(startingSnapshotId, originalDoc),
                               updateRequest.getUpdates(),
                               true,   // enforceQuota
                               false,  // indexesAffected = false because _id is the only index
                               nullptr,
                               &args);

    wuow.commit();
}

// Failpoint which allows different failure actions to happen after each write. Supports the
// parameters below, which can be combined with each other (unless explicitly disallowed):
//
// closeConnection (bool, default = true): Closes the connection on which the write was executed.
// failBeforeCommitExceptionCode (int, default = not specified): If set, the specified exception
//      code will be thrown, which will cause the write to not commit; if not specified, the write
//      will be allowed to commit.
MONGO_FP_DECLARE(onPrimaryTransactionalWrite);

// Failpoint which will pause an operation just after allocating a point-in-time storage engine
// transaction.
MONGO_FP_DECLARE(hangAfterPreallocateSnapshot);
}  // namespace

const BSONObj Session::kDeadEndSentinel(BSON("$incompleteOplogHistory" << 1));

Session::Session(LogicalSessionId sessionId) : _sessionId(std::move(sessionId)) {}

void Session::refreshFromStorageIfNeeded(OperationContext* opCtx) {
    if (opCtx->getClient()->isInDirectClient()) {
        return;
    }

    invariant(!opCtx->lockState()->isLocked());
    invariant(repl::ReadConcernArgs::get(opCtx).getLevel() ==
              repl::ReadConcernLevel::kLocalReadConcern);

    stdx::unique_lock<stdx::mutex> ul(_mutex);

    while (!_isValid) {
        const int numInvalidations = _numInvalidations;

        ul.unlock();

        auto activeTxnHistory = fetchActiveTransactionHistory(opCtx, _sessionId);

        ul.lock();

        // Protect against concurrent refreshes or invalidations
        if (!_isValid && _numInvalidations == numInvalidations) {
            _isValid = true;
            _lastWrittenSessionRecord = std::move(activeTxnHistory.lastTxnRecord);

            if (_lastWrittenSessionRecord) {
                _activeTxnNumber = _lastWrittenSessionRecord->getTxnNum();
                _activeTxnCommittedStatements = std::move(activeTxnHistory.committedStatements);
                _hasIncompleteHistory = activeTxnHistory.hasIncompleteHistory;
                if (activeTxnHistory.transactionCommitted) {
                    _txnState = MultiDocumentTransactionState::kCommitted;
                }
            }

            break;
        }
    }
}

void Session::beginOrContinueTxn(OperationContext* opCtx,
                                 TxnNumber txnNumber,
                                 boost::optional<bool> autocommit,
                                 boost::optional<bool> startTransaction) {
    if (opCtx->getClient()->isInDirectClient()) {
        return;
    }

    // If the command specified a read preference that allows it to run on a secondary, and it is
    // trying to execute an operation on a multi-statement transaction, then we throw an error.
    // Transactions are only allowed to be run on a primary.
    if (!getTestCommandsEnabled()) {
        uassert(50789,
                "readPreference=primary is the only allowed readPreference for multi-statement "
                "transactions.",
                !(autocommit && ReadPreferenceSetting::get(opCtx).canRunOnSecondary()));
    }

    invariant(!opCtx->lockState()->isLocked());

    TxnNumber txnNumberAtStart;
    bool canKillCursors = false;
    {
        stdx::lock_guard<stdx::mutex> lg(_mutex);
        txnNumberAtStart = _activeTxnNumber;
        _beginOrContinueTxn(lg, opCtx, txnNumber, autocommit, startTransaction, &canKillCursors);
    }

    if (canKillCursors) {
        _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
    }
}

void Session::beginOrContinueTxnOnMigration(OperationContext* opCtx, TxnNumber txnNumber) {
    invariant(!opCtx->getClient()->isInDirectClient());
    invariant(!opCtx->lockState()->isLocked());

    TxnNumber txnNumberAtStart;
    bool canKillCursors = false;
    {
        stdx::lock_guard<stdx::mutex> lg(_mutex);
        txnNumberAtStart = _activeTxnNumber;
        _beginOrContinueTxnOnMigration(lg, opCtx, txnNumber, &canKillCursors);
    }
    if (canKillCursors) {
        _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
    }
}


void Session::onWriteOpCompletedOnPrimary(OperationContext* opCtx,
                                          TxnNumber txnNumber,
                                          std::vector<StmtId> stmtIdsWritten,
                                          const repl::OpTime& lastStmtIdWriteOpTime,
                                          Date_t lastStmtIdWriteDate) {
    invariant(opCtx->lockState()->inAWriteUnitOfWork());

    stdx::unique_lock<stdx::mutex> ul(_mutex);

    // Sanity check that we don't double-execute statements
    for (const auto stmtId : stmtIdsWritten) {
        const auto stmtOpTime = _checkStatementExecuted(ul, txnNumber, stmtId);
        if (stmtOpTime) {
            fassertOnRepeatedExecution(
                _sessionId, txnNumber, stmtId, *stmtOpTime, lastStmtIdWriteOpTime);
        }
    }

    const auto updateRequest =
        _makeUpdateRequest(ul, txnNumber, lastStmtIdWriteOpTime, lastStmtIdWriteDate);

    ul.unlock();

    repl::UnreplicatedWritesBlock doNotReplicateWrites(opCtx);

    updateSessionEntry(opCtx, updateRequest);
    _registerUpdateCacheOnCommit(
        opCtx, txnNumber, std::move(stmtIdsWritten), lastStmtIdWriteOpTime);
}

bool Session::onMigrateBeginOnPrimary(OperationContext* opCtx, TxnNumber txnNumber, StmtId stmtId) {
    beginOrContinueTxnOnMigration(opCtx, txnNumber);

    try {
        if (checkStatementExecuted(opCtx, txnNumber, stmtId)) {
            return false;
        }
    } catch (const DBException& ex) {
        // If the transaction chain was truncated on the recipient shard, then we
        // are most likely copying from a session that hasn't been touched on the
        // recipient shard for a very long time but could be recent on the donor.
        // We continue copying regardless to get the entire transaction from the donor.
        if (ex.code() != ErrorCodes::IncompleteTransactionHistory) {
            throw;
        }
        if (stmtId == kIncompleteHistoryStmtId) {
            return false;
        }
    }

    return true;
}

void Session::onMigrateCompletedOnPrimary(OperationContext* opCtx,
                                          TxnNumber txnNumber,
                                          std::vector<StmtId> stmtIdsWritten,
                                          const repl::OpTime& lastStmtIdWriteOpTime,
                                          Date_t lastStmtIdWriteDate) {
    invariant(opCtx->lockState()->inAWriteUnitOfWork());

    stdx::unique_lock<stdx::mutex> ul(_mutex);

    _checkValid(ul);
    _checkIsActiveTransaction(ul, txnNumber, false);

    const auto updateRequest =
        _makeUpdateRequest(ul, txnNumber, lastStmtIdWriteOpTime, lastStmtIdWriteDate);

    ul.unlock();

    repl::UnreplicatedWritesBlock doNotReplicateWrites(opCtx);

    updateSessionEntry(opCtx, updateRequest);
    _registerUpdateCacheOnCommit(
        opCtx, txnNumber, std::move(stmtIdsWritten), lastStmtIdWriteOpTime);
}

void Session::invalidate() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    _isValid = false;
    _numInvalidations++;

    _lastWrittenSessionRecord.reset();

    _activeTxnNumber = kUninitializedTxnNumber;
    _activeTxnCommittedStatements.clear();
    _hasIncompleteHistory = false;
}

repl::OpTime Session::getLastWriteOpTime(TxnNumber txnNumber) const {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    _checkValid(lg);
    _checkIsActiveTransaction(lg, txnNumber, false);

    if (!_lastWrittenSessionRecord || _lastWrittenSessionRecord->getTxnNum() != txnNumber)
        return {};

    return _lastWrittenSessionRecord->getLastWriteOpTime();
}

boost::optional<repl::OplogEntry> Session::checkStatementExecuted(OperationContext* opCtx,
                                                                  TxnNumber txnNumber,
                                                                  StmtId stmtId) const {
    const auto stmtTimestamp = [&] {
        stdx::lock_guard<stdx::mutex> lg(_mutex);
        return _checkStatementExecuted(lg, txnNumber, stmtId);
    }();

    if (!stmtTimestamp)
        return boost::none;

    TransactionHistoryIterator txnIter(*stmtTimestamp);
    while (txnIter.hasNext()) {
        const auto entry = txnIter.next(opCtx);
        invariant(entry.getStatementId());
        if (*entry.getStatementId() == stmtId)
            return entry;
    }

    MONGO_UNREACHABLE;
}

bool Session::checkStatementExecutedNoOplogEntryFetch(TxnNumber txnNumber, StmtId stmtId) const {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    return bool(_checkStatementExecuted(lg, txnNumber, stmtId));
}

void Session::_beginOrContinueTxn(WithLock wl,
                                  OperationContext* opCtx,
                                  TxnNumber txnNumber,
                                  boost::optional<bool> autocommit,
                                  boost::optional<bool> startTransaction,
                                  bool* canKillCursors) {

    // Check whether the session information needs to be refreshed from disk.
    _checkValid(wl);

    // Check if the given transaction number is valid for this session. The transaction number must
    // be >= the active transaction number.
    _checkTxnValid(wl, txnNumber);

    ON_BLOCK_EXIT([this, opCtx] {
        Session::TransactionState::get(opCtx).requiresIXReadUpgrade =
            _txnState == MultiDocumentTransactionState::kInProgress;
    });

    //
    // Continue an active transaction.
    //
    if (txnNumber == _activeTxnNumber) {

        // It is never valid to specify 'startTransaction' on an active transaction.
        uassert(ErrorCodes::ConflictingOperationInProgress,
                str::stream() << "Cannot specify 'startTransaction' on transaction " << txnNumber
                              << " since it is already in progress.",
                startTransaction == boost::none);

        // Continue a retryable write or a snapshot read.
        if (_txnState == MultiDocumentTransactionState::kNone ||
            _txnState == MultiDocumentTransactionState::kInSnapshotRead) {
            uassert(ErrorCodes::InvalidOptions,
                    "Cannot specify 'autocommit' on an operation not inside a multi-statement "
                    "transaction.",
                    autocommit == boost::none);
            return;
        }

        // Continue a multi-statement transaction. In this case, it is required that
        // autocommit=false be given as an argument on the request. Retryable writes and snapshot
        // reads will have _autocommit=true, so that is why we verify that _autocommit=false here.
        if (!_autocommit) {
            uassert(
                ErrorCodes::InvalidOptions,
                "Must specify autocommit=false on all operations of a multi-statement transaction.",
                autocommit == boost::optional<bool>(false));
            if (_txnState == MultiDocumentTransactionState::kInProgress && !_txnResourceStash) {
                // This indicates that the first command in the transaction failed but did not
                // implicitly abort the transaction. It is not safe to continue the transaction, in
                // particular because we have not saved the readConcern from the first statement of
                // the transaction.
                _abortTransaction(wl, opCtx, canKillCursors);
                uasserted(ErrorCodes::NoSuchTransaction,
                          str::stream() << "Transaction " << txnNumber << " has been aborted.");
            }
        }
        return;
    }

    //
    // Start a new transaction.
    //
    // At this point, the given transaction number must be > _activeTxnNumber. Existence of an
    // 'autocommit' field means we interpret this operation as part of a multi-document transaction.
    invariant(txnNumber > _activeTxnNumber);
    if (autocommit) {
        // Start a multi-document transaction.
        invariant(*autocommit == false);
        uassert(ErrorCodes::NoSuchTransaction,
                str::stream() << "Given transaction number " << txnNumber
                              << " does not match any in-progress transactions.",
                startTransaction != boost::none);

        // Check for FCV 4.0. The presence of an autocommit field distiguishes this as a
        // multi-statement transaction vs a retryable write.
        uassert(
            50773,
            str::stream() << "Transactions are only supported in featureCompatibilityVersion 4.0. "
                          << "See "
                          << feature_compatibility_version_documentation::kCompatibilityLink
                          << " for more information.",
            (serverGlobalParams.featureCompatibility.isVersionInitialized() &&
             serverGlobalParams.featureCompatibility.getVersion() ==
                 ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo40));

        _setActiveTxn(wl, opCtx, txnNumber, canKillCursors);
        _autocommit = false;
        _txnState = MultiDocumentTransactionState::kInProgress;
        _transactionExpireDate =
            Date_t::now() + stdx::chrono::seconds{transactionLifetimeLimitSeconds.load()};
    } else {
        // Execute a retryable write or snapshot read.
        invariant(startTransaction == boost::none);
        _setActiveTxn(wl, opCtx, txnNumber, canKillCursors);
        _autocommit = true;
        _txnState = MultiDocumentTransactionState::kNone;
    }

    invariant(_transactionOperations.empty());
}

void Session::_checkTxnValid(WithLock, TxnNumber txnNumber) const {
    uassert(ErrorCodes::TransactionTooOld,
            str::stream() << "Cannot start transaction " << txnNumber << " on session "
                          << getSessionId()
                          << " because a newer transaction "
                          << _activeTxnNumber
                          << " has already started.",
            txnNumber >= _activeTxnNumber);
}

Session::TxnResources::TxnResources(OperationContext* opCtx) {
    _ruState = opCtx->getWriteUnitOfWork()->release();
    opCtx->setWriteUnitOfWork(nullptr);

    _locker = opCtx->swapLockState(stdx::make_unique<DefaultLockerImpl>());
    _locker->releaseTicket();
    _locker->unsetThreadId();

    _recoveryUnit = std::unique_ptr<RecoveryUnit>(opCtx->releaseRecoveryUnit());
    opCtx->setRecoveryUnit(opCtx->getServiceContext()->getGlobalStorageEngine()->newRecoveryUnit(),
                           WriteUnitOfWork::RecoveryUnitState::kNotInUnitOfWork);

    _readConcernArgs = repl::ReadConcernArgs::get(opCtx);
}

Session::TxnResources::~TxnResources() {
    if (!_released && _recoveryUnit) {
        // This should only be reached when aborting a transaction that isn't active, i.e.
        // when starting a new transaction before completing an old one.  So we should
        // be at WUOW nesting level 1 (only the top level WriteUnitOfWork).
        _locker->endWriteUnitOfWork();
        invariant(!_locker->inAWriteUnitOfWork());
        _recoveryUnit->abortUnitOfWork();
    }
}

void Session::TxnResources::release(OperationContext* opCtx) {
    // Perform operations that can fail the release before marking the TxnResources as released.
    _locker->reacquireTicket(opCtx);

    invariant(!_released);
    _released = true;

    // We intentionally do not capture the return value of swapLockState(), which is just an empty
    // locker. At the end of the operation, if the transaction is not complete, we will stash the
    // operation context's locker and replace it with a new empty locker.
    invariant(opCtx->lockState()->getClientState() == Locker::ClientState::kInactive);
    opCtx->swapLockState(std::move(_locker));
    opCtx->lockState()->updateThreadIdToCurrentThread();

    opCtx->setRecoveryUnit(_recoveryUnit.release(),
                           WriteUnitOfWork::RecoveryUnitState::kNotInUnitOfWork);

    opCtx->setWriteUnitOfWork(WriteUnitOfWork::createForSnapshotResume(opCtx, _ruState));

    auto& readConcernArgs = repl::ReadConcernArgs::get(opCtx);
    readConcernArgs = _readConcernArgs;
}

void Session::stashTransactionResources(OperationContext* opCtx) {
    if (opCtx->getClient()->isInDirectClient()) {
        return;
    }

    invariant(opCtx->getTxnNumber());

    // We must lock the Client to change the Locker on the OperationContext and the Session mutex to
    // access Session state. We must lock the Client before the Session mutex, since the Client
    // effectively owns the Session. That is, a user might lock the Client to ensure it doesn't go
    // away, and then lock the Session owned by that client. We rely on the fact that we are not
    // using the DefaultLockerImpl to avoid deadlock.
    invariant(!isMMAPV1());
    stdx::lock_guard<Client> lk(*opCtx->getClient());
    stdx::unique_lock<stdx::mutex> lg(_mutex);

    // Always check '_activeTxnNumber', since it can be modified by migration, which does not
    // check out the session. We intentionally do not error if _txnState=kAborted, since we
    // expect this function to be called at the end of the 'abortTransaction' command.
    _checkIsActiveTransaction(lg, *opCtx->getTxnNumber(), false);

    if (_txnState != MultiDocumentTransactionState::kInProgress &&
        _txnState != MultiDocumentTransactionState::kInSnapshotRead) {
        // Not in a multi-document transaction or snapshot read: nothing to do.
        return;
    }

    if (_txnState == MultiDocumentTransactionState::kInSnapshotRead &&
        !_cursorExistsFunction(_sessionId, _activeTxnNumber)) {
        // The snapshot read is complete.
        invariant(opCtx->getWriteUnitOfWork());
        _commitTransaction(std::move(lg), opCtx);
    } else {
        invariant(!_txnResourceStash);
        _txnResourceStash = TxnResources(opCtx);
    }
}

void Session::unstashTransactionResources(OperationContext* opCtx, const std::string& cmdName) {
    if (opCtx->getClient()->isInDirectClient()) {
        return;
    }

    invariant(opCtx->getTxnNumber());

    // If the storage engine is mmapv1, it is not safe to lock both the Client and the Session
    // mutex. This is fine because mmapv1 does not support transactions.
    if (isMMAPV1()) {
        return;
    }

    bool snapshotPreallocated = false;
    {
        // We must lock the Client to change the Locker on the OperationContext and the Session
        // mutex to access Session state. We must lock the Client before the Session mutex, since
        // the Client effectively owns the Session. That is, a user might lock the Client to ensure
        // it doesn't go away, and then lock the Session owned by that client.
        stdx::lock_guard<Client> lk(*opCtx->getClient());
        stdx::lock_guard<stdx::mutex> lg(_mutex);

        // Always check '_activeTxnNumber' and '_txnState', since they can be modified by session
        // kill and migration, which do not check out the session.
        _checkIsActiveTransaction(lg, *opCtx->getTxnNumber(), false);
        // Throw NoSuchTransaction error instead of TransactionAborted error since this is the entry
        // point of transaction execution.
        uassert(ErrorCodes::NoSuchTransaction,
                str::stream() << "Transaction " << *opCtx->getTxnNumber() << " has been aborted.",
                _txnState != MultiDocumentTransactionState::kAborted);

        // Cannot change committed transaction but allow retrying commitTransaction command.
        uassert(ErrorCodes::TransactionCommitted,
                str::stream() << "Transaction " << *opCtx->getTxnNumber() << " has been committed.",
                cmdName == "commitTransaction" ||
                    _txnState != MultiDocumentTransactionState::kCommitted);

        if (_txnResourceStash) {
            // Transaction resources already exist for this transaction.  Transfer them from the
            // stash to the operation context.
            invariant(_txnState != MultiDocumentTransactionState::kNone);

            auto& readConcernArgs = repl::ReadConcernArgs::get(opCtx);
            uassert(ErrorCodes::InvalidOptions,
                    "Only the first command in a transaction may specify a readConcern",
                    readConcernArgs.isEmpty());

            _txnResourceStash->release(opCtx);
            _txnResourceStash = boost::none;
        } else {
            // Stashed transaction resources do not exist for this transaction.  If this is a
            // snapshot read or a multi-document transaction, set up the transaction resources on
            // the opCtx.
            auto readConcernArgs = repl::ReadConcernArgs::get(opCtx);
            if (readConcernArgs.getLevel() == repl::ReadConcernLevel::kSnapshotReadConcern ||
                _txnState == MultiDocumentTransactionState::kInProgress) {
                opCtx->setWriteUnitOfWork(std::make_unique<WriteUnitOfWork>(opCtx));

                // Storage engine transactions may be started in a lazy manner. By explicitly
                // starting here we ensure that a point-in-time snapshot is established during the
                // first operation of a transaction.
                opCtx->recoveryUnit()->preallocateSnapshot();
                snapshotPreallocated = true;

                if (_txnState != MultiDocumentTransactionState::kInProgress) {
                    _txnState = MultiDocumentTransactionState::kInSnapshotRead;
                }
            }
        }
    }

    if (snapshotPreallocated) {
        // The Client lock must not be held when executing this failpoint as it will block currentOp
        // execution.
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterPreallocateSnapshot);
    }
}

void Session::abortArbitraryTransaction(OperationContext* opCtx, bool shouldKillClientCursors) {
    TxnNumber txnNumberAtStart;
    bool canKillCursors = false;
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        txnNumberAtStart = _activeTxnNumber;
        _abortArbitraryTransaction(lock, opCtx, &canKillCursors);
    }

    if (shouldKillClientCursors && canKillCursors) {
        _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
    }
}

void Session::abortArbitraryTransactionIfExpired(OperationContext* opCtx) {
    TxnNumber txnNumberAtStart;
    bool canKillCursors = false;
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        if (!_transactionExpireDate || _transactionExpireDate >= Date_t::now()) {
            return;
        }
        txnNumberAtStart = _activeTxnNumber;
        _abortArbitraryTransaction(lock, opCtx, &canKillCursors);
    }

    if (canKillCursors) {
        _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
    }
}

void Session::_abortArbitraryTransaction(WithLock lock,
                                         OperationContext* opCtx,
                                         bool* canKillCursors) {
    if (_txnState != MultiDocumentTransactionState::kInProgress &&
        _txnState != MultiDocumentTransactionState::kInSnapshotRead) {
        return;
    }

    _abortTransaction(lock, opCtx, canKillCursors);
}

void Session::abortActiveTransaction(OperationContext* opCtx) {
    TxnNumber txnNumberAtStart;
    bool canKillCursors = false;
    {
        stdx::unique_lock<Client> clientLock(*opCtx->getClient());
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        txnNumberAtStart = _activeTxnNumber;

        if (_txnState != MultiDocumentTransactionState::kInProgress &&
            _txnState != MultiDocumentTransactionState::kInSnapshotRead) {
            return;
        }

        _abortTransaction(lock, opCtx, &canKillCursors);

        // Abort the WUOW. We should be able to abort empty transactions that don't have WUOW.
        if (opCtx->getWriteUnitOfWork()) {
            opCtx->setWriteUnitOfWork(nullptr);
        }
    }
    if (canKillCursors) {
        _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
    }
}

void Session::killTransactionCursors(OperationContext* opCtx) {
    TxnNumber txnNumberAtStart;
    {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        txnNumberAtStart = _activeTxnNumber;
    }

    _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
}

void Session::_abortTransaction(WithLock wl, OperationContext* opCtx, bool* canKillCursors) {
    invariant(canKillCursors);
    // TODO SERVER-33432 Disallow aborting committed transaction after we implement implicit abort.
    // A transaction in kCommitting state will either commit or abort for storage-layer reasons; it
    // is too late to abort externally.
    if (_txnState == MultiDocumentTransactionState::kCommitting ||
        _txnState == MultiDocumentTransactionState::kCommitted) {
        return;
    }
    _txnResourceStash = boost::none;
    _transactionOperationBytes = 0;
    _transactionOperations.clear();
    _txnState = MultiDocumentTransactionState::kAborted;
    *canKillCursors = true;
}

void Session::_beginOrContinueTxnOnMigration(WithLock wl,
                                             OperationContext* opCtx,
                                             TxnNumber txnNumber,
                                             bool* canKillCursors) {
    _checkValid(wl);
    _checkTxnValid(wl, txnNumber);

    // Check for continuing an existing transaction
    if (txnNumber == _activeTxnNumber)
        return;

    _setActiveTxn(wl, opCtx, txnNumber, canKillCursors);
}

void Session::_setActiveTxn(WithLock wl,
                            OperationContext* opCtx,
                            TxnNumber txnNumber,
                            bool* canKillCursors) {
    // Abort the existing transaction if it's not committed or aborted.
    if (_txnState == MultiDocumentTransactionState::kInProgress ||
        _txnState == MultiDocumentTransactionState::kInSnapshotRead) {
        _abortTransaction(wl, opCtx, canKillCursors);
    }
    _activeTxnNumber = txnNumber;
    _activeTxnCommittedStatements.clear();
    _hasIncompleteHistory = false;
    _txnState = MultiDocumentTransactionState::kNone;
}

void Session::addTransactionOperation(OperationContext* opCtx,
                                      const repl::ReplOperation& operation) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    // Always check '_activeTxnNumber' and '_txnState', since they can be modified by session kill
    // and migration, which do not check out the session.
    _checkIsActiveTransaction(lk, *opCtx->getTxnNumber(), true);

    invariant(_txnState == MultiDocumentTransactionState::kInProgress);
    invariant(!_autocommit && _activeTxnNumber != kUninitializedTxnNumber);
    invariant(opCtx->lockState()->inAWriteUnitOfWork());
    _transactionOperations.push_back(operation);
    _transactionOperationBytes += repl::OplogEntry::getReplOperationSize(operation);
    // _transactionOperationBytes is based on the in-memory size of the operation.  With overhead,
    // we expect the BSON size of the operation to be larger, so it's possible to make a transaction
    // just a bit too large and have it fail only in the commit.  It's still useful to fail early
    // when possible (e.g. to avoid exhausting server memory).
    uassert(ErrorCodes::TransactionTooLarge,
            str::stream() << "Total size of all transaction operations must be less than "
                          << BSONObjMaxInternalSize
                          << ". Actual size is "
                          << _transactionOperationBytes,
            _transactionOperationBytes <= BSONObjMaxInternalSize);
}

std::vector<repl::ReplOperation> Session::endTransactionAndRetrieveOperations(
    OperationContext* opCtx) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    // Always check '_activeTxnNumber' and '_txnState', since they can be modified by session kill
    // and migration, which do not check out the session.
    _checkIsActiveTransaction(lk, *opCtx->getTxnNumber(), true);

    invariant(!_autocommit);
    _transactionOperationBytes = 0;
    return std::move(_transactionOperations);
}

void Session::commitTransaction(OperationContext* opCtx) {
    TxnNumber txnNumberAtStart;
    {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        txnNumberAtStart = _activeTxnNumber;

        // Always check '_activeTxnNumber' and '_txnState', since they can be modified by
        // session kill and migration, which do not check out the session.
        _checkIsActiveTransaction(lk, *opCtx->getTxnNumber(), true);

        invariant(_txnState != MultiDocumentTransactionState::kCommitted);
        _commitTransaction(std::move(lk), opCtx);
    }
    _killTransactionCursors(opCtx, _sessionId, txnNumberAtStart);
}

void Session::_commitTransaction(stdx::unique_lock<stdx::mutex> lk, OperationContext* opCtx) {
    invariant(_txnState == MultiDocumentTransactionState::kInProgress ||
              _txnState == MultiDocumentTransactionState::kInSnapshotRead);
    const bool isMultiDocumentTransaction = _txnState == MultiDocumentTransactionState::kInProgress;
    if (isMultiDocumentTransaction) {
        // We need to unlock the session to run the opObserver onTransactionCommit, which calls back
        // into the session.
        lk.unlock();
        auto opObserver = opCtx->getServiceContext()->getOpObserver();
        invariant(opObserver);
        opObserver->onTransactionCommit(opCtx);
        lk.lock();
        // It's possible some other thread aborted the transaction (e.g. through killSession) while
        // the opObserver was running.  If that happened, the commit should be reported as failed.
        uassert(ErrorCodes::NoSuchTransaction,
                str::stream() << "Transaction " << opCtx->getTxnNumber()
                              << " aborted while attempting to commit",
                _txnState == MultiDocumentTransactionState::kInProgress &&
                    _activeTxnNumber == opCtx->getTxnNumber());
    }
    _txnState = MultiDocumentTransactionState::kCommitting;
    bool committed = false;
    ON_BLOCK_EXIT([this, &committed, opCtx]() {
        // If we're still "committing", the recovery unit failed to commit, and the lock is not
        // held.  We can't safely use _txnState here, as it is protected by the lock.
        if (!committed) {
            stdx::lock_guard<stdx::mutex> lk(_mutex);
            opCtx->setWriteUnitOfWork(nullptr);
            // Make sure the transaction didn't change because of chunk migration.
            if (opCtx->getTxnNumber() == _activeTxnNumber) {
                _txnState = MultiDocumentTransactionState::kAborted;
            }
        }
        _commitcv.notify_all();
    });
    lk.unlock();
    opCtx->getWriteUnitOfWork()->commit();
    opCtx->setWriteUnitOfWork(nullptr);
    committed = true;
    lk.lock();
    _txnState = MultiDocumentTransactionState::kCommitted;
}

BSONObj Session::reportStashedState() const {
    BSONObjBuilder builder;
    reportStashedState(&builder);
    return builder.obj();
}

void Session::reportStashedState(BSONObjBuilder* builder) const {
    stdx::lock_guard<stdx::mutex> ls(_mutex);

    if (_txnResourceStash && _txnResourceStash->locker()) {
        if (auto lockerInfo = _txnResourceStash->locker()->getLockerInfo()) {
            invariant(_activeTxnNumber != kUninitializedTxnNumber);
            builder->append("host", getHostNameCachedAndPort());
            builder->append("desc", "inactive transaction");
            {
                BSONObjBuilder lsid(builder->subobjStart("lsid"));
                getSessionId().serialize(&lsid);
            }
            builder->append("txnNumber", _activeTxnNumber);
            builder->append("waitingForLock", false);
            builder->append("active", false);
            fillLockerInfo(*lockerInfo, *builder);
        }
    }
}

// TODO SERVER-34395: Remove opCtx from this interface once no longer required.
void Session::_killTransactionCursors(OperationContext* opCtx,
                                      LogicalSessionId lsid,
                                      TxnNumber txnNumber) {
    invariant(_cursorKillFunction);

    if (!opCtx) {
        return;
    }

    _cursorKillFunction(opCtx, lsid, txnNumber);
}

void Session::_checkValid(WithLock) const {
    uassert(ErrorCodes::ConflictingOperationInProgress,
            str::stream() << "Session " << getSessionId()
                          << " was concurrently modified and the operation must be retried.",
            _isValid);
}

void Session::_checkIsActiveTransaction(WithLock, TxnNumber txnNumber, bool checkAbort) const {
    uassert(ErrorCodes::ConflictingOperationInProgress,
            str::stream() << "Cannot perform operations on transaction " << txnNumber
                          << " on session "
                          << getSessionId()
                          << " because a different transaction "
                          << _activeTxnNumber
                          << " is now active.",
            txnNumber == _activeTxnNumber);

    uassert(ErrorCodes::NoSuchTransaction,
            str::stream() << "Transaction " << txnNumber << " has been aborted.",
            !checkAbort || _txnState != MultiDocumentTransactionState::kAborted);
}

boost::optional<repl::OpTime> Session::_checkStatementExecuted(WithLock wl,
                                                               TxnNumber txnNumber,
                                                               StmtId stmtId) const {
    _checkValid(wl);
    _checkIsActiveTransaction(wl, txnNumber, false);
    // Retries are not detected for multi-document transactions.
    if (_txnState == MultiDocumentTransactionState::kInProgress)
        return boost::none;

    const auto it = _activeTxnCommittedStatements.find(stmtId);
    if (it == _activeTxnCommittedStatements.end()) {
        uassert(ErrorCodes::IncompleteTransactionHistory,
                str::stream() << "Incomplete history detected for transaction " << txnNumber
                              << " on session "
                              << _sessionId.toBSON(),
                !_hasIncompleteHistory);

        return boost::none;
    }

    invariant(_lastWrittenSessionRecord);
    invariant(_lastWrittenSessionRecord->getTxnNum() == txnNumber);

    return it->second;
}

UpdateRequest Session::_makeUpdateRequest(WithLock,
                                          TxnNumber newTxnNumber,
                                          const repl::OpTime& newLastWriteOpTime,
                                          Date_t newLastWriteDate) const {
    UpdateRequest updateRequest(NamespaceString::kSessionTransactionsTableNamespace);

    const auto updateBSON = [&] {
        SessionTxnRecord newTxnRecord;
        newTxnRecord.setSessionId(_sessionId);
        newTxnRecord.setTxnNum(newTxnNumber);
        newTxnRecord.setLastWriteOpTime(newLastWriteOpTime);
        newTxnRecord.setLastWriteDate(newLastWriteDate);
        return newTxnRecord.toBSON();
    }();
    updateRequest.setUpdates(updateBSON);
    updateRequest.setQuery(BSON(SessionTxnRecord::kSessionIdFieldName << _sessionId.toBSON()));
    updateRequest.setUpsert(true);

    return updateRequest;
}

void Session::_registerUpdateCacheOnCommit(OperationContext* opCtx,
                                           TxnNumber newTxnNumber,
                                           std::vector<StmtId> stmtIdsWritten,
                                           const repl::OpTime& lastStmtIdWriteOpTime) {
    opCtx->recoveryUnit()->onCommit(
        [ this, newTxnNumber, stmtIdsWritten = std::move(stmtIdsWritten), lastStmtIdWriteOpTime ] {
            RetryableWritesStats::get(getGlobalServiceContext())
                ->incrementTransactionsCollectionWriteCount();

            stdx::lock_guard<stdx::mutex> lg(_mutex);

            if (!_isValid)
                return;

            // The cache of the last written record must always be advanced after a write so that
            // subsequent writes have the correct point to start from.
            if (!_lastWrittenSessionRecord) {
                _lastWrittenSessionRecord.emplace();

                _lastWrittenSessionRecord->setSessionId(_sessionId);
                _lastWrittenSessionRecord->setTxnNum(newTxnNumber);
                _lastWrittenSessionRecord->setLastWriteOpTime(lastStmtIdWriteOpTime);
            } else {
                if (newTxnNumber > _lastWrittenSessionRecord->getTxnNum())
                    _lastWrittenSessionRecord->setTxnNum(newTxnNumber);

                if (lastStmtIdWriteOpTime > _lastWrittenSessionRecord->getLastWriteOpTime())
                    _lastWrittenSessionRecord->setLastWriteOpTime(lastStmtIdWriteOpTime);
            }

            if (newTxnNumber > _activeTxnNumber) {
                // This call is necessary in order to advance the txn number and reset the cached
                // state in the case where just before the storage transaction commits, the cache
                // entry gets invalidated and immediately refreshed while there were no writes for
                // newTxnNumber yet. In this case _activeTxnNumber will be less than newTxnNumber
                // and we will fail to update the cache even though the write was successful.
                OperationContext* opCtx = nullptr;
                bool ignoredCanKillCursors = false;
                _beginOrContinueTxn(
                    lg, opCtx, newTxnNumber, boost::none, boost::none, &ignoredCanKillCursors);
            }

            if (newTxnNumber == _activeTxnNumber) {
                for (const auto stmtId : stmtIdsWritten) {
                    if (stmtId == kIncompleteHistoryStmtId) {
                        _hasIncompleteHistory = true;
                        continue;
                    }

                    const auto insertRes =
                        _activeTxnCommittedStatements.emplace(stmtId, lastStmtIdWriteOpTime);
                    if (!insertRes.second) {
                        const auto& existingOpTime = insertRes.first->second;
                        fassertOnRepeatedExecution(_sessionId,
                                                   newTxnNumber,
                                                   stmtId,
                                                   existingOpTime,
                                                   lastStmtIdWriteOpTime);
                    }
                }
            }
        });

    MONGO_FAIL_POINT_BLOCK(onPrimaryTransactionalWrite, customArgs) {
        const auto& data = customArgs.getData();

        const auto closeConnectionElem = data["closeConnection"];
        if (closeConnectionElem.eoo() || closeConnectionElem.Bool()) {
            opCtx->getClient()->session()->end();
        }

        const auto failBeforeCommitExceptionElem = data["failBeforeCommitExceptionCode"];
        if (!failBeforeCommitExceptionElem.eoo()) {
            const auto failureCode = ErrorCodes::Error(int(failBeforeCommitExceptionElem.Number()));
            uasserted(failureCode,
                      str::stream() << "Failing write for " << _sessionId << ":" << newTxnNumber
                                    << " due to failpoint. The write must not be reflected.");
        }
    }
}

std::vector<repl::OplogEntry> Session::addOpsForReplicatingTxnTable(
    const std::vector<repl::OplogEntry>& ops) {
    std::vector<repl::OplogEntry> newOps;

    for (auto&& op : ops) {
        newOps.push_back(op);

        if (auto updateTxnTableOp = createMatchingTransactionTableUpdate(op)) {
            newOps.push_back(*updateTxnTableOp);
        }
    }

    return newOps;
}

boost::optional<repl::OplogEntry> Session::createMatchingTransactionTableUpdate(
    const repl::OplogEntry& entry) {
    auto sessionInfo = entry.getOperationSessionInfo();
    if (!sessionInfo.getTxnNumber()) {
        return boost::none;
    }

    invariant(sessionInfo.getSessionId());
    invariant(entry.getWallClockTime());

    const auto updateBSON = [&] {
        SessionTxnRecord newTxnRecord;
        newTxnRecord.setSessionId(*sessionInfo.getSessionId());
        newTxnRecord.setTxnNum(*sessionInfo.getTxnNumber());
        newTxnRecord.setLastWriteOpTime(entry.getOpTime());
        newTxnRecord.setLastWriteDate(*entry.getWallClockTime());
        return newTxnRecord.toBSON();
    }();

    return repl::OplogEntry(
        entry.getOpTime(),
        0,  // hash
        repl::OpTypeEnum::kUpdate,
        NamespaceString::kSessionTransactionsTableNamespace,
        boost::none,  // uuid
        false,        // fromMigrate
        repl::OplogEntry::kOplogVersion,
        updateBSON,
        BSON(SessionTxnRecord::kSessionIdFieldName << sessionInfo.getSessionId()->toBSON()),
        {},    // sessionInfo
        true,  // upsert
        *entry.getWallClockTime(),
        boost::none,  // statementId
        boost::none,  // prevWriteOpTime
        boost::none,  // preImangeOpTime
        boost::none   // postImageOpTime
        );
}

}  // namespace mongo
