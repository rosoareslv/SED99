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

#include "mongo/client/connpool.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/client.h"
#include "mongo/db/commands.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/repl_client_info.h"
#include "mongo/s/catalog/sharding_catalog_client.h"
#include "mongo/s/catalog/type_database.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/commands/cluster_commands_helpers.h"
#include "mongo/s/grid.h"
#include "mongo/s/request_types/move_primary_gen.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

using std::string;

namespace {

const WriteConcernOptions kMajorityWriteConcern(WriteConcernOptions::kMajority,
                                                WriteConcernOptions::SyncMode::UNSET,
                                                Seconds(60));

/**
 * Internal sharding command run on config servers to change a database's primary shard.
 */
class ConfigSvrMovePrimaryCommand : public BasicCommand {
public:
    ConfigSvrMovePrimaryCommand() : BasicCommand("_configsvrMovePrimary") {}

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
                "directly. Reassigns the primary shard of a database.";
    }

    virtual Status checkAuthForCommand(Client* client,
                                       const std::string& dbname,
                                       const BSONObj& cmdObj) override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        return Status::OK();
    }

    virtual std::string parseNs(const std::string& dbname, const BSONObj& cmdObj) const {
        const auto nsElt = cmdObj.firstElement();
        uassert(ErrorCodes::InvalidNamespace,
                "'movePrimary' must be of type String",
                nsElt.type() == BSONType::String);
        return nsElt.str();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname_unused,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {

        if (serverGlobalParams.clusterRole != ClusterRole::ConfigServer) {
            return appendCommandStatus(
                result,
                Status(ErrorCodes::IllegalOperation,
                       "_configsvrMovePrimary can only be run on config servers"));
        }

        auto movePrimaryRequest =
            MovePrimary::parse(IDLParserErrorContext("ConfigSvrMovePrimary"), cmdObj);
        const string dbname = parseNs("", cmdObj);

        uassert(
            ErrorCodes::InvalidNamespace,
            str::stream() << "invalid db name specified: " << dbname,
            NamespaceString::validDBName(dbname, NamespaceString::DollarInDbNameBehavior::Allow));

        if (dbname == NamespaceString::kAdminDb || dbname == NamespaceString::kConfigDb ||
            dbname == NamespaceString::kLocalDb) {
            return appendCommandStatus(
                result,
                {ErrorCodes::InvalidOptions,
                 str::stream() << "Can't move primary for " << dbname << " database"});
        }

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "movePrimary must be called with majority writeConcern, got "
                              << cmdObj,
                opCtx->getWriteConcern().wMode == WriteConcernOptions::kMajority);

        auto const catalogClient = Grid::get(opCtx)->catalogClient();
        auto const catalogCache = Grid::get(opCtx)->catalogCache();
        auto const shardRegistry = Grid::get(opCtx)->shardRegistry();

        // Remove the backwards compatible lock after 3.6 ships.
        auto backwardsCompatibleDbDistLock = uassertStatusOK(
            catalogClient->getDistLockManager()->lock(opCtx,
                                                      dbname + "-movePrimary",
                                                      "movePrimary",
                                                      DistLockManager::kDefaultLockTimeout));
        auto dbDistLock = uassertStatusOK(catalogClient->getDistLockManager()->lock(
            opCtx, dbname, "movePrimary", DistLockManager::kDefaultLockTimeout));

        auto dbType = uassertStatusOK(catalogClient->getDatabase(
                                          opCtx, dbname, repl::ReadConcernLevel::kLocalReadConcern))
                          .value;

        const std::string to = movePrimaryRequest.getTo().toString();

        if (to.empty()) {
            return appendCommandStatus(
                result,
                {ErrorCodes::InvalidOptions,
                 str::stream() << "you have to specify where you want to move it"});
        }

        const auto fromShard = uassertStatusOK(shardRegistry->getShard(opCtx, dbType.getPrimary()));

        const auto toShard = [&]() {
            auto toShardStatus = shardRegistry->getShard(opCtx, to);
            if (!toShardStatus.isOK()) {
                const std::string msg(
                    str::stream() << "Could not move database '" << dbname << "' to shard '" << to
                                  << "' due to "
                                  << toShardStatus.getStatus().reason());
                log() << msg;
                uasserted(toShardStatus.getStatus().code(), msg);
            }

            return toShardStatus.getValue();
        }();

        if (fromShard->getId() == toShard->getId()) {
            // We did a local read of the database entry above and found that this movePrimary
            // request was already satisfied. However, the data may not be majority committed (a
            // previous movePrimary attempt may have failed with a write concern error).
            // Since the current Client doesn't know the opTime of the last write to the database
            // entry, make it wait for the last opTime in the system when we wait for writeConcern.
            repl::ReplClientInfo::forClient(opCtx->getClient()).setLastOpToSystemLastOpTime(opCtx);
            result << "primary" << toShard->toString();
            return true;
        }

        log() << "Moving " << dbname << " primary from: " << fromShard->toString()
              << " to: " << toShard->toString();

        const auto shardedColls = getAllShardedCollectionsForDb(opCtx, dbname);

        // Record start in changelog
        uassertStatusOK(catalogClient->logChange(
            opCtx,
            "movePrimary.start",
            dbname,
            _buildMoveLogEntry(dbname, fromShard->toString(), toShard->toString(), shardedColls),
            ShardingCatalogClient::kMajorityWriteConcern));

        ScopedDbConnection toconn(toShard->getConnString());
        ON_BLOCK_EXIT([&toconn] { toconn.done(); });

        // TODO ERH - we need a clone command which replays operations from clone start to now
        //            can just use local.oplog.$main
        BSONObj cloneRes;
        bool hasWCError = false;

        {
            BSONArrayBuilder barr;
            for (const auto& shardedColl : shardedColls) {
                barr.append(shardedColl.ns());
            }

            const bool worked = toconn->runCommand(
                dbname,
                BSON("clone" << fromShard->getConnString().toString() << "collsToIgnore"
                             << barr.arr()
                             << bypassDocumentValidationCommandOption()
                             << true
                             << "writeConcern"
                             << opCtx->getWriteConcern().toBSON()),
                cloneRes);

            if (!worked) {
                log() << "clone failed" << redact(cloneRes);
                return appendCommandStatus(
                    result, {ErrorCodes::OperationFailed, str::stream() << "clone failed"});
            }

            if (auto wcErrorElem = cloneRes["writeConcernError"]) {
                appendWriteConcernErrorToCmdResponse(toShard->getId(), wcErrorElem, result);
                hasWCError = true;
            }
        }

        // Update the new primary in the config server metadata.
        dbType.setPrimary(toShard->getId());
        uassertStatusOK(catalogClient->updateDatabase(opCtx, dbname, dbType));

        // Ensure the next attempt to retrieve the database or any of its collections will do a full
        // reload
        catalogCache->purgeDatabase(dbname);

        const string oldPrimary = fromShard->getConnString().toString();

        ScopedDbConnection fromconn(fromShard->getConnString());
        ON_BLOCK_EXIT([&fromconn] { fromconn.done(); });

        if (shardedColls.empty()) {
            // TODO: Collections can be created in the meantime, and we should handle in the future.
            log() << "movePrimary dropping database on " << oldPrimary
                  << ", no sharded collections in " << dbname;

            try {
                BSONObj dropDBInfo;
                fromconn->dropDatabase(dbname.c_str(), opCtx->getWriteConcern(), &dropDBInfo);
                if (!hasWCError) {
                    if (auto wcErrorElem = dropDBInfo["writeConcernError"]) {
                        appendWriteConcernErrorToCmdResponse(
                            fromShard->getId(), wcErrorElem, result);
                        hasWCError = true;
                    }
                }
            } catch (DBException& e) {
                e.addContext(str::stream() << "movePrimary could not drop the database " << dbname
                                           << " on "
                                           << oldPrimary);
                throw;
            }

        } else if (cloneRes["clonedColls"].type() != Array) {
            // Legacy behavior from old mongod with sharded collections, *do not* delete
            // database, but inform user they can drop manually (or ignore).
            warning() << "movePrimary legacy mongod behavior detected. "
                      << "User must manually remove unsharded collections in database " << dbname
                      << " on " << oldPrimary;
        } else {
            // We moved some unsharded collections, but not all
            BSONObjIterator it(cloneRes["clonedColls"].Obj());

            while (it.more()) {
                BSONElement el = it.next();
                if (el.type() == String) {
                    try {
                        log() << "movePrimary dropping cloned collection " << el.String() << " on "
                              << oldPrimary;
                        BSONObj dropCollInfo;
                        fromconn->dropCollection(
                            el.String(), opCtx->getWriteConcern(), &dropCollInfo);
                        if (!hasWCError) {
                            if (auto wcErrorElem = dropCollInfo["writeConcernError"]) {
                                appendWriteConcernErrorToCmdResponse(
                                    fromShard->getId(), wcErrorElem, result);
                                hasWCError = true;
                            }
                        }

                    } catch (DBException& e) {
                        e.addContext(str::stream()
                                     << "movePrimary could not drop the cloned collection "
                                     << el.String()
                                     << " on "
                                     << oldPrimary);
                        throw;
                    }
                }
            }
        }

        result << "primary" << toShard->toString();

        // Record finish in changelog
        uassertStatusOK(catalogClient->logChange(
            opCtx,
            "movePrimary",
            dbname,
            _buildMoveLogEntry(dbname, oldPrimary, toShard->toString(), shardedColls),
            ShardingCatalogClient::kMajorityWriteConcern));

        return true;
    }

private:
    static BSONObj _buildMoveLogEntry(const std::string& db,
                                      const std::string& from,
                                      const std::string& to,
                                      const std::vector<NamespaceString>& shardedColls) {
        BSONObjBuilder details;
        details.append("database", db);
        details.append("from", from);
        details.append("to", to);

        BSONArrayBuilder collB(details.subarrayStart("shardedCollections"));
        for (const auto& shardedColl : shardedColls) {
            collB.append(shardedColl.ns());
        }
        collB.done();

        return details.obj();
    }

} configsvrMovePrimaryCmd;

}  // namespace
}  // namespace mongo
