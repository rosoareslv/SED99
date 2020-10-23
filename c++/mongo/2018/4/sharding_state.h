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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <string>
#include <vector>

#include "mongo/base/disallow_copying.h"
#include "mongo/bson/oid.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"

namespace mongo {

class BSONObj;
class BSONObjBuilder;
class ConnectionString;
class OperationContext;
class ServiceContext;
class ShardIdentityType;
class Status;

namespace repl {
class OpTime;
}  // namespace repl

/**
 * Contains the global sharding state for a running mongod. There is one instance of this object per
 * service context and it is never destroyed for the lifetime of the context.
 */
class ShardingState {
    MONGO_DISALLOW_COPYING(ShardingState);

public:
    using GlobalInitFunc =
        stdx::function<Status(OperationContext*, const ConnectionString&, StringData)>;

    ShardingState();
    ~ShardingState();

    /**
     * Retrieves the sharding state object associated with the specified service context. This
     * method must only be called if ShardingState decoration has been created on the service
     * context, otherwise it will fassert. In other words, it may only be called on MongoD and
     * tests, which specifically require and instantiate ShardingState.
     *
     * Returns the instance's ShardingState.
     */
    static ShardingState* get(ServiceContext* serviceContext);
    static ShardingState* get(OperationContext* operationContext);

    /**
     * Returns true if ShardingState has been successfully initialized.
     *
     * Code that needs to perform extra actions if sharding is initialized, but does not need to
     * error if not, should use this. Alternatively, see ShardingState::canAcceptShardedCommands().
     */
    bool enabled() const;

    /**
     * Force-sets the initialization state to InitializationState::kInitialized, for testing
     * purposes. Note that this function should ONLY be used for testing purposes.
     */
    void setEnabledForTest(const std::string& shardName);

    /**
     * Returns Status::OK if the ShardingState is enabled; if not, returns an error describing
     * whether the ShardingState is just not yet initialized, or if this shard is not running with
     * --shardsvr at all.
     *
     * Code that should error if sharding state has not been initialized should use this to report
     * a more descriptive error. Alternatively, see ShardingState::enabled().
     */
    Status canAcceptShardedCommands() const;

    std::string getShardName();

    /**
     * Initializes the sharding state of this server from the shard identity document argument
     * and sets secondary or primary state information on the catalog cache loader.
     *
     * Note: caller must hold a global/database lock! Needed in order to stably check for
     * replica set state (primary, secondary, standalone).
     */
    Status initializeFromShardIdentity(OperationContext* opCtx,
                                       const ShardIdentityType& shardIdentity);

    /**
     * Shuts down sharding machinery on the shard.
     */
    void shutDown(OperationContext* opCtx);

    /**
     * Updates the ShardRegistry's stored notion of the config server optime based on the
     * ConfigServerMetadata decoration attached to the OperationContext.
     */
    Status updateConfigServerOpTimeFromMetadata(OperationContext* opCtx);

    void appendInfo(OperationContext* opCtx, BSONObjBuilder& b);

    bool needCollectionMetadata(OperationContext* opCtx, const std::string& ns);

    /**
     * Updates the config server field of the shardIdentity document with the given connection
     * string.
     *
     * Note: this can return NotMaster error.
     */
    Status updateShardIdentityConfigString(OperationContext* opCtx,
                                           const std::string& newConnectionString);

    /**
     * For testing only. Mock the initialization method used by initializeFromConfigConnString and
     * initializeFromShardIdentity after all checks are performed.
     */
    void setGlobalInitMethodForTest(GlobalInitFunc func);

    /**
     * If started with --shardsvr, initializes sharding awareness from the shardIdentity document
     * on disk, if there is one.
     * If started with --shardsvr in queryableBackupMode, initializes sharding awareness from the
     * shardIdentity document passed through the --overrideShardIdentity startup parameter.
     *
     * If returns true, the ShardingState::_globalInit method was called, meaning all the core
     * classes for sharding were initialized, but no networking calls were made yet (with the
     * exception of the duplicate ShardRegistry reload in ShardRegistry::startup() (see
     * SERVER-26123). Outgoing networking calls to cluster members can now be made.
     *
     * Note: this function briefly takes the global lock to determine primary/secondary state.
     */
    StatusWith<bool> initializeShardingAwarenessIfNeeded(OperationContext* opCtx);

private:
    // Progress of the sharding state initialization
    enum class InitializationState : uint32_t {
        // Initial state. The server must be under exclusive lock when this state is entered. No
        // metadata is available yet and it is not known whether there is any min optime metadata,
        // which needs to be recovered. From this state, the server may enter INITIALIZING, if a
        // recovey document is found or stay in it until initialize has been called.
        kNew,

        // Sharding state is fully usable.
        kInitialized,

        // Some initialization error occurred. The _initializationStatus variable will contain the
        // error.
        kError,
    };

    /**
     * Returns the initialization state.
     */
    InitializationState _getInitializationState() const;

    /**
     * Updates the initialization state.
     */
    void _setInitializationState(InitializationState newState);

    // Protects state below
    stdx::mutex _mutex;

    // State of the initialization of the sharding state along with any potential errors
    AtomicUInt32 _initializationState;

    // Only valid if _initializationState is kError. Contains the reason for initialization failure.
    Status _initializationStatus;

    // Signaled when ::initialize finishes.
    stdx::condition_variable _initializationFinishedCondition;

    // Sets the shard name for this host (comes through setShardVersion)
    std::string _shardName;

    // The id for the cluster this shard belongs to.
    OID _clusterId;

    // Function for initializing the external sharding state components not owned here.
    GlobalInitFunc _globalInit;
};

}  // namespace mongo
