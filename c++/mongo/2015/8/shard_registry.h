/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#pragma once

#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <vector>

#include "mongo/base/disallow_copying.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/read_concern_args.h"
#include "mongo/s/client/shard.h"
#include "mongo/stdx/mutex.h"

namespace mongo {

class BSONObjBuilder;
class CatalogManager;
struct HostAndPort;
class NamespaceString;
class OperationContext;
class RemoteCommandTargeterFactory;
class Shard;
class ShardType;

template <typename T>
class StatusWith;

namespace executor {

class NetworkInterface;
class TaskExecutor;

}  // namespace executor

/**
 * Maintains the set of all shards known to the instance and their connections. Manages polling
 * the respective replica sets for membership changes.
 */
class ShardRegistry {
    MONGO_DISALLOW_COPYING(ShardRegistry);

public:
    struct CommandResponse {
        BSONObj response;
        repl::OpTime opTime;
    };

    struct QueryResponse {
        std::vector<BSONObj> docs;
        repl::OpTime opTime;
    };

    /**
     * Instantiates a new shard registry.
     *
     * @param targeterFactory Produces targeters for each shard's individual connection string
     * @param commandRunner Command runner for executing commands against hosts
     * @param executor Asynchronous task executor to use for making calls to shards.
     * @param network Network interface backing executor.
     * @param configServerCS ConnectionString used for communicating with the config servers
     */
    ShardRegistry(std::unique_ptr<RemoteCommandTargeterFactory> targeterFactory,
                  std::unique_ptr<executor::TaskExecutor> executor,
                  executor::NetworkInterface* network,
                  ConnectionString configServerCS);

    ~ShardRegistry();

    /**
     * Invoked when the connection string for the config server changes. Updates the config server
     * connection string and recreates the config server's shard.
     */
    void updateConfigServerConnectionString(ConnectionString configServerCS);

    /**
     * Invokes the executor's startup method, which will start any networking/async execution
     * threads.
     */
    void startup();

    /**
     * Stops the executor thread and waits for it to join.
     */
    void shutdown();

    executor::TaskExecutor* getExecutor() const {
        return _executor.get();
    }

    executor::NetworkInterface* getNetwork() const {
        return _network;
    }

    ConnectionString getConfigServerConnectionString() const {
        return _configServerCS;
    }

    void reload(OperationContext* txn);

    /**
     * Returns shared pointer to shard object with given shard id.
     */
    std::shared_ptr<Shard> getShard(OperationContext* txn, const ShardId& shardId);

    /**
     * Returns shared pointer to the shard object representing the config servers.
     */
    std::shared_ptr<Shard> getConfigShard();

    /**
     * Instantiates a new detached shard connection, which does not appear in the list of shards
     * tracked by the registry and as a result will not be returned by getAllShardIds.
     *
     * The caller owns the returned shard object and is responsible for disposing of it when done.
     *
     * @param connStr Connection string to the shard.
     */
    std::unique_ptr<Shard> createConnection(const ConnectionString& connStr) const;

    /**
     * Lookup shard by replica set name. Returns nullptr if the name can't be found.
     * Note: this doesn't refresh the table if the name isn't found, so it's possible that a
     * newly added shard/Replica Set may not be found.
     */
    std::shared_ptr<Shard> lookupRSName(const std::string& name) const;

    void remove(const ShardId& id);

    void getAllShardIds(std::vector<ShardId>* all) const;

    void toBSON(BSONObjBuilder* result);

    /**
     * Executes 'find' command against the specified host and fetches *all* the results that
     * the host will return until there are no more or until an error is returned.
     *
     * Returns either the complete set of results or an error, never partial results.
     *
     * Note: should never be used outside of CatalogManagerReplicaSet or DistLockCatalogImpl.
     */
    StatusWith<QueryResponse> exhaustiveFind(const HostAndPort& host,
                                             const NamespaceString& nss,
                                             const BSONObj& query,
                                             const BSONObj& sort,
                                             boost::optional<long long> limit,
                                             boost::optional<repl::ReadConcernArgs> readConcern,
                                             const BSONObj& metadata);

    /**
     * Runs a command against the specified host and returns the result.  It is the responsibility
     * of the caller to check the returned BSON for command-specific failures.
     */
    StatusWith<CommandResponse> runCommandWithMetadata(const HostAndPort& host,
                                                       const std::string& dbName,
                                                       const BSONObj& cmdObj,
                                                       const BSONObj& metadata);

    /**
     * Runs a command against the specified host and returns the result.
     */
    StatusWith<BSONObj> runCommand(const HostAndPort& host,
                                   const std::string& dbName,
                                   const BSONObj& cmdObj);

    /**
     * Helper for running commands against a given shard with logic for retargeting and
     * retrying the command in the event of a NotMaster response.
     * Returns ErrorCodes::NotMaster if after the max number of retries we still haven't
     * successfully delivered the command to a primary.  Can also return a non-ok status in the
     * event of a network error communicating with the shard.  If we are able to get
     * a valid response from running the command then we will return it, even if the command
     * response indicates failure.  Thus the caller is responsible for checking the command
     * response object for any kind of command-specific failure.  The only exception is
     * NotMaster errors, which we intercept and follow the rules described above for handling.
     */
    StatusWith<BSONObj> runCommandWithNotMasterRetries(OperationContext* txn,
                                                       const ShardId& shard,
                                                       const std::string& dbname,
                                                       const BSONObj& cmdObj);

    StatusWith<CommandResponse> runCommandWithNotMasterRetries(OperationContext* txn,
                                                               const ShardId& shardId,
                                                               const std::string& dbname,
                                                               const BSONObj& cmdObj,
                                                               const BSONObj& metadata);

    StatusWith<BSONObj> runCommandOnConfigWithNotMasterRetries(const std::string& dbname,
                                                               const BSONObj& cmdObj);

    StatusWith<CommandResponse> runCommandOnConfigWithNotMasterRetries(const std::string& dbname,
                                                                       const BSONObj& cmdObj,
                                                                       const BSONObj& metadata);

private:
    typedef std::map<ShardId, std::shared_ptr<Shard>> ShardMap;

    /**
     * Creates a shard based on the specified information and puts it into the lookup maps.
     */
    void _addShard_inlock(const ShardType& shardType);

    /**
     * Adds the "config" shard (representing the config server) to the shard registry.
     */
    void _addConfigShard_inlock();

    std::shared_ptr<Shard> _findUsingLookUp(const ShardId& shardId);

    StatusWith<CommandResponse> _runCommandWithNotMasterRetries(RemoteCommandTargeter* targeter,
                                                                const std::string& dbname,
                                                                const BSONObj& cmdObj,
                                                                const BSONObj& metadata);

    // Factory to obtain remote command targeters for shards
    const std::unique_ptr<RemoteCommandTargeterFactory> _targeterFactory;

    // Executor for scheduling work and remote commands to shards that run in an asynchronous
    // manner.
    const std::unique_ptr<executor::TaskExecutor> _executor;

    // Network interface being used by _executor.  Used for asking questions about the network
    // configuration, such as getting the current server's hostname.
    executor::NetworkInterface* const _network;

    // Protects the config server connections string and the lookup maps below
    mutable stdx::mutex _mutex;

    // Config server connection string
    ConnectionString _configServerCS;

    // Map of both shardName -> Shard and hostName -> Shard
    ShardMap _lookup;

    // Map from replica set name to shard corresponding to this replica set
    ShardMap _rsLookup;
};

}  // namespace mongo
