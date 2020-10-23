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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplication

#include "mongo/platform/basic.h"

#include "mongo/db/write_concern.h"

#include "mongo/base/counter.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/client.h"
#include "mongo/db/commands/server_status_metric.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/read_write_concern_defaults.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_options.h"
#include "mongo/db/service_context.h"
#include "mongo/db/stats/timer_stats.h"
#include "mongo/db/storage/storage_engine.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/rpc/protocol.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/log.h"

namespace mongo {

using repl::OpTime;
using repl::OpTimeAndWallTime;
using std::string;

static TimerStats gleWtimeStats;
static ServerStatusMetricField<TimerStats> displayGleLatency("getLastError.wtime", &gleWtimeStats);

static Counter64 gleWtimeouts;
static ServerStatusMetricField<Counter64> gleWtimeoutsDisplay("getLastError.wtimeouts",
                                                              &gleWtimeouts);

MONGO_FAIL_POINT_DEFINE(hangBeforeWaitingForWriteConcern);

bool commandSpecifiesWriteConcern(const BSONObj& cmdObj) {
    return cmdObj.hasField(WriteConcernOptions::kWriteConcernField);
}

StatusWith<WriteConcernOptions> extractWriteConcern(OperationContext* opCtx,
                                                    const BSONObj& cmdObj) {
    // The default write concern if empty is {w:1}. Specifying {w:0} is/was allowed, but is
    // interpreted identically to {w:1}.
    auto wcResult = WriteConcernOptions::extractWCFromCommand(cmdObj);
    if (!wcResult.isOK()) {
        return wcResult.getStatus();
    }

    WriteConcernOptions writeConcern = wcResult.getValue();

    // If no write concern is specified in the command (so usedDefault is true), then use the
    // cluster-wide default WC (if there is one), or else the default WC from the ReplSetConfig
    // (which takes the ReplicationCoordinator mutex).
    if (writeConcern.usedDefault) {
        writeConcern = ([&]() {
            // WriteConcern defaults can only be applied on regular replica set members.  Operations
            // received by shard and config servers should always have WC explicitly specified.
            if (serverGlobalParams.clusterRole != ClusterRole::ShardServer &&
                serverGlobalParams.clusterRole != ClusterRole::ConfigServer &&
                !opCtx->inMultiDocumentTransaction() && !opCtx->getClient()->isInDirectClient()) {
                auto wcDefault = ReadWriteConcernDefaults::get(opCtx->getServiceContext())
                                     .getDefaultWriteConcern(opCtx);
                if (wcDefault) {
                    LOG(2) << "Applying default writeConcern on " << cmdObj.firstElementFieldName()
                           << " of " << wcDefault->toBSON();
                    return *wcDefault;
                }
            }

            auto getLastErrorDefault =
                repl::ReplicationCoordinator::get(opCtx)->getGetLastErrorDefault();
            // Since replication configs always include all fields (explicitly setting them to the
            // default value if necessary), usedDefault and usedDefaultW are always false here, even
            // if the getLastErrorDefaults has never actually been set (because the
            // getLastErrorDefaults writeConcern has been explicitly read out of the replset
            // config).
            //
            // In this case, where the getLastErrorDefault is "conceptually unset" (ie. identical to
            // the implicit server default of { w: 1, wtimeout: 0 }), we would prefer if downstream
            // code behaved as if no writeConcern had been applied (since in addition to "no"
            // getLastErrorDefaults, there is no ReadWriteConcernDefaults writeConcern and the user
            // did not specify a writeConcern).
            //
            // Therefore when the getLastErrorDefault is { w: 1, wtimeout: 0 } we force usedDefault
            // and usedDefaultW to be true.
            if (getLastErrorDefault.wNumNodes == 1 && getLastErrorDefault.wTimeout == 0) {
                getLastErrorDefault.usedDefault = true;
                getLastErrorDefault.usedDefaultW = true;
            }
            return getLastErrorDefault;
        })();
        if (writeConcern.wNumNodes == 0 && writeConcern.wMode.empty()) {
            writeConcern.wNumNodes = 1;
        }
        writeConcern.usedDefaultW = true;
    }

    if (writeConcern.usedDefault && serverGlobalParams.clusterRole == ClusterRole::ConfigServer &&
        !opCtx->getClient()->isInDirectClient() &&
        (opCtx->getClient()->session() &&
         (opCtx->getClient()->session()->getTags() & transport::Session::kInternalClient))) {
        // Upconvert the writeConcern of any incoming requests from internal connections (i.e.,
        // from other nodes in the cluster) to "majority." This protects against internal code that
        // does not specify writeConcern when writing to the config server.
        writeConcern = {
            WriteConcernOptions::kMajority, WriteConcernOptions::SyncMode::UNSET, Seconds(30)};
    } else {
        Status wcStatus = validateWriteConcern(opCtx, writeConcern);
        if (!wcStatus.isOK()) {
            return wcStatus;
        }
    }

    return writeConcern;
}

Status validateWriteConcern(OperationContext* opCtx, const WriteConcernOptions& writeConcern) {
    if (writeConcern.syncMode == WriteConcernOptions::SyncMode::JOURNAL &&
        !opCtx->getServiceContext()->getStorageEngine()->isDurable()) {
        return Status(ErrorCodes::BadValue,
                      "cannot use 'j' option when a host does not have journaling enabled");
    }

    const auto replMode = repl::ReplicationCoordinator::get(opCtx)->getReplicationMode();

    if (replMode == repl::ReplicationCoordinator::modeNone && writeConcern.wNumNodes > 1) {
        return Status(ErrorCodes::BadValue, "cannot use 'w' > 1 when a host is not replicated");
    }

    if (replMode != repl::ReplicationCoordinator::modeReplSet && !writeConcern.wMode.empty() &&
        writeConcern.wMode != WriteConcernOptions::kMajority) {
        return Status(ErrorCodes::BadValue,
                      string("cannot use non-majority 'w' mode ") + writeConcern.wMode +
                          " when a host is not a member of a replica set");
    }

    return Status::OK();
}

void WriteConcernResult::appendTo(const WriteConcernOptions& writeConcern,
                                  BSONObjBuilder* result) const {
    if (syncMillis >= 0)
        result->appendNumber("syncMillis", syncMillis);

    if (fsyncFiles >= 0)
        result->appendNumber("fsyncFiles", fsyncFiles);

    if (wTime >= 0) {
        if (wTimedOut)
            result->appendNumber("waited", wTime);
        else
            result->appendNumber("wtime", wTime);
    }

    if (wTimedOut)
        result->appendBool("wtimeout", true);

    if (writtenTo.size()) {
        BSONArrayBuilder hosts(result->subarrayStart("writtenTo"));
        for (size_t i = 0; i < writtenTo.size(); ++i) {
            hosts.append(writtenTo[i].toString());
        }
    } else {
        result->appendNull("writtenTo");
    }

    if (err.empty())
        result->appendNull("err");
    else
        result->append("err", err);

    // For ephemeral storage engines, 0 files may be fsynced
    invariant(writeConcern.syncMode != WriteConcernOptions::SyncMode::FSYNC ||
              (result->asTempObj()["fsyncFiles"].numberLong() >= 0 ||
               !result->asTempObj()["waited"].eoo()));
}

Status waitForWriteConcern(OperationContext* opCtx,
                           const OpTime& replOpTime,
                           const WriteConcernOptions& writeConcern,
                           WriteConcernResult* result) {
    LOG(2) << "Waiting for write concern. OpTime: " << replOpTime
           << ", write concern: " << writeConcern.toBSON();

    auto const replCoord = repl::ReplicationCoordinator::get(opCtx);

    if (!opCtx->getClient()->isInDirectClient()) {
        // Respecting this failpoint for internal clients prevents stepup from working properly.
        hangBeforeWaitingForWriteConcern.pauseWhileSet();
    }

    // Next handle blocking on disk
    Timer syncTimer;
    WriteConcernOptions writeConcernWithPopulatedSyncMode =
        replCoord->populateUnsetWriteConcernOptionsSyncMode(writeConcern);

    switch (writeConcernWithPopulatedSyncMode.syncMode) {
        case WriteConcernOptions::SyncMode::UNSET:
            severe() << "Attempting to wait on a WriteConcern with an unset sync option";
            fassertFailed(34410);
        case WriteConcernOptions::SyncMode::NONE:
            break;
        case WriteConcernOptions::SyncMode::FSYNC: {
            StorageEngine* storageEngine = getGlobalServiceContext()->getStorageEngine();
            if (!storageEngine->isDurable()) {
                result->fsyncFiles = storageEngine->flushAllFiles(opCtx, true);
            } else {
                // We only need to commit the journal if we're durable
                opCtx->recoveryUnit()->waitUntilDurable(opCtx);
            }
            break;
        }
        case WriteConcernOptions::SyncMode::JOURNAL:
            if (replCoord->getReplicationMode() != repl::ReplicationCoordinator::Mode::modeNone) {
                // Wait for ops to become durable then update replication system's
                // knowledge of this.
                auto appliedOpTimeAndWallTime = replCoord->getMyLastAppliedOpTimeAndWallTime();
                opCtx->recoveryUnit()->waitUntilDurable(opCtx);
                replCoord->setMyLastDurableOpTimeAndWallTimeForward(appliedOpTimeAndWallTime);
            } else {
                opCtx->recoveryUnit()->waitUntilDurable(opCtx);
            }
            break;
    }

    result->syncMillis = syncTimer.millis();

    // Now wait for replication

    if (replOpTime.isNull()) {
        // no write happened for this client yet
        return Status::OK();
    }

    // needed to avoid incrementing gleWtimeStats SERVER-9005
    if (!writeConcernWithPopulatedSyncMode.needToWaitForOtherNodes()) {
        // no desired replication check
        return Status::OK();
    }

    // Replica set stepdowns and gle mode changes are thrown as errors
    repl::ReplicationCoordinator::StatusAndDuration replStatus =
        replCoord->awaitReplication(opCtx, replOpTime, writeConcernWithPopulatedSyncMode);
    if (replStatus.status == ErrorCodes::WriteConcernFailed) {
        gleWtimeouts.increment();
        result->err = "timeout";
        result->wTimedOut = true;
    }

    gleWtimeStats.recordMillis(durationCount<Milliseconds>(replStatus.duration));
    result->wTime = durationCount<Milliseconds>(replStatus.duration);

    return replStatus.status;
}

}  // namespace mongo
