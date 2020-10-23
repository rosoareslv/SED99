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

#include <string>

#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/storage/mobile/mobile_options.h"
#include "mongo/db/storage/mobile/mobile_recovery_unit.h"
#include "mongo/db/storage/mobile/mobile_sqlite_statement.h"
#include "mongo/db/storage/mobile/mobile_util.h"
#include "mongo/db/storage/sorted_data_interface.h"
#include "mongo/util/log.h"

#define RECOVERY_UNIT_TRACE() LOG(MOBILE_TRACE_LEVEL) << "MobileSE: RecoveryUnit ID:" << _id << " "

namespace mongo {

AtomicWord<long long> MobileRecoveryUnit::_nextID(0);

MobileRecoveryUnit::MobileRecoveryUnit(MobileSessionPool* sessionPool)
    : _isReadOnly(true), _sessionPool(sessionPool) {
    // Increment the global instance count and assign this instance an id.
    _id = _nextID.addAndFetch(1);

    RECOVERY_UNIT_TRACE() << "Created.";
}

MobileRecoveryUnit::~MobileRecoveryUnit() {
    invariant(!_inUnitOfWork(), toString(_getState()));
    _abort();
    RECOVERY_UNIT_TRACE() << "Destroyed.";
}

void MobileRecoveryUnit::_commit() {
    if (_session && _isActive()) {
        _txnClose(true);
    }
    _setState(State::kCommitting);
    commitRegisteredChanges(boost::none);
    _setState(State::kInactive);
}

void MobileRecoveryUnit::_abort() {
    if (_session && _isActive()) {
        _txnClose(false);
    }
    _setState(State::kAborting);
    abortRegisteredChanges();

    invariant(!_isActive(), toString(_getState()));
    _setState(State::kInactive);
}

void MobileRecoveryUnit::beginUnitOfWork(OperationContext* opCtx) {
    invariant(!_inUnitOfWork(), toString(_getState()));

    RECOVERY_UNIT_TRACE() << "Unit of work Active.";

    if (_isActive()) {
        // Confirm a write transaction is not running
        invariant(_isReadOnly);

        // Rollback read transaction running outside wuow
        _txnClose(false);
    }
    _setState(State::kInactiveInUnitOfWork);
    _txnOpen(opCtx, false);
}

void MobileRecoveryUnit::commitUnitOfWork() {
    invariant(_inUnitOfWork(), toString(_getState()));

    RECOVERY_UNIT_TRACE() << "Unit of work commited, marked inactive.";

    _commit();
}

void MobileRecoveryUnit::abortUnitOfWork() {
    invariant(_inUnitOfWork(), toString(_getState()));

    RECOVERY_UNIT_TRACE() << "Unit of work aborted, marked inactive.";

    _abort();
}

bool MobileRecoveryUnit::waitUntilDurable(OperationContext* opCtx) {
    // This is going to be slow as we're taking a global X lock and doing a full checkpoint. This
    // should not be needed to do on Android or iOS if we are on WAL and synchronous=NORMAL which
    // are our default settings. The system will make sure any non-flushed writes will not be lost
    // before going down but our powercycle test bench require it. Therefore make sure embedded does
    // not call this (by disabling writeConcern j:true) but allow it when this is used inside
    // mongod.
    if (_sessionPool->getOptions().durabilityLevel < 2) {
        _ensureSession(opCtx);
        RECOVERY_UNIT_TRACE() << "waitUntilDurable called, attempting to perform a checkpoint";
        int framesInWAL = 0;
        int checkpointedFrames = 0;
        int ret;
        {
            Lock::GlobalLock lk(opCtx, MODE_X);
            // Use FULL mode to guarantee durability
            ret = sqlite3_wal_checkpoint_v2(_session.get()->getSession(),
                                            nullptr,
                                            SQLITE_CHECKPOINT_FULL,
                                            &framesInWAL,
                                            &checkpointedFrames);
        }
        embedded::checkStatus(ret, SQLITE_OK, "sqlite3_wal_checkpoint_v2");
        fassert(51164,
                framesInWAL != -1 && checkpointedFrames != -1 && framesInWAL == checkpointedFrames);
        RECOVERY_UNIT_TRACE() << "Checkpointed " << checkpointedFrames << " of the " << framesInWAL
                              << " total frames in the WAL";
    } else {
        RECOVERY_UNIT_TRACE() << "No checkpoint attempted -- in full synchronous mode";
    }

    return true;
}

void MobileRecoveryUnit::abandonSnapshot() {
    invariant(!_inUnitOfWork(), toString(_getState()));
    if (_isActive()) {
        // We can't be in a WriteUnitOfWork, so it is safe to rollback.
        _txnClose(false);
    }
    _setState(State::kInactive);
}

MobileSession* MobileRecoveryUnit::getSession(OperationContext* opCtx, bool readOnly) {
    RECOVERY_UNIT_TRACE() << "getSession called with readOnly:" << (readOnly ? "TRUE" : "FALSE");

    invariant(_inUnitOfWork() || readOnly);
    if (!_isActive()) {
        _txnOpen(opCtx, readOnly);
    }

    return _session.get();
}

MobileSession* MobileRecoveryUnit::getSessionNoTxn(OperationContext* opCtx) {
    _ensureSession(opCtx);
    return _session.get();
}

void MobileRecoveryUnit::assertInActiveTxn() const {
    fassert(37050, _isActive());
}

void MobileRecoveryUnit::_ensureSession(OperationContext* opCtx) {
    RECOVERY_UNIT_TRACE() << "Creating new session:" << (_session ? "NO" : "YES");
    if (!_session) {
        _session = _sessionPool->getSession(opCtx);
    }
}

void MobileRecoveryUnit::_txnOpen(OperationContext* opCtx, bool readOnly) {
    invariant(!_isActive(), toString(_getState()));
    RECOVERY_UNIT_TRACE() << "_txnOpen called with readOnly:" << (readOnly ? "TRUE" : "FALSE");
    _ensureSession(opCtx);

    /*
     * Starting a transaction with the "BEGIN" statement doesn't take an immediate lock.
     * SQLite defers taking any locks until the database is first accessed. This creates the
     * possibility of having multiple transactions opened in parallel. All sessions except the
     * first to request the access get a database locked error.
     * However, "BEGIN IMMEDIATE" forces SQLite to take a lock immediately. If another session
     * tries to create a transaction in parallel, it receives a busy error and then retries.
     * Reads outside these explicit transactions proceed unaffected.
     */

    // Check for correct locking at higher levels
    if (readOnly) {
        // Confirm that this reader has taken a shared lock
        if (!opCtx->lockState()->isLockHeldForMode(resourceIdGlobal, MODE_S)) {
            opCtx->lockState()->dump();
            invariant(!"Reading without a shared lock");
        }
        SqliteStatement::execQuery(_session.get(), "BEGIN");
    } else {
        // Single writer allowed at a time, confirm a global write lock has been taken
        if (!opCtx->lockState()->isLockHeldForMode(resourceIdGlobal, MODE_X)) {
            opCtx->lockState()->dump();
            invariant(!"Writing without an exclusive lock");
        }
        SqliteStatement::execQuery(_session.get(), "BEGIN EXCLUSIVE");
    }

    _isReadOnly = readOnly;
    _setState(_inUnitOfWork() ? State::kActive : State::kActiveNotInUnitOfWork);
}

void MobileRecoveryUnit::_txnClose(bool commit) {
    invariant(_isActive(), toString(_getState()));
    RECOVERY_UNIT_TRACE() << "_txnClose called with " << (commit ? "commit " : "rollback ");

    if (commit) {
        SqliteStatement::execQuery(_session.get(), "COMMIT");
    } else {
        SqliteStatement::execQuery(_session.get(), "ROLLBACK");
    }

    _isReadOnly = true;  // I don't suppose we need this, but no harm in doing so
}

void MobileRecoveryUnit::enqueueFailedDrop(std::string& dropQuery) {
    _sessionPool->failedDropsQueue.enqueueOp(dropQuery);
}
}  // namespace mongo
