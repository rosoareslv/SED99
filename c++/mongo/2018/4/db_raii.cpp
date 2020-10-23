/**
 *    Copyright (C) 2014 MongoDB Inc.
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

#include "mongo/db/db_raii.h"

#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/concurrency/locker.h"
#include "mongo/db/curop.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/session_catalog.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

const boost::optional<int> kDoNotChangeProfilingLevel = boost::none;

}  // namespace

// If true, do not take the PBWM lock in AutoGetCollectionForRead on secondaries during batch
// application.
MONGO_EXPORT_SERVER_PARAMETER(allowSecondaryReadsDuringBatchApplication, bool, true);

AutoStatsTracker::AutoStatsTracker(OperationContext* opCtx,
                                   const NamespaceString& nss,
                                   Top::LockType lockType,
                                   boost::optional<int> dbProfilingLevel,
                                   Date_t deadline)
    : _opCtx(opCtx), _lockType(lockType) {
    if (!dbProfilingLevel) {
        // No profiling level was determined, attempt to read the profiling level from the Database
        // object.
        AutoGetDb autoDb(_opCtx, nss.db(), MODE_IS, deadline);
        if (autoDb.getDb()) {
            dbProfilingLevel = autoDb.getDb()->getProfilingLevel();
        }
    }

    stdx::lock_guard<Client> clientLock(*_opCtx->getClient());
    CurOp::get(_opCtx)->enter_inlock(nss.ns().c_str(), dbProfilingLevel);
}

AutoStatsTracker::~AutoStatsTracker() {
    auto curOp = CurOp::get(_opCtx);
    Top::get(_opCtx->getServiceContext())
        .record(_opCtx,
                curOp->getNS(),
                curOp->getLogicalOp(),
                _lockType,
                durationCount<Microseconds>(curOp->elapsedTimeExcludingPauses()),
                curOp->isCommand(),
                curOp->getReadWriteType());
}

AutoGetCollectionForRead::AutoGetCollectionForRead(OperationContext* opCtx,
                                                   const NamespaceStringOrUUID& nsOrUUID,
                                                   AutoGetCollection::ViewMode viewMode,
                                                   Date_t deadline) {
    // Don't take the ParallelBatchWriterMode lock when the server parameter is set and our
    // storage engine supports snapshot reads.
    if (allowSecondaryReadsDuringBatchApplication.load() &&
        opCtx->getServiceContext()->getGlobalStorageEngine()->supportsReadConcernSnapshot()) {
        _shouldNotConflictWithSecondaryBatchApplicationBlock.emplace(opCtx->lockState());
    }
    const auto collectionLockMode = getLockModeForQuery(opCtx);
    _autoColl.emplace(opCtx, nsOrUUID, collectionLockMode, viewMode, deadline);

    repl::ReplicationCoordinator* const replCoord = repl::ReplicationCoordinator::get(opCtx);
    const auto readConcernLevel = opCtx->recoveryUnit()->getReadConcernLevel();

    // If the collection doesn't exist or disappears after releasing locks and waiting, there is no
    // need to check for pending catalog changes.
    while (auto coll = _autoColl->getCollection()) {

        // During batch application on secondaries, there is a potential to read inconsistent states
        // that would normally be protected by the PBWM lock. In order to serve secondary reads
        // during this period, we default to not acquiring the lock (by setting
        // _shouldNotConflictWithSecondaryBatchApplicationBlock). On primaries, we always read at a
        // consistent time, so not taking the PBWM lock is not a problem. On secondaries, we have to
        // guarantee we read at a consistent state, so we must read at the last applied timestamp,
        // which is set after each complete batch.
        //
        // If an attempt to read at the last applied timestamp is unsuccessful because there are
        // pending catalog changes that occur after the last applied timestamp, we release our locks
        // and try again with the PBWM lock (by unsetting
        // _shouldNotConflictWithSecondaryBatchApplicationBlock).

        const NamespaceString& nss = coll->ns();

        // Read at the last applied timestamp if we don't have the PBWM lock and correct conditions
        // are met.
        bool readAtLastAppliedTimestamp =
            _shouldReadAtLastAppliedTimestamp(opCtx, nss, readConcernLevel);

        opCtx->recoveryUnit()->setShouldReadAtLastAppliedTimestamp(readAtLastAppliedTimestamp);

        // This timestamp could be earlier than the timestamp seen when the transaction is opened
        // because it is set asynchonously. This is not problematic because holding the collection
        // lock guarantees no metadata changes will occur in that time.
        auto lastAppliedTimestamp = readAtLastAppliedTimestamp
            ? boost::optional<Timestamp>(replCoord->getMyLastAppliedOpTime().getTimestamp())
            : boost::none;

        // Return if there are no conflicting catalog changes on the collection.
        auto minSnapshot = coll->getMinimumVisibleSnapshot();
        if (!_conflictingCatalogChanges(
                opCtx, readConcernLevel, minSnapshot, lastAppliedTimestamp)) {
            return;
        }

        invariant(lastAppliedTimestamp ||
                  readConcernLevel == repl::ReadConcernLevel::kMajorityReadConcern);

        // Yield locks in order to do the blocking call below.
        // This should only be called if we are doing a snapshot read at the last applied time or
        // majority commit point.
        _autoColl = boost::none;

        // If there are pending catalog changes, we should conflict with any in-progress batches (by
        // taking the PBWM lock) and choose not to read from the last applied timestamp by unsetting
        // _shouldNotConflictWithSecondaryBatchApplicationBlock. Index builds on secondaries can
        // complete at timestamps later than the lastAppliedTimestamp during initial sync
        // (SERVER-34343). After initial sync finishes, if we waited instead of retrying, readers
        // would block indefinitely waiting for the lastAppliedTimestamp to move forward. Instead we
        // force the reader take the PBWM lock and retry.
        if (lastAppliedTimestamp) {
            LOG(2) << "Tried reading at last-applied time: " << *lastAppliedTimestamp
                   << " on nss: " << nss.ns() << ", but future catalog changes are pending at time "
                   << *minSnapshot << ". Trying again without reading at last-applied time.";
            _shouldNotConflictWithSecondaryBatchApplicationBlock = boost::none;
        }

        if (readConcernLevel == repl::ReadConcernLevel::kMajorityReadConcern) {
            replCoord->waitUntilSnapshotCommitted(opCtx, *minSnapshot);
            uassertStatusOK(opCtx->recoveryUnit()->obtainMajorityCommittedSnapshot());
        }

        {
            stdx::lock_guard<Client> lk(*opCtx->getClient());
            CurOp::get(opCtx)->yielded();
        }

        _autoColl.emplace(opCtx, nsOrUUID, collectionLockMode, viewMode, deadline);
    }
}

bool AutoGetCollectionForRead::_shouldReadAtLastAppliedTimestamp(
    OperationContext* opCtx,
    const NamespaceString& nss,
    repl::ReadConcernLevel readConcernLevel) const {

    // If external circumstances prevent us from reading at lastApplied, disallow it.
    if (!_shouldNotConflictWithSecondaryBatchApplicationBlock) {
        return false;
    }

    // Majority and snapshot readConcern levels should not read from lastApplied; these read
    // concerns already have a designated timestamp to read from.
    if (readConcernLevel != repl::ReadConcernLevel::kLocalReadConcern &&
        readConcernLevel != repl::ReadConcernLevel::kAvailableReadConcern) {
        return false;
    }

    // If we are in a replication state (like secondary or primary catch-up) where we are not
    // accepting writes, we should read at lastApplied. If this node can accept writes, then no
    // conflicting replication batches are being applied and we can read from the default snapshot.
    if (repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesForDatabase(opCtx, "admin")) {
        return false;
    }

    // Non-replicated collections do not need to read at lastApplied, as those collections are not
    // written by the replication system.  However, the oplog is special, as it *is* written by the
    // replication system.
    if (!nss.isReplicated() && !nss.isOplog()) {
        return false;
    }

    // Not being able to read from the lastApplied with non-network clients is tracked by
    // SERVER-34440.  After SERVER-34440 is fixed, this code can be removed.
    if (!opCtx->getClient()->isFromUserConnection()) {
        return false;
    }

    return true;
}

bool AutoGetCollectionForRead::_conflictingCatalogChanges(
    OperationContext* opCtx,
    repl::ReadConcernLevel readConcernLevel,
    boost::optional<Timestamp> minSnapshot,
    boost::optional<Timestamp> lastAppliedTimestamp) const {
    // This is the timestamp of the most recent catalog changes to this collection. If this is
    // greater than any point in time read timestamps, we should either wait or return an error.
    if (!minSnapshot) {
        return false;
    }

    // If we are reading from the lastAppliedTimestamp and it is up-to-date with any catalog
    // changes, we can return.
    if (lastAppliedTimestamp &&
        (lastAppliedTimestamp->isNull() || *lastAppliedTimestamp >= *minSnapshot)) {
        return false;
    }

    // This can be set when readConcern is "snapshot" or "majority".
    auto mySnapshot = opCtx->recoveryUnit()->getPointInTimeReadTimestamp();

    // If we do not have a point in time to conflict with minSnapshot, return.
    if (!mySnapshot && !lastAppliedTimestamp) {
        return false;
    }

    // Return if there are no conflicting catalog changes with mySnapshot.
    if (mySnapshot && *mySnapshot >= *minSnapshot) {
        return false;
    }

    // Snapshot readConcern can't yield its locks when there are catalog changes.
    if (readConcernLevel == repl::ReadConcernLevel::kSnapshotReadConcern) {
        uasserted(
            ErrorCodes::SnapshotUnavailable,
            str::stream() << "Unable to read from a snapshot due to pending collection catalog "
                             "changes; please retry the operation. Snapshot timestamp is "
                          << mySnapshot->toString()
                          << ". Collection minimum is "
                          << minSnapshot->toString());
    }
    return true;
}


AutoGetCollectionForReadCommand::AutoGetCollectionForReadCommand(
    OperationContext* opCtx,
    const NamespaceStringOrUUID& nsOrUUID,
    AutoGetCollection::ViewMode viewMode,
    Date_t deadline)
    : _autoCollForRead(opCtx, nsOrUUID, viewMode, deadline),
      _statsTracker(opCtx,
                    _autoCollForRead.getNss(),
                    Top::LockType::ReadLocked,
                    _autoCollForRead.getDb() ? _autoCollForRead.getDb()->getProfilingLevel()
                                             : kDoNotChangeProfilingLevel,
                    deadline) {
    if (!_autoCollForRead.getView()) {
        // We have both the DB and collection locked, which is the prerequisite to do a stable shard
        // version check, but we'd like to do the check after we have a satisfactory snapshot.
        auto css = CollectionShardingState::get(opCtx, _autoCollForRead.getNss());
        css->checkShardVersionOrThrow(opCtx);
    }
}

OldClientContext::OldClientContext(OperationContext* opCtx, const std::string& ns, bool doVersion)
    : OldClientContext(
          opCtx, ns, doVersion, DatabaseHolder::getDatabaseHolder().get(opCtx, ns), false) {}

OldClientContext::OldClientContext(
    OperationContext* opCtx, const std::string& ns, bool doVersion, Database* db, bool justCreated)
    : _opCtx(opCtx), _db(db), _justCreated(justCreated) {
    if (!_db) {
        const auto dbName = nsToDatabaseSubstring(ns);
        invariant(_opCtx->lockState()->isDbLockedForMode(dbName, MODE_X));
        _db = DatabaseHolder::getDatabaseHolder().openDb(_opCtx, dbName, &_justCreated);
        invariant(_db);
    }

    auto const currentOp = CurOp::get(_opCtx);

    if (doVersion) {
        switch (currentOp->getNetworkOp()) {
            case dbGetMore:  // getMore is special and should be handled elsewhere
            case dbUpdate:   // update & delete check shard version as part of the write executor
            case dbDelete:   // path, so no need to check them here as well
                break;
            default:
                auto css = CollectionShardingState::get(_opCtx, ns);
                css->checkShardVersionOrThrow(_opCtx);
                break;
        }
    }

    stdx::lock_guard<Client> lk(*_opCtx->getClient());
    currentOp->enter_inlock(ns.c_str(), _db->getProfilingLevel());
}

OldClientContext::~OldClientContext() {
    // If in an interrupt, don't record any stats.
    // It is possible to have no lock after saving the lock state and being interrupted while
    // waiting to restore.
    if (_opCtx->getKillStatus() != ErrorCodes::OK)
        return;

    invariant(_opCtx->lockState()->isLocked());
    auto currentOp = CurOp::get(_opCtx);
    Top::get(_opCtx->getClient()->getServiceContext())
        .record(_opCtx,
                currentOp->getNS(),
                currentOp->getLogicalOp(),
                _opCtx->lockState()->isWriteLocked() ? Top::LockType::WriteLocked
                                                     : Top::LockType::ReadLocked,
                _timer.micros(),
                currentOp->isCommand(),
                currentOp->getReadWriteType());
}


OldClientWriteContext::OldClientWriteContext(OperationContext* opCtx, StringData ns)
    : _opCtx(opCtx), _nss(ns) {
    // Lock the database and collection
    _autoCreateDb.emplace(opCtx, _nss.db(), MODE_IX);
    _collLock.emplace(opCtx->lockState(), _nss.ns(), MODE_IX);

    // TODO (Kal): None of the places which use OldClientWriteContext seem to require versioning, so
    // we should consider defaulting this to false
    const bool doShardVersionCheck = true;
    _clientContext.emplace(opCtx,
                           _nss.ns(),
                           doShardVersionCheck,
                           _autoCreateDb->getDb(),
                           _autoCreateDb->justCreated());
    invariant(_autoCreateDb->getDb() == _clientContext->db());

    // If the collection exists, there is no need to lock into stronger mode
    if (getCollection())
        return;

    // If the database was just created, it is already locked in MODE_X so we can skip the relocking
    // code below
    if (_autoCreateDb->justCreated()) {
        dassert(opCtx->lockState()->isDbLockedForMode(_nss.db(), MODE_X));
        return;
    }

    // If the collection doesn't exists, put the context in a state where the database is locked in
    // MODE_X so that the collection can be created
    _clientContext.reset();
    _collLock.reset();
    _autoCreateDb.reset();
    _autoCreateDb.emplace(opCtx, _nss.db(), MODE_X);

    _clientContext.emplace(opCtx,
                           _nss.ns(),
                           doShardVersionCheck,
                           _autoCreateDb->getDb(),
                           _autoCreateDb->justCreated());
    invariant(_autoCreateDb->getDb() == _clientContext->db());
}

LockMode getLockModeForQuery(OperationContext* opCtx) {
    invariant(opCtx);

    // Use IX locks for autocommit:false multi-statement transactions; otherwise, use IS locks.
    if (Session::TransactionState::get(opCtx).requiresIXReadUpgrade) {
        return MODE_IX;
    }
    return MODE_IS;
}

}  // namespace mongo
