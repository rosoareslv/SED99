/**
 * Copyright (C) 2016 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/client/shard.h"

#include "mongo/util/log.h"

namespace mongo {

namespace {

const int kOnErrorNumRetries = 3;

Status _getEffectiveCommandStatus(StatusWith<Shard::CommandResponse> cmdResponse) {
    // Make sure the command even received a valid response
    if (!cmdResponse.isOK()) {
        return cmdResponse.getStatus();
    }

    // If the request reached the shard, check if the command itself failed.
    if (!cmdResponse.getValue().commandStatus.isOK()) {
        return cmdResponse.getValue().commandStatus;
    }

    // Finally check if the write concern failed
    if (!cmdResponse.getValue().writeConcernStatus.isOK()) {
        return cmdResponse.getValue().writeConcernStatus;
    }

    return Status::OK();
}

}  // namespace

Shard::Shard(const ShardId& id) : _id(id) {}

const ShardId Shard::getId() const {
    return _id;
}

bool Shard::isConfig() const {
    return _id == "config";
}

StatusWith<Shard::CommandResponse> Shard::runCommand(OperationContext* txn,
                                                     const ReadPreferenceSetting& readPref,
                                                     const std::string& dbName,
                                                     const BSONObj& cmdObj,
                                                     const BSONObj& metadata,
                                                     RetryPolicy retryPolicy) {
    for (int retry = 1; retry <= kOnErrorNumRetries; ++retry) {
        auto swCmdResponse = _runCommand(txn, readPref, dbName, cmdObj, metadata);
        auto commandStatus = _getEffectiveCommandStatus(swCmdResponse);

        if (retry < kOnErrorNumRetries && _isRetriableError(commandStatus.code(), retryPolicy)) {
            LOG(3) << "Command " << cmdObj << " failed with retriable error and will be retried"
                   << causedBy(commandStatus);
            continue;
        }

        return swCmdResponse;
    }
    MONGO_UNREACHABLE;
}

StatusWith<Shard::QueryResponse> Shard::exhaustiveFindOnConfig(
    OperationContext* txn,
    const ReadPreferenceSetting& readPref,
    const NamespaceString& nss,
    const BSONObj& query,
    const BSONObj& sort,
    const boost::optional<long long> limit) {
    for (int retry = 1; retry <= kOnErrorNumRetries; retry++) {
        auto result = _exhaustiveFindOnConfig(txn, readPref, nss, query, sort, limit);

        if (retry < kOnErrorNumRetries &&
            _isRetriableError(result.getStatus().code(), RetryPolicy::kIdempotent)) {
            continue;
        }

        return result;
    }

    MONGO_UNREACHABLE;
}

}  // namespace mongo
