/**
 *    Copyright (C) 2017 MongoDB Inc.
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
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplication
#define LOG_FOR_RECOVERY(level) \
    MONGO_LOG_COMPONENT(level, ::mongo::logger::LogComponent::kStorageRecovery)

#include "mongo/platform/basic.h"

#include "mongo/db/repl/replication_recovery.h"

#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/replication_consistency_markers_impl.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/sync_tail.h"
#include "mongo/db/server_recovery.h"
#include "mongo/db/session.h"
#include "mongo/util/log.h"

namespace mongo {
namespace repl {

ReplicationRecoveryImpl::ReplicationRecoveryImpl(StorageInterface* storageInterface,
                                                 ReplicationConsistencyMarkers* consistencyMarkers)
    : _storageInterface(storageInterface), _consistencyMarkers(consistencyMarkers) {}

void ReplicationRecoveryImpl::recoverFromOplog(OperationContext* opCtx,
                                               boost::optional<Timestamp> stableTimestamp) try {
    if (_consistencyMarkers->getInitialSyncFlag(opCtx)) {
        log() << "No recovery needed. Initial sync flag set.";
        return;  // Initial Sync will take over so no cleanup is needed.
    }

    const auto serviceCtx = getGlobalServiceContext();
    inReplicationRecovery(serviceCtx) = true;
    ON_BLOCK_EXIT([serviceCtx] {
        invariant(
            inReplicationRecovery(serviceCtx),
            "replication recovery flag is unexpectedly unset when exiting recoverFromOplog()");
        inReplicationRecovery(serviceCtx) = false;
        sizeRecoveryState(serviceCtx).clearStateAfterRecovery();
    });

    const auto truncateAfterPoint = _consistencyMarkers->getOplogTruncateAfterPoint(opCtx);
    if (!truncateAfterPoint.isNull()) {
        log() << "Removing unapplied entries starting at: " << truncateAfterPoint.toBSON();
        _truncateOplogTo(opCtx, truncateAfterPoint);

        // Clear the truncateAfterPoint so that we don't truncate the next batch of oplog entries
        // erroneously.
        _consistencyMarkers->setOplogTruncateAfterPoint(opCtx, {});
        opCtx->recoveryUnit()->waitUntilDurable();
    }

    auto topOfOplogSW = _getTopOfOplog(opCtx);
    if (topOfOplogSW.getStatus() == ErrorCodes::CollectionIsEmpty ||
        topOfOplogSW.getStatus() == ErrorCodes::NamespaceNotFound) {
        // Oplog is empty. There are no oplog entries to apply, so we exit recovery and go into
        // initial sync.
        log() << "No oplog entries to apply for recovery. Oplog is empty.";
        return;
    }
    fassert(40290, topOfOplogSW);
    const auto topOfOplog = topOfOplogSW.getValue();

    // If we were passed in a stable timestamp, we are in rollback recovery and should recover from
    // that stable timestamp. Otherwise, we're recovering at startup. If this storage engine
    // supports recover to stable timestamp, we ask it for the recovery timestamp. If the storage
    // engine returns a timestamp, we recover from that point. However, if the storage engine
    // returns "none", the storage engine does not have a stable checkpoint and we must recover from
    // an unstable checkpoint instead.
    const bool supportsRecoverToStableTimestamp =
        _storageInterface->supportsRecoverToStableTimestamp(opCtx->getServiceContext());
    if (!stableTimestamp && supportsRecoverToStableTimestamp) {
        stableTimestamp = _storageInterface->getRecoveryTimestamp(opCtx->getServiceContext());
    }

    const auto appliedThrough = _consistencyMarkers->getAppliedThrough(opCtx);
    invariant(!stableTimestamp || appliedThrough.isNull() ||
                  *stableTimestamp == appliedThrough.getTimestamp(),
              str::stream() << "Stable timestamp " << stableTimestamp->toString()
                            << " does not equal appliedThrough timestamp "
                            << appliedThrough.toString());

    if (stableTimestamp) {
        invariant(supportsRecoverToStableTimestamp);
        _recoverFromStableTimestamp(opCtx, *stableTimestamp, appliedThrough, topOfOplog);
    } else {
        _recoverFromUnstableCheckpoint(opCtx, appliedThrough, topOfOplog);
    }
} catch (...) {
    severe() << "Caught exception during replication recovery: " << exceptionToStatus();
    std::terminate();
}

void ReplicationRecoveryImpl::_recoverFromStableTimestamp(OperationContext* opCtx,
                                                          Timestamp stableTimestamp,
                                                          OpTime appliedThrough,
                                                          OpTime topOfOplog) {
    invariant(!stableTimestamp.isNull());
    invariant(!topOfOplog.isNull());
    const auto truncateAfterPoint = _consistencyMarkers->getOplogTruncateAfterPoint(opCtx);
    log() << "Recovering from stable timestamp: " << stableTimestamp
          << " (top of oplog: " << topOfOplog << ", appliedThrough: " << appliedThrough
          << ", TruncateAfter: " << truncateAfterPoint << ")";

    log() << "Starting recovery oplog application at the stable timestamp: " << stableTimestamp;
    _applyToEndOfOplog(opCtx, stableTimestamp, topOfOplog.getTimestamp());
}

void ReplicationRecoveryImpl::_recoverFromUnstableCheckpoint(OperationContext* opCtx,
                                                             OpTime appliedThrough,
                                                             OpTime topOfOplog) {
    invariant(!topOfOplog.isNull());
    log() << "Recovering from an unstable checkpoint (top of oplog: " << topOfOplog
          << ", appliedThrough: " << appliedThrough << ")";

    if (appliedThrough.isNull()) {
        // The appliedThrough would be null if we shut down cleanly or crashed as a primary. Either
        // way we are consistent at the top of the oplog.
        log() << "No oplog entries to apply for recovery. appliedThrough is null.";
    } else {
        // If the appliedThrough is not null, then we shut down uncleanly during secondary oplog
        // application and must apply from the appliedThrough to the top of the oplog.
        log() << "Starting recovery oplog application at the appliedThrough: " << appliedThrough
              << ", through the top of the oplog: " << topOfOplog;
        _applyToEndOfOplog(opCtx, appliedThrough.getTimestamp(), topOfOplog.getTimestamp());
    }

    // `_recoverFromUnstableCheckpoint` is only expected to be called on startup.
    _storageInterface->setInitialDataTimestamp(opCtx->getServiceContext(),
                                               topOfOplog.getTimestamp());

    // Ensure the `appliedThrough` is set to the top of oplog, specifically if the node was
    // previously running as a primary. If a crash happens before the first stable checkpoint on
    // upgrade, replication recovery will know it must apply from this point and not assume the
    // datafiles contain any writes that were taken before the crash.
    _consistencyMarkers->setAppliedThrough(opCtx, topOfOplog);

    // Force the set `appliedThrough` to become durable on disk in a checkpoint. This method would
    // typically take a stable checkpoint, but because we're starting up from a checkpoint that
    // has no checkpoint timestamp, the stable checkpoint "degrades" into an unstable checkpoint.
    //
    // Not waiting for checkpoint durability here can result in a scenario where the node takes
    // writes and persists them to the oplog, but crashes before a stable checkpoint persists a
    // "recovery timestamp". The typical startup path for data-bearing nodes with 4.0 is to use
    // the recovery timestamp to determine where to play oplog forward from. As this method shows,
    // when a recovery timestamp does not exist, the applied through is used to determine where to
    // start playing oplog entries from.
    opCtx->recoveryUnit()->waitUntilUnjournaledWritesDurable();
}

void ReplicationRecoveryImpl::_applyToEndOfOplog(OperationContext* opCtx,
                                                 Timestamp oplogApplicationStartPoint,
                                                 Timestamp topOfOplog) {
    invariant(!oplogApplicationStartPoint.isNull());
    invariant(!topOfOplog.isNull());

    // Check if we have any unapplied ops in our oplog. It is important that this is done after
    // deleting the ragged end of the oplog.
    if (oplogApplicationStartPoint == topOfOplog) {
        log() << "No oplog entries to apply for recovery. Start point is at the top of the oplog.";
        return;  // We've applied all the valid oplog we have.
    } else if (oplogApplicationStartPoint > topOfOplog) {
        severe() << "Applied op " << oplogApplicationStartPoint.toBSON()
                 << " not found. Top of oplog is " << topOfOplog.toBSON() << '.';
        fassertFailedNoTrace(40313);
    }

    log() << "Replaying stored operations from " << oplogApplicationStartPoint.toBSON()
          << " (exclusive) to " << topOfOplog.toBSON() << " (inclusive).";

    DBDirectClient db(opCtx);
    auto cursor = db.query(NamespaceString::kRsOplogNamespace.ns(),
                           QUERY("ts" << BSON("$gte" << oplogApplicationStartPoint)),
                           /*batchSize*/ 0,
                           /*skip*/ 0,
                           /*projection*/ nullptr,
                           QueryOption_OplogReplay);

    // Check that the first document matches our appliedThrough point then skip it since it's
    // already been applied.
    if (!cursor->more()) {
        // This should really be impossible because we check above that the top of the oplog is
        // strictly > appliedThrough. If this fails it represents a serious bug in either the
        // storage engine or query's implementation of OplogReplay.
        severe() << "Couldn't find any entries in the oplog >= "
                 << oplogApplicationStartPoint.toBSON() << " which should be impossible.";
        fassertFailedNoTrace(40293);
    }

    auto firstTimestampFound =
        fassert(40291, OpTime::parseFromOplogEntry(cursor->nextSafe())).getTimestamp();
    if (firstTimestampFound != oplogApplicationStartPoint) {
        severe() << "Oplog entry at " << oplogApplicationStartPoint.toBSON()
                 << " is missing; actual entry found is " << firstTimestampFound.toBSON();
        fassertFailedNoTrace(40292);
    }

    // Apply remaining ops one at at time, but don't log them because they are already logged.
    UnreplicatedWritesBlock uwb(opCtx);
    DisableDocumentValidation validationDisabler(opCtx);

    BSONObj entry;
    while (cursor->more()) {
        entry = cursor->nextSafe();
        LOG_FOR_RECOVERY(2) << "Applying op during replication recovery: " << redact(entry);
        fassert(40294, SyncTail::syncApply(opCtx, entry, OplogApplication::Mode::kRecovering));

        auto oplogEntry = fassert(50763, OplogEntry::parse(entry));
        if (auto txnTableOplog = Session::createMatchingTransactionTableUpdate(oplogEntry)) {
            fassert(50764,
                    SyncTail::syncApply(
                        opCtx, txnTableOplog->toBSON(), OplogApplication::Mode::kRecovering));
        }
    }

    // We may crash before setting appliedThrough. If we have a stable checkpoint, we will recover
    // to that checkpoint at a replication consistent point, and applying the oplog is safe.
    // If we don't have a stable checkpoint, then we must be in startup recovery, and not rollback
    // recovery, because we only roll back to a stable timestamp when we have a stable checkpoint.
    // Startup recovery from an unstable checkpoint only ever applies a single batch and it is safe
    // to replay the batch from any point.
    _consistencyMarkers->setAppliedThrough(opCtx,
                                           fassert(40295, OpTime::parseFromOplogEntry(entry)));
}

StatusWith<OpTime> ReplicationRecoveryImpl::_getTopOfOplog(OperationContext* opCtx) const {
    const auto docsSW = _storageInterface->findDocuments(opCtx,
                                                         NamespaceString::kRsOplogNamespace,
                                                         boost::none,  // Collection scan
                                                         StorageInterface::ScanDirection::kBackward,
                                                         {},
                                                         BoundInclusion::kIncludeStartKeyOnly,
                                                         1U);
    if (!docsSW.isOK()) {
        return docsSW.getStatus();
    }
    const auto docs = docsSW.getValue();
    if (docs.empty()) {
        return Status(ErrorCodes::CollectionIsEmpty, "oplog is empty");
    }
    invariant(1U == docs.size());

    return OpTime::parseFromOplogEntry(docs.front());
}

void ReplicationRecoveryImpl::_truncateOplogTo(OperationContext* opCtx,
                                               Timestamp truncateTimestamp) {
    const NamespaceString oplogNss(NamespaceString::kRsOplogNamespace);
    AutoGetDb autoDb(opCtx, oplogNss.db(), MODE_IX);
    Lock::CollectionLock oplogCollectionLoc(opCtx->lockState(), oplogNss.ns(), MODE_X);
    Collection* oplogCollection = autoDb.getDb()->getCollection(opCtx, oplogNss);
    if (!oplogCollection) {
        fassertFailedWithStatusNoTrace(
            34418,
            Status(ErrorCodes::NamespaceNotFound,
                   str::stream() << "Can't find " << NamespaceString::kRsOplogNamespace.ns()));
    }

    // Scan through oplog in reverse, from latest entry to first, to find the truncateTimestamp.
    RecordId oldestIDToDelete;  // Non-null if there is something to delete.
    auto oplogRs = oplogCollection->getRecordStore();
    auto oplogReverseCursor = oplogRs->getCursor(opCtx, /*forward=*/false);
    size_t count = 0;
    while (auto next = oplogReverseCursor->next()) {
        const BSONObj entry = next->data.releaseToBson();
        const RecordId id = next->id;
        count++;

        const auto tsElem = entry["ts"];
        if (count == 1) {
            if (tsElem.eoo())
                LOG(2) << "Oplog tail entry: " << redact(entry);
            else
                LOG(2) << "Oplog tail entry ts field: " << tsElem;
        }

        if (tsElem.timestamp() < truncateTimestamp) {
            // If count == 1, that means that we have nothing to delete because everything in the
            // oplog is < truncateTimestamp.
            if (count != 1) {
                invariant(!oldestIDToDelete.isNull());
                oplogCollection->cappedTruncateAfter(opCtx, oldestIDToDelete, /*inclusive=*/true);
            }
            return;
        }

        oldestIDToDelete = id;
    }

    severe() << "Reached end of oplog looking for oplog entry before " << truncateTimestamp.toBSON()
             << " but couldn't find any after looking through " << count << " entries.";
    fassertFailedNoTrace(40296);
}

}  // namespace repl
}  // namespace mongo
