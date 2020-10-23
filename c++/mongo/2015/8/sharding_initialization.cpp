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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/sharding_initialization.h"

#include <string>

#include "mongo/base/status.h"
#include "mongo/client/remote_command_targeter_factory_impl.h"
#include "mongo/client/syncclusterconnection.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/task_executor.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/db/server_options.h"
#include "mongo/db/service_context.h"
#include "mongo/s/catalog/forwarding_catalog_manager.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/client/sharding_network_connection_hook.h"
#include "mongo/s/grid.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/sock.h"

namespace mongo {

namespace {

using executor::NetworkInterface;
using executor::ThreadPoolTaskExecutor;

std::unique_ptr<ThreadPoolTaskExecutor> makeTaskExecutor(std::unique_ptr<NetworkInterface> net) {
    ThreadPool::Options tpOptions;
    tpOptions.poolName = "ShardWork";
    return stdx::make_unique<ThreadPoolTaskExecutor>(stdx::make_unique<ThreadPool>(tpOptions),
                                                     std::move(net));
}

}  // namespace

Status initializeGlobalShardingState(OperationContext* txn,
                                     const ConnectionString& configCS,
                                     bool allowNetworking) {
    SyncClusterConnection::setConnectionValidationHook(
        [](const HostAndPort& target, const executor::RemoteCommandResponse& isMasterReply) {
            return ShardingNetworkConnectionHook::validateHostImpl(target, isMasterReply);
        });
    auto network =
        executor::makeNetworkInterface(stdx::make_unique<ShardingNetworkConnectionHook>());
    auto networkPtr = network.get();
    auto shardRegistry(
        stdx::make_unique<ShardRegistry>(stdx::make_unique<RemoteCommandTargeterFactoryImpl>(),
                                         makeTaskExecutor(std::move(network)),
                                         networkPtr,
                                         configCS));

    std::unique_ptr<ForwardingCatalogManager> catalogManager;
    try {
        catalogManager = stdx::make_unique<ForwardingCatalogManager>(
            getGlobalServiceContext(),
            configCS,
            shardRegistry.get(),
            HostAndPort(getHostName(), serverGlobalParams.port));
    } catch (const DBException& ex) {
        return ex.toStatus();
    }

    shardRegistry->startup();
    grid.init(std::move(catalogManager),
              std::move(shardRegistry),
              stdx::make_unique<ClusterCursorManager>(getGlobalServiceContext()->getClockSource()));

    auto status = grid.catalogManager()->startup(txn, allowNetworking);
    if (!status.isOK()) {
        return status;
    }

    return Status::OK();
}

}  // namespace mongo
