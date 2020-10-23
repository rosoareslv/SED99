/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/client/shard_remote.h"

#include <string>

#include "mongo/client/query_fetcher.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/remote_command_targeter.h"
#include "mongo/client/replica_set_monitor.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/read_concern_args.h"
#include "mongo/db/query/lite_parsed_query.h"
#include "mongo/executor/task_executor_pool.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/rpc/metadata/repl_set_metadata.h"
#include "mongo/rpc/metadata/server_selection_metadata.h"
#include "mongo/s/grid.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

using std::string;

using executor::RemoteCommandRequest;
using executor::RemoteCommandResponse;
using executor::TaskExecutor;
using RemoteCommandCallbackArgs = TaskExecutor::RemoteCommandCallbackArgs;

namespace {

const Status kInternalErrorStatus{ErrorCodes::InternalError,
                                  "Invalid to check for write concern error if command failed"};

const Seconds kConfigCommandTimeout{30};

// Include kReplSetMetadataFieldName in a request to get the shard's ReplSetMetadata in the
// response.
const BSONObj kReplMetadata(BSON(rpc::kReplSetMetadataFieldName << 1));

// Allow the command to be executed on a secondary (see ServerSelectionMetadata).
const BSONObj kSecondaryOkMetadata{rpc::ServerSelectionMetadata(true, boost::none).toBSON()};

// Helper for requesting ReplSetMetadata in the response as well as allowing the command to be
// executed on a secondary.
const BSONObj kReplSecondaryOkMetadata{[] {
    BSONObjBuilder o;
    o.appendElements(kSecondaryOkMetadata);
    o.appendElements(kReplMetadata);
    return o.obj();
}()};

class ErrorCodesHash {
public:
    size_t operator()(ErrorCodes::Error e) const {
        return std::hash<typename std::underlying_type<ErrorCodes::Error>::type>()(e);
    }
};

using ErrorCodesSet = unordered_set<ErrorCodes::Error, ErrorCodesHash>;

const ErrorCodesSet kNotMasterErrors{
    ErrorCodes::NotMaster, ErrorCodes::NotMasterNoSlaveOk, ErrorCodes::NotMasterOrSecondary};

const ErrorCodesSet kAllRetriableErrors{
    ErrorCodes::NotMaster,
    ErrorCodes::NotMasterNoSlaveOk,
    ErrorCodes::NotMasterOrSecondary,
    // If write concern failed to be satisfied on the remote server, this most probably means that
    // some of the secondary nodes were unreachable or otherwise unresponsive, so the call is safe
    // to be retried if idempotency can be guaranteed.
    ErrorCodes::WriteConcernFailed,
    ErrorCodes::HostUnreachable,
    ErrorCodes::HostNotFound,
    ErrorCodes::NetworkTimeout,
    ErrorCodes::InterruptedDueToReplStateChange};

/**
 * Returns a new BSONObj describing the same command and arguments as 'cmdObj', but with a maxTimeMS
 * set on it that is the minimum of the maxTimeMS in 'cmdObj' (if present), 'maxTimeMicros', and
 * 30 seconds.
 */
BSONObj appendMaxTimeToCmdObj(long long maxTimeMicros, const BSONObj& cmdObj) {
    Milliseconds maxTime = duration_cast<Milliseconds>(kConfigCommandTimeout);

    Milliseconds remainingTxnMaxTime = duration_cast<Milliseconds>(Microseconds(maxTimeMicros));
    bool hasTxnMaxTime(remainingTxnMaxTime != Microseconds::zero());
    bool hasUserMaxTime = !cmdObj[LiteParsedQuery::cmdOptionMaxTimeMS].eoo();

    if (hasTxnMaxTime) {
        if (remainingTxnMaxTime < maxTime) {
            maxTime = remainingTxnMaxTime;
        }
    }

    if (hasUserMaxTime) {
        Milliseconds userMaxTime(cmdObj[LiteParsedQuery::cmdOptionMaxTimeMS].numberLong());
        if (userMaxTime == maxTime) {
            return cmdObj;
        }
        if (userMaxTime < maxTime) {
            maxTime = userMaxTime;
        }
    }

    BSONObjBuilder updatedCmdBuilder;
    if (hasUserMaxTime) {  // Need to remove user provided maxTimeMS.
        BSONObjIterator cmdObjIter(cmdObj);
        const char* maxTimeFieldName = LiteParsedQuery::cmdOptionMaxTimeMS;
        while (cmdObjIter.more()) {
            BSONElement e = cmdObjIter.next();
            if (str::equals(e.fieldName(), maxTimeFieldName)) {
                continue;
            }
            updatedCmdBuilder.append(e);
        }
    } else {
        updatedCmdBuilder.appendElements(cmdObj);
    }

    updatedCmdBuilder.append(LiteParsedQuery::cmdOptionMaxTimeMS,
                             durationCount<Milliseconds>(maxTime));
    return updatedCmdBuilder.obj();
}

}  // unnamed namespace

ShardRemote::ShardRemote(const ShardId& id,
                         const ConnectionString& originalConnString,
                         std::unique_ptr<RemoteCommandTargeter> targeter)
    : Shard(id), _originalConnString(originalConnString), _targeter(targeter.release()) {}

ShardRemote::~ShardRemote() = default;

bool ShardRemote::_isRetriableError(ErrorCodes::Error code, RetryPolicy options) {
    if (options == RetryPolicy::kNoRetry) {
        return false;
    }

    if (options == RetryPolicy::kIdempotent) {
        return kAllRetriableErrors.count(code);
    } else {
        invariant(options == RetryPolicy::kNotIdempotent);
        return kNotMasterErrors.count(code);
    }
}

const ConnectionString ShardRemote::getConnString() const {
    return _targeter->connectionString();
}

void ShardRemote::updateReplSetMonitor(const HostAndPort& remoteHost,
                                       const Status& remoteCommandStatus) {
    if (remoteCommandStatus.isOK())
        return;

    if (ErrorCodes::isNotMasterError(remoteCommandStatus.code()) ||
        (remoteCommandStatus == ErrorCodes::InterruptedDueToReplStateChange)) {
        _targeter->markHostNotMaster(remoteHost);
    } else if (ErrorCodes::isNetworkError(remoteCommandStatus.code())) {
        _targeter->markHostUnreachable(remoteHost);
    } else if (remoteCommandStatus == ErrorCodes::NotMasterOrSecondary) {
        _targeter->markHostUnreachable(remoteHost);
    } else if (remoteCommandStatus == ErrorCodes::ExceededTimeLimit) {
        _targeter->markHostUnreachable(remoteHost);
    }
}

std::string ShardRemote::toString() const {
    return getId() + ":" + _originalConnString.toString();
}

StatusWith<Shard::CommandResponse> ShardRemote::_runCommand(OperationContext* txn,
                                                            const ReadPreferenceSetting& readPref,
                                                            const string& dbName,
                                                            const BSONObj& cmdObj,
                                                            const BSONObj& metadata) {
    const BSONObj cmdWithMaxTimeMS =
        (isConfig() ? appendMaxTimeToCmdObj(txn->getRemainingMaxTimeMicros(), cmdObj) : cmdObj);

    const auto host =
        _targeter->findHost(readPref, RemoteCommandTargeter::selectFindHostMaxWaitTime(txn));
    if (!host.isOK()) {
        return host.getStatus();
    }

    RemoteCommandRequest request(host.getValue(),
                                 dbName,
                                 cmdWithMaxTimeMS,
                                 metadata,
                                 isConfig() ? kConfigCommandTimeout
                                            : executor::RemoteCommandRequest::kNoTimeout);
    StatusWith<RemoteCommandResponse> swResponse =
        Status(ErrorCodes::InternalError, "Internal error running command");

    TaskExecutor* executor = Grid::get(txn)->getExecutorPool()->getFixedExecutor();
    auto callStatus = executor->scheduleRemoteCommand(
        request,
        [&swResponse](const RemoteCommandCallbackArgs& args) { swResponse = args.response; });
    if (!callStatus.isOK()) {
        return callStatus.getStatus();
    }

    // Block until the command is carried out
    executor->wait(callStatus.getValue());

    updateReplSetMonitor(host.getValue(), swResponse.getStatus());

    if (!swResponse.isOK()) {
        if (swResponse.getStatus().compareCode(ErrorCodes::ExceededTimeLimit)) {
            LOG(0) << "Operation timed out with status " << swResponse.getStatus();
        }
        return swResponse.getStatus();
    }

    BSONObj responseObj = swResponse.getValue().data.getOwned();
    BSONObj responseMetadata = swResponse.getValue().metadata.getOwned();
    Status commandStatus = getStatusFromCommandResult(responseObj);
    Status writeConcernStatus = kInternalErrorStatus;
    if (commandStatus.isOK()) {
        writeConcernStatus = getWriteConcernStatusFromCommandResult(responseObj);
    }
    updateReplSetMonitor(host.getValue(), commandStatus);

    return CommandResponse(std::move(responseObj),
                           std::move(responseMetadata),
                           std::move(commandStatus),
                           std::move(writeConcernStatus));
}

StatusWith<Shard::QueryResponse> ShardRemote::_exhaustiveFindOnConfig(
    OperationContext* txn,
    const ReadPreferenceSetting& readPref,
    const NamespaceString& nss,
    const BSONObj& query,
    const BSONObj& sort,
    boost::optional<long long> limit) {
    // Do not allow exhaustive finds to be run against regular shards.
    invariant(getId() == "config");

    const auto host =
        _targeter->findHost(readPref, RemoteCommandTargeter::selectFindHostMaxWaitTime(txn));
    if (!host.isOK()) {
        return host.getStatus();
    }

    QueryResponse response;

    // If for some reason the callback never gets invoked, we will return this status in response.
    Status status = Status(ErrorCodes::InternalError, "Internal error running find command");

    auto fetcherCallback = [this, &status, &response](
        const Fetcher::QueryResponseStatus& dataStatus, Fetcher::NextAction* nextAction) {

        // Throw out any accumulated results on error
        if (!dataStatus.isOK()) {
            status = dataStatus.getStatus();
            response.docs.clear();
            return;
        }

        auto& data = dataStatus.getValue();
        if (data.otherFields.metadata.hasField(rpc::kReplSetMetadataFieldName)) {
            auto replParseStatus =
                rpc::ReplSetMetadata::readFromMetadata(data.otherFields.metadata);

            if (!replParseStatus.isOK()) {
                status = replParseStatus.getStatus();
                response.docs.clear();
                return;
            }

            response.opTime = replParseStatus.getValue().getLastOpVisible();

            // We return the config opTime that was returned for this particular request, but as a
            // safeguard we ensure our global configOpTime is at least as large as it.
            invariant(grid.configOpTime() >= response.opTime);
        }

        for (const BSONObj& doc : data.documents) {
            response.docs.push_back(doc.getOwned());
        }

        status = Status::OK();
    };

    BSONObj readConcernObj;
    {
        const repl::ReadConcernArgs readConcern{grid.configOpTime(),
                                                repl::ReadConcernLevel::kMajorityReadConcern};
        BSONObjBuilder bob;
        readConcern.appendInfo(&bob);
        readConcernObj =
            bob.done().getObjectField(repl::ReadConcernArgs::kReadConcernFieldName).getOwned();
    }

    auto lpq = LiteParsedQuery::makeAsFindCmd(nss,
                                              query,
                                              BSONObj(),  // projection
                                              sort,
                                              BSONObj(),  // hint
                                              readConcernObj,
                                              BSONObj(),    // collation
                                              boost::none,  // skip
                                              limit);

    BSONObjBuilder findCmdBuilder;
    lpq->asFindCommand(&findCmdBuilder);

    Seconds maxTime = kConfigCommandTimeout;
    Microseconds remainingTxnMaxTime(txn->getRemainingMaxTimeMicros());
    if (remainingTxnMaxTime != Microseconds::zero()) {
        maxTime = duration_cast<Seconds>(remainingTxnMaxTime);
    }

    findCmdBuilder.append(LiteParsedQuery::cmdOptionMaxTimeMS,
                          durationCount<Milliseconds>(maxTime));

    QueryFetcher fetcher(Grid::get(txn)->getExecutorPool()->getFixedExecutor(),
                         host.getValue(),
                         nss,
                         findCmdBuilder.done(),
                         fetcherCallback,
                         readPref.pref == ReadPreference::PrimaryOnly ? kReplMetadata
                                                                      : kReplSecondaryOkMetadata,
                         maxTime);
    Status scheduleStatus = fetcher.schedule();
    if (!scheduleStatus.isOK()) {
        return scheduleStatus;
    }

    fetcher.wait();

    updateReplSetMonitor(host.getValue(), status);

    if (!status.isOK()) {
        if (status.compareCode(ErrorCodes::ExceededTimeLimit)) {
            LOG(0) << "Operation timed out with status " << status;
        }
        return status;
    }

    return response;
}

}  // namespace mongo
