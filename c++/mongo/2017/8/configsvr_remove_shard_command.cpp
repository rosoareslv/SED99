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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include <set>

#include "mongo/db/audit.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/client.h"
#include "mongo/db/commands.h"
#include "mongo/db/operation_context.h"
#include "mongo/s/catalog/sharding_catalog_manager.h"
#include "mongo/s/catalog/type_database.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/grid.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {

/**
 * Internal sharding command run on config servers to remove a shard from the cluster.
 */
class ConfigSvrRemoveShardCommand : public BasicCommand {
public:
    ConfigSvrRemoveShardCommand() : BasicCommand("_configsvrRemoveShard") {}

    virtual bool slaveOk() const {
        return false;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    virtual void help(std::stringstream& help) const override {
        help << "Internal command, which is exported by the sharding config server. Do not call "
                "directly. Removes a shard from the cluster.";
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {

        uassert(ErrorCodes::IllegalOperation,
                "_configsvrRemoveShard can only be run on config servers",
                serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

        uassert(ErrorCodes::TypeMismatch,
                str::stream() << "Field '" << cmdObj.firstElement().fieldName()
                              << "' must be of type string",
                cmdObj.firstElement().type() == BSONType::String);
        const std::string target = cmdObj.firstElement().str();

        const auto shardStatus = grid.shardRegistry()->getShard(opCtx, ShardId(target));
        if (!shardStatus.isOK()) {
            std::string msg(str::stream() << "Could not drop shard '" << target
                                          << "' because it does not exist");
            log() << msg;
            return appendCommandStatus(result, Status(ErrorCodes::ShardNotFound, msg));
        }
        const auto shard = shardStatus.getValue();

        const auto shardingCatalogManager = ShardingCatalogManager::get(opCtx);

        StatusWith<ShardDrainingStatus> removeShardResult =
            shardingCatalogManager->removeShard(opCtx, shard->getId());
        if (!removeShardResult.isOK()) {
            return appendCommandStatus(result, removeShardResult.getStatus());
        }

        std::vector<std::string> databases;
        Status status =
            shardingCatalogManager->getDatabasesForShard(opCtx, shard->getId(), &databases);
        if (!status.isOK()) {
            return appendCommandStatus(result, status);
        }

        // Get BSONObj containing:
        // 1) note about moving or dropping databases in a shard
        // 2) list of databases (excluding 'local' database) that need to be moved
        BSONObj dbInfo;
        {
            BSONObjBuilder dbInfoBuilder;
            dbInfoBuilder.append("note", "you need to drop or movePrimary these databases");
            BSONArrayBuilder dbs(dbInfoBuilder.subarrayStart("dbsToMove"));
            for (std::vector<std::string>::const_iterator it = databases.begin();
                 it != databases.end();
                 it++) {
                if (*it != "local") {
                    dbs.append(*it);
                }
            }
            dbs.doneFast();
            dbInfo = dbInfoBuilder.obj();
        }

        // TODO: Standardize/Seperate how we append to the result object
        switch (removeShardResult.getValue()) {
            case ShardDrainingStatus::STARTED:
                result.append("msg", "draining started successfully");
                result.append("state", "started");
                result.append("shard", shard->getId().toString());
                result.appendElements(dbInfo);
                break;
            case ShardDrainingStatus::ONGOING: {
                std::vector<ChunkType> chunks;
                Status status = Grid::get(opCtx)->catalogClient()->getChunks(
                    opCtx,
                    BSON(ChunkType::shard(shard->getId().toString())),
                    BSONObj(),
                    boost::none,  // return all
                    &chunks,
                    nullptr,
                    repl::ReadConcernLevel::kMajorityReadConcern);
                if (!status.isOK()) {
                    return appendCommandStatus(result, status);
                }

                result.append("msg", "draining ongoing");
                result.append("state", "ongoing");
                {
                    BSONObjBuilder inner;
                    inner.append("chunks", static_cast<long long>(chunks.size()));
                    inner.append("dbs", static_cast<long long>(databases.size()));
                    BSONObj b = inner.obj();
                    result.append("remaining", b);
                }
                result.appendElements(dbInfo);
                break;
            }
            case ShardDrainingStatus::COMPLETED:
                result.append("msg", "removeshard completed successfully");
                result.append("state", "completed");
                result.append("shard", shard->getId().toString());
        }

        return true;
    }

} configsvrRemoveShardCmd;

}  // namespace
}  // namespace mongo
