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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/sharding_initialization.h"

#include <memory>
#include <string>

#include "mongo/base/status.h"
#include "mongo/db/audit.h"
#include "mongo/db/keys_collection_client_sharded.h"
#include "mongo/db/keys_collection_manager.h"
#include "mongo/db/logical_clock.h"
#include "mongo/db/logical_time_validator.h"
#include "mongo/db/server_options.h"
#include "mongo/db/service_context.h"
#include "mongo/db/time_proof_service.h"
#include "mongo/executor/connection_pool.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/network_interface_thread_pool.h"
#include "mongo/executor/task_executor.h"
#include "mongo/executor/task_executor_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/rpc/metadata/config_server_metadata.h"
#include "mongo/rpc/metadata/metadata_hook.h"
#include "mongo/s/balancer_configuration.h"
#include "mongo/s/catalog/dist_lock_catalog_impl.h"
#include "mongo/s/catalog/replset_dist_lock_manager.h"
#include "mongo/s/catalog/sharding_catalog_client_impl.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/client/shard_factory.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/client/sharding_network_connection_hook.h"
#include "mongo/s/cluster_identity_loader.h"
#include "mongo/s/grid.h"
#include "mongo/s/query/cluster_cursor_manager.h"
#include "mongo/s/sharding_task_executor.h"
#include "mongo/s/sharding_task_executor_pool_controller.h"
#include "mongo/s/sharding_task_executor_pool_gen.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/net/socket_utils.h"
#include "mongo/util/str.h"

namespace mongo {

namespace {

using executor::ConnectionPool;
using executor::NetworkInterface;
using executor::NetworkInterfaceThreadPool;
using executor::TaskExecutorPool;
using executor::ThreadPoolTaskExecutor;

static constexpr auto kRetryInterval = Seconds{2};

std::unique_ptr<ShardingCatalogClient> makeCatalogClient(ServiceContext* service,
                                                         StringData distLockProcessId) {
    auto distLockCatalog = std::make_unique<DistLockCatalogImpl>();
    auto distLockManager =
        std::make_unique<ReplSetDistLockManager>(service,
                                                 distLockProcessId,
                                                 std::move(distLockCatalog),
                                                 ReplSetDistLockManager::kDistLockPingInterval,
                                                 ReplSetDistLockManager::kDistLockExpirationTime);

    return std::make_unique<ShardingCatalogClientImpl>(std::move(distLockManager));
}

std::shared_ptr<executor::TaskExecutor> makeShardingFixedTaskExecutor(
    std::unique_ptr<NetworkInterface> net) {
    auto executor = std::make_unique<ThreadPoolTaskExecutor>(
        std::make_unique<ThreadPool>([] {
            ThreadPool::Options opts;
            opts.poolName = "Sharding-Fixed";

            const auto maxThreads = stdx::thread::hardware_concurrency();
            opts.maxThreads = maxThreads == 0 ? 16 : 2 * maxThreads;
            return opts;
        }()),
        std::move(net));

    return std::make_shared<executor::ShardingTaskExecutor>(std::move(executor));
}

std::unique_ptr<TaskExecutorPool> makeShardingTaskExecutorPool(
    std::unique_ptr<NetworkInterface> fixedNet,
    rpc::ShardingEgressMetadataHookBuilder metadataHookBuilder,
    ConnectionPool::Options connPoolOptions,
    boost::optional<size_t> taskExecutorPoolSize) {
    std::vector<std::shared_ptr<executor::TaskExecutor>> executors;

    const auto poolSize = taskExecutorPoolSize.value_or(TaskExecutorPool::getSuggestedPoolSize());

    for (size_t i = 0; i < poolSize; ++i) {
        auto exec = makeShardingTaskExecutor(
            executor::makeNetworkInterface("TaskExecutorPool-" + std::to_string(i),
                                           std::make_unique<ShardingNetworkConnectionHook>(),
                                           metadataHookBuilder(),
                                           connPoolOptions));

        executors.emplace_back(std::move(exec));
    }

    // Add executor used to perform non-performance critical work.
    auto fixedExec = makeShardingFixedTaskExecutor(std::move(fixedNet));

    auto executorPool = std::make_unique<TaskExecutorPool>();
    executorPool->addExecutors(std::move(executors), std::move(fixedExec));
    return executorPool;
}

}  // namespace

std::unique_ptr<executor::TaskExecutor> makeShardingTaskExecutor(
    std::unique_ptr<NetworkInterface> net) {
    auto netPtr = net.get();
    auto executor = std::make_unique<ThreadPoolTaskExecutor>(
        std::make_unique<NetworkInterfaceThreadPool>(netPtr), std::move(net));

    return std::make_unique<executor::ShardingTaskExecutor>(std::move(executor));
}

std::string generateDistLockProcessId(OperationContext* opCtx) {
    std::unique_ptr<SecureRandom> rng(SecureRandom::create());

    return str::stream()
        << HostAndPort(getHostName(), serverGlobalParams.port).toString() << ':'
        << durationCount<Seconds>(
               opCtx->getServiceContext()->getPreciseClockSource()->now().toDurationSinceEpoch())
        << ':' << rng->nextInt64();
}

Status initializeGlobalShardingState(OperationContext* opCtx,
                                     const ConnectionString& configCS,
                                     StringData distLockProcessId,
                                     std::unique_ptr<ShardFactory> shardFactory,
                                     std::unique_ptr<CatalogCache> catalogCache,
                                     rpc::ShardingEgressMetadataHookBuilder hookBuilder,
                                     boost::optional<size_t> taskExecutorPoolSize) {
    if (configCS.type() == ConnectionString::INVALID) {
        return {ErrorCodes::BadValue, "Unrecognized connection string."};
    }

    ConnectionPool::Options connPoolOptions;
    connPoolOptions.controller = std::make_shared<ShardingTaskExecutorPoolController>();

    auto network = executor::makeNetworkInterface(
        "ShardRegistry", std::make_unique<ShardingNetworkConnectionHook>(), hookBuilder());
    auto networkPtr = network.get();
    auto executorPool = makeShardingTaskExecutorPool(
        std::move(network), hookBuilder, connPoolOptions, taskExecutorPoolSize);
    executorPool->startup();

    const auto service = opCtx->getServiceContext();
    auto const grid = Grid::get(service);

    grid->init(makeCatalogClient(service, distLockProcessId),
               std::move(catalogCache),
               std::make_unique<ShardRegistry>(std::move(shardFactory), configCS),
               std::make_unique<ClusterCursorManager>(service->getPreciseClockSource()),
               std::make_unique<BalancerConfiguration>(),
               std::move(executorPool),
               networkPtr);

    // The shard registry must be started once the grid is initialized
    grid->shardRegistry()->startup(opCtx);

    // The catalog client must be started after the shard registry has been started up
    grid->catalogClient()->startup();

    auto keysCollectionClient =
        std::make_unique<KeysCollectionClientSharded>(grid->catalogClient());
    auto keyManager =
        std::make_shared<KeysCollectionManager>(KeysCollectionManager::kKeyManagerPurposeString,
                                                std::move(keysCollectionClient),
                                                Seconds(KeysRotationIntervalSec));
    keyManager->startMonitoring(service);

    LogicalTimeValidator::set(service, std::make_unique<LogicalTimeValidator>(keyManager));

    return Status::OK();
}

Status waitForShardRegistryReload(OperationContext* opCtx) {
    if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer) {
        return Status::OK();
    }

    while (!globalInShutdownDeprecated()) {
        auto stopStatus = opCtx->checkForInterruptNoAssert();
        if (!stopStatus.isOK()) {
            return stopStatus;
        }

        try {
            uassertStatusOK(ClusterIdentityLoader::get(opCtx)->loadClusterId(
                opCtx, repl::ReadConcernLevel::kMajorityReadConcern));
            if (Grid::get(opCtx)->shardRegistry()->isUp()) {
                return Status::OK();
            }
            sleepFor(kRetryInterval);
            continue;
        } catch (const DBException& ex) {
            Status status = ex.toStatus();
            warning()
                << "Error initializing sharding state, sleeping for 2 seconds and trying again"
                << causedBy(status);
            sleepFor(kRetryInterval);
            continue;
        }
    }

    return {ErrorCodes::ShutdownInProgress, "aborting shard loading attempt"};
}

}  // namespace mongo
