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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/drop_database.h"

#include <algorithm>

#include "mongo/db/background.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/index_builds_coordinator.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/repl/repl_client_info.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/service_context.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/util/duration.h"
#include "mongo/util/fail_point_service.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

MONGO_FAIL_POINT_DEFINE(dropDatabaseHangBeforeLog);
MONGO_FAIL_POINT_DEFINE(dropDatabaseHangAfterAllCollectionsDrop);

namespace {

/**
 * Removes database from catalog and writes dropDatabase entry to oplog.
 *
 * Ensures that the database's drop-pending flag is reset to false if the drop fails.
 *
 * Throws on errors.
 */
void _finishDropDatabase(OperationContext* opCtx,
                         const std::string& dbName,
                         Database* db,
                         std::size_t numCollections) {
    invariant(opCtx->lockState()->isDbLockedForMode(dbName, MODE_X));

    // If DatabaseHolder::dropDb() fails, we should reset the drop-pending state on Database.
    auto dropPendingGuard = makeGuard([db, opCtx] { db->setDropPending(opCtx, false); });

    BackgroundOperation::assertNoBgOpInProgForDb(dbName);
    IndexBuildsCoordinator::get(opCtx)->assertNoBgOpInProgForDb(dbName);

    auto databaseHolder = DatabaseHolder::get(opCtx);
    databaseHolder->dropDb(opCtx, db);
    dropPendingGuard.dismiss();

    log() << "dropDatabase " << dbName << " - dropped " << numCollections << " collection(s)";
    log() << "dropDatabase " << dbName << " - finished";

    if (MONGO_FAIL_POINT(dropDatabaseHangBeforeLog)) {
        log() << "dropDatabase - fail point dropDatabaseHangBeforeLog enabled. "
                 "Blocking until fail point is disabled. ";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(dropDatabaseHangBeforeLog);
    }

    writeConflictRetry(opCtx, "dropDatabase_database", dbName, [&] {
        WriteUnitOfWork wunit(opCtx);
        getGlobalServiceContext()->getOpObserver()->onDropDatabase(opCtx, dbName);
        wunit.commit();
    });
}

}  // namespace

Status dropDatabase(OperationContext* opCtx, const std::string& dbName) {
    uassert(ErrorCodes::IllegalOperation,
            "Cannot drop a database in read-only mode",
            !storageGlobalParams.readOnly);

    // As of SERVER-32205, dropping the admin database is prohibited.
    uassert(ErrorCodes::IllegalOperation,
            str::stream() << "Dropping the '" << dbName << "' database is prohibited.",
            dbName != NamespaceString::kAdminDb);

    // TODO (Kal): OldClientContext legacy, needs to be removed
    {
        CurOp::get(opCtx)->ensureStarted();
        stdx::lock_guard<Client> lk(*opCtx->getClient());
        CurOp::get(opCtx)->setNS_inlock(dbName);
    }

    auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    std::size_t numCollectionsToDrop = 0;
    std::size_t numCollections = 0;

    // We have to wait for the last drop-pending collection to be removed if there are no
    // collections to drop.
    repl::OpTime latestDropPendingOpTime;

    {
        AutoGetDb autoDB(opCtx, dbName, MODE_X);
        Database* const db = autoDB.getDb();
        if (!db) {
            return Status(ErrorCodes::NamespaceNotFound,
                          str::stream() << "Could not drop database " << dbName
                                        << " because it does not exist");
        }

        bool userInitiatedWritesAndNotPrimary =
            opCtx->writesAreReplicated() && !replCoord->canAcceptWritesForDatabase(opCtx, dbName);

        if (userInitiatedWritesAndNotPrimary) {
            return Status(ErrorCodes::NotMaster,
                          str::stream() << "Not primary while dropping database " << dbName);
        }

        if (db->isDropPending(opCtx)) {
            return Status(ErrorCodes::DatabaseDropPending,
                          str::stream()
                              << "The database is currently being dropped. Database: " << dbName);
        }

        log() << "dropDatabase " << dbName << " - starting";
        db->setDropPending(opCtx, true);

        // If Database::dropCollectionEventIfSystem() fails, we should reset the drop-pending state
        // on Database.
        auto dropPendingGuard = makeGuard([&db, opCtx] { db->setDropPending(opCtx, false); });

        std::vector<NamespaceString> collectionsToDrop;
        for (auto collIt = db->begin(opCtx); collIt != db->end(opCtx); ++collIt) {
            auto collection = *collIt;
            if (!collection) {
                break;
            }

            const auto& nss = collection->ns();
            numCollections++;

            log() << "dropDatabase " << dbName << " - dropping collection: " << nss;

            if (nss.isDropPendingNamespace() && replCoord->isReplEnabled() &&
                opCtx->writesAreReplicated()) {
                log() << "dropDatabase " << dbName << " - found drop-pending collection: " << nss;
                latestDropPendingOpTime = std::max(
                    latestDropPendingOpTime, uassertStatusOK(nss.getDropPendingNamespaceOpTime()));
                continue;
            }
            if (replCoord->isOplogDisabledFor(opCtx, nss)) {
                continue;
            }
            collectionsToDrop.push_back(nss);
        }
        numCollectionsToDrop = collectionsToDrop.size();

        for (auto nss : collectionsToDrop) {
            if (!opCtx->writesAreReplicated()) {
                // Dropping a database on a primary replicates individual collection drops followed
                // by a database drop oplog entry. When a secondary observes the database drop oplog
                // entry, all of the replicated collections that were dropped must have been
                // processed. Only non-replicated collections like `system.profile` should be left
                // to remove. Collections with the `tmp.mr` namespace may or may not be getting
                // replicated; be conservative and assume they are not.
                invariant(!nss.isReplicated() || nss.coll().startsWith("tmp.mr"));
            }

            BackgroundOperation::assertNoBgOpInProgForNs(nss.ns());
            IndexBuildsCoordinator::get(opCtx)->assertNoIndexBuildInProgForCollection(
                db->getCollection(opCtx, nss)->uuid());

            writeConflictRetry(opCtx, "dropDatabase_collection", nss.ns(), [&] {
                WriteUnitOfWork wunit(opCtx);
                // A primary processing this will assign a timestamp when the operation is written
                // to the oplog. As stated above, a secondary processing must only observe
                // non-replicated collections, thus this should not be timestamped.
                fassert(40476, db->dropCollectionEvenIfSystem(opCtx, nss));
                wunit.commit();
            });
        }


        // _finishDropDatabase creates its own scope guard to ensure drop-pending is unset.
        dropPendingGuard.dismiss();

        // If there are no collection drops to wait for, we complete the drop database operation.
        if (numCollectionsToDrop == 0U && latestDropPendingOpTime.isNull()) {
            _finishDropDatabase(opCtx, dbName, db, numCollections);
            return Status::OK();
        }
    }

    // Create a scope guard to reset the drop-pending state on the Database to false if there are
    // any errors while we await the replication of any collection drops and then reacquire the
    // locks (which can throw) needed to finish the drop database.
    auto dropPendingGuardWhileUnlocked = makeGuard([dbName, opCtx] {
        UninterruptibleLockGuard noInterrupt(opCtx->lockState());
        AutoGetDb autoDB(opCtx, dbName, MODE_IX);
        if (auto db = autoDB.getDb()) {
            db->setDropPending(opCtx, false);
        }
    });

    {
        // Holding of any locks is disallowed while awaiting replication because this can
        // potentially block for long time while doing network activity.
        //
        // Even though dropDatabase() does not explicitly acquire any locks before awaiting
        // replication, it is possible that the caller of this function may already have acquired
        // a lock. The applyOps command is an example of a dropDatabase() caller that does this.
        // Therefore, we have to release any locks using a TempRelease RAII object.
        //
        // TODO: Remove the use of this TempRelease object when SERVER-29802 is completed.
        // The work in SERVER-29802 will adjust the locking rules around applyOps operations and
        // dropDatabase is expected to be one of the operations where we expect to no longer acquire
        // the global lock.
        Lock::TempRelease release(opCtx->lockState());

        auto awaitOpTime = [&]() {
            if (numCollectionsToDrop > 0U) {
                const auto& clientInfo = repl::ReplClientInfo::forClient(opCtx->getClient());
                return clientInfo.getLastOp();
            }
            invariant(!latestDropPendingOpTime.isNull());
            return latestDropPendingOpTime;
        }();

        // The user-supplied wTimeout should be used when waiting for majority write concern.
        const auto& userWriteConcern = opCtx->getWriteConcern();
        const auto wTimeout = !userWriteConcern.usedDefault
            ? Milliseconds{userWriteConcern.wTimeout}
            : duration_cast<Milliseconds>(Minutes(10));

        // This is used to wait for the collection drops to replicate to a majority of the replica
        // set. Note: Even though we're setting UNSET here, kMajority implies JOURNAL if journaling
        // is supported by mongod and writeConcernMajorityJournalDefault is set to true in the
        // ReplSetConfig.
        const WriteConcernOptions dropDatabaseWriteConcern(
            WriteConcernOptions::kMajority, WriteConcernOptions::SyncMode::UNSET, wTimeout);

        log() << "dropDatabase " << dbName << " waiting for " << awaitOpTime
              << " to be replicated at " << dropDatabaseWriteConcern.toBSON() << ". Dropping "
              << numCollectionsToDrop << " collection(s), with last collection drop at "
              << latestDropPendingOpTime;

        auto result = replCoord->awaitReplication(opCtx, awaitOpTime, dropDatabaseWriteConcern);

        // If the user-provided write concern is weaker than majority, this is effectively a no-op.
        if (result.status.isOK() && !userWriteConcern.usedDefault) {
            log() << "dropDatabase " << dbName << " waiting for " << awaitOpTime
                  << " to be replicated at " << userWriteConcern.toBSON();
            result = replCoord->awaitReplication(opCtx, awaitOpTime, userWriteConcern);
        }

        if (!result.status.isOK()) {
            return result.status.withContext(str::stream()
                                             << "dropDatabase " << dbName << " failed waiting for "
                                             << numCollectionsToDrop
                                             << " collection drop(s) (most recent drop optime: "
                                             << awaitOpTime.toString() << ") to replicate.");
        }

        log() << "dropDatabase " << dbName << " - successfully dropped " << numCollectionsToDrop
              << " collection(s) (most recent drop optime: " << awaitOpTime << ") after "
              << result.duration << ". dropping database";
    }

    if (MONGO_FAIL_POINT(dropDatabaseHangAfterAllCollectionsDrop)) {
        log() << "dropDatabase - fail point dropDatabaseHangAfterAllCollectionsDrop enabled. "
                 "Blocking until fail point is disabled. ";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(dropDatabaseHangAfterAllCollectionsDrop);
    }

    AutoGetDb autoDB(opCtx, dbName, MODE_X);
    auto db = autoDB.getDb();
    if (!db) {
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "Could not drop database " << dbName
                                    << " because it does not exist after dropping "
                                    << numCollectionsToDrop << " collection(s).");
    }

    bool userInitiatedWritesAndNotPrimary =
        opCtx->writesAreReplicated() && !replCoord->canAcceptWritesForDatabase(opCtx, dbName);

    if (userInitiatedWritesAndNotPrimary) {
        return Status(ErrorCodes::PrimarySteppedDown,
                      str::stream()
                          << "Could not drop database " << dbName
                          << " because we transitioned from PRIMARY to "
                          << replCoord->getMemberState().toString() << " while waiting for "
                          << numCollectionsToDrop << " pending collection drop(s).");
    }

    // _finishDropDatabase creates its own scope guard to ensure drop-pending is unset.
    dropPendingGuardWhileUnlocked.dismiss();

    _finishDropDatabase(opCtx, dbName, db, numCollections);

    return Status::OK();
}

}  // namespace mongo
