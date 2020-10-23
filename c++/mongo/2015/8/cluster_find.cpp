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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/s/query/cluster_find.h"

#include <set>
#include <vector>

#include "mongo/base/status_with.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/remote_command_targeter.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/query/find_common.h"
#include "mongo/db/query/getmore_request.h"
#include "mongo/s/catalog/catalog_cache.h"
#include "mongo/s/chunk_manager.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/config.h"
#include "mongo/s/grid.h"
#include "mongo/s/query/cluster_client_cursor_impl.h"
#include "mongo/s/query/cluster_cursor_manager.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"

namespace mongo {

namespace {

/**
 * Given the LiteParsedQuery 'lpq' being executed by mongos, returns a copy of the query which is
 * suitable for forwarding to the targeted hosts.
 */
std::unique_ptr<LiteParsedQuery> transformQueryForShards(const LiteParsedQuery& lpq) {
    // If there is a limit, we forward the sum of the limit and the skip.
    boost::optional<long long> newLimit;
    if (lpq.getLimit()) {
        newLimit = *lpq.getLimit() + lpq.getSkip().value_or(0);
    }

    // Similarly, if nToReturn is set, we forward the sum of nToReturn and the skip.
    boost::optional<long long> newNToReturn;
    if (lpq.getNToReturn()) {
        newNToReturn = *lpq.getNToReturn() + lpq.getSkip().value_or(0);
    }

    return LiteParsedQuery::makeAsFindCmd(lpq.nss(),
                                          lpq.getFilter(),
                                          lpq.getProj(),
                                          lpq.getSort(),
                                          lpq.getHint(),
                                          boost::none,  // Don't forward skip.
                                          newLimit,
                                          lpq.getBatchSize(),
                                          newNToReturn,
                                          lpq.wantMore(),
                                          lpq.isExplain(),
                                          lpq.getComment(),
                                          lpq.getMaxScan(),
                                          lpq.getMaxTimeMS(),
                                          lpq.getMin(),
                                          lpq.getMax(),
                                          lpq.returnKey(),
                                          lpq.showRecordId(),
                                          lpq.isSnapshot(),
                                          lpq.hasReadPref(),
                                          lpq.isTailable(),
                                          lpq.isSlaveOk(),
                                          lpq.isOplogReplay(),
                                          lpq.isNoCursorTimeout(),
                                          lpq.isAwaitData(),
                                          lpq.isPartial());
}

StatusWith<CursorId> runQueryWithoutRetrying(OperationContext* txn,
                                             const CanonicalQuery& query,
                                             const ReadPreferenceSetting& readPref,
                                             ChunkManager* chunkManager,
                                             std::shared_ptr<Shard> primary,
                                             std::vector<BSONObj>* results) {
    auto shardRegistry = grid.shardRegistry();

    // Get the set of shards on which we will run the query.
    std::vector<std::shared_ptr<Shard>> shards;
    if (primary) {
        shards.emplace_back(std::move(primary));
    } else {
        invariant(chunkManager);

        std::set<ShardId> shardIds;
        chunkManager->getShardIdsForQuery(shardIds, query.getParsed().getFilter());

        for (auto id : shardIds) {
            shards.emplace_back(shardRegistry->getShard(txn, id));
        }
    }

    ClusterClientCursorParams params(query.nss());
    params.limit = query.getParsed().getLimit();
    params.batchSize = query.getParsed().getEffectiveBatchSize();
    params.skip = query.getParsed().getSkip();
    params.isTailable = query.getParsed().isTailable();

    // $natural sort is actually a hint to use a collection scan, and shouldn't be treated like a
    // sort on mongos. Including a $natural anywhere in the sort spec results in the whole sort
    // being considered a hint to use a collection scan.
    if (!query.getParsed().getSort().hasField("$natural")) {
        params.sort = query.getParsed().getSort();
    }

    // Tailable cursors can't have a sort, which should have already been validated.
    invariant(params.sort.isEmpty() || !params.isTailable);

    const auto lpqToForward = transformQueryForShards(query.getParsed());

    // Use read pref to target a particular host from each shard. Also construct the find command
    // that we will forward to each shard.
    for (const auto& shard : shards) {
        // The find command cannot be used to query config server content with legacy 3-host config
        // servers, because the new targeting logic only works for config server replica sets.
        if (shard->isConfig() && shard->getConnString().type() == ConnectionString::SYNC) {
            return Status(ErrorCodes::CommandNotSupported,
                          "find command not supported without config server as a replica set");
        }

        auto targeter = shard->getTargeter();
        auto hostAndPort = targeter->findHost(readPref);
        if (!hostAndPort.isOK()) {
            return hostAndPort.getStatus();
        }

        // Build the find command, and attach shard version if necessary.
        BSONObjBuilder cmdBuilder;
        lpqToForward->asFindCommand(&cmdBuilder);

        if (chunkManager) {
            auto shardVersion = chunkManager->getVersion(shard->getId());
            cmdBuilder.appendArray(LiteParsedQuery::kShardVersionField, shardVersion.toBSON());
        }

        params.remotes.emplace_back(std::move(hostAndPort.getValue()), cmdBuilder.obj());
    }

    auto ccc =
        stdx::make_unique<ClusterClientCursorImpl>(shardRegistry->getExecutor(), std::move(params));

    // Register the cursor with the cursor manager.
    auto cursorManager = grid.getCursorManager();
    const auto cursorType = chunkManager ? ClusterCursorManager::CursorType::NamespaceSharded
                                         : ClusterCursorManager::CursorType::NamespaceNotSharded;
    const auto cursorLifetime = query.getParsed().isNoCursorTimeout()
        ? ClusterCursorManager::CursorLifetime::Immortal
        : ClusterCursorManager::CursorLifetime::Mortal;
    auto pinnedCursor =
        cursorManager->registerCursor(std::move(ccc), query.nss(), cursorType, cursorLifetime);

    auto cursorState = ClusterCursorManager::CursorState::NotExhausted;
    int bytesBuffered = 0;
    while (!FindCommon::enoughForFirstBatch(query.getParsed(), results->size(), bytesBuffered)) {
        auto next = pinnedCursor.next();
        if (!next.isOK()) {
            return next.getStatus();
        }

        if (!next.getValue()) {
            // We reached end-of-stream.
            if (!pinnedCursor.isTailable()) {
                cursorState = ClusterCursorManager::CursorState::Exhausted;
            }
            break;
        }

        // Add doc to the batch.
        bytesBuffered += next.getValue()->objsize();
        results->push_back(std::move(*next.getValue()));
    }

    CursorId idToReturn = (cursorState == ClusterCursorManager::CursorState::Exhausted)
        ? CursorId(0)
        : pinnedCursor.getCursorId();

    // Transfer ownership of the cursor back to the cursor manager.
    pinnedCursor.returnCursor(cursorState);

    return idToReturn;
}

}  // namespace

const size_t ClusterFind::kMaxStaleConfigRetries = 10;

StatusWith<CursorId> ClusterFind::runQuery(OperationContext* txn,
                                           const CanonicalQuery& query,
                                           const ReadPreferenceSetting& readPref,
                                           std::vector<BSONObj>* results) {
    invariant(results);

    auto dbConfig = grid.catalogCache()->getDatabase(txn, query.nss().db().toString());
    if (dbConfig.getStatus() == ErrorCodes::DatabaseNotFound) {
        // If the database doesn't exist, we successfully return an empty result set without
        // creating a cursor.
        return CursorId(0);
    } else if (!dbConfig.isOK()) {
        return dbConfig.getStatus();
    }

    std::shared_ptr<ChunkManager> chunkManager;
    std::shared_ptr<Shard> primary;
    dbConfig.getValue()->getChunkManagerOrPrimary(txn, query.nss().ns(), chunkManager, primary);

    // Re-target and re-send the initial find command to the shards until we have established the
    // shard version.
    for (size_t retries = 1; retries <= kMaxStaleConfigRetries; ++retries) {
        auto cursorId = runQueryWithoutRetrying(
            txn, query, readPref, chunkManager.get(), std::move(primary), results);
        if (cursorId.isOK()) {
            return cursorId;
        }
        auto status = std::move(cursorId.getStatus());

        if (status != ErrorCodes::SendStaleConfig && status != ErrorCodes::RecvStaleConfig) {
            // Errors other than receiving a stale config message from mongoD are fatal to the
            // operation.
            return status;
        }

        LOG(1) << "Received stale config for query " << query.toStringShort() << " on attempt "
               << retries << " of " << kMaxStaleConfigRetries << ": " << status.reason();

        invariant(chunkManager);
        chunkManager = chunkManager->reload(txn);
    }

    return {ErrorCodes::StaleShardVersion,
            str::stream() << "Retried " << kMaxStaleConfigRetries
                          << " times without establishing shard version."};
}

StatusWith<CursorResponse> ClusterFind::runGetMore(OperationContext* txn,
                                                   const GetMoreRequest& request) {
    auto cursorManager = grid.getCursorManager();

    auto pinnedCursor = cursorManager->checkOutCursor(request.nss, request.cursorid);
    if (!pinnedCursor.isOK()) {
        return pinnedCursor.getStatus();
    }
    invariant(request.cursorid == pinnedCursor.getValue().getCursorId());

    std::vector<BSONObj> batch;
    int bytesBuffered = 0;
    long long batchSize = request.batchSize.value_or(0);
    long long startingFrom = pinnedCursor.getValue().getNumReturnedSoFar();
    auto cursorState = ClusterCursorManager::CursorState::NotExhausted;
    while (!FindCommon::enoughForGetMore(batchSize, batch.size(), bytesBuffered)) {
        auto next = pinnedCursor.getValue().next();
        if (!next.isOK()) {
            return next.getStatus();
        }

        if (!next.getValue()) {
            // We reached end-of-stream.
            if (!pinnedCursor.getValue().isTailable()) {
                cursorState = ClusterCursorManager::CursorState::Exhausted;
            }
            break;
        }

        // Add doc to the batch.
        bytesBuffered += next.getValue()->objsize();
        batch.push_back(std::move(*next.getValue()));
    }

    // Transfer ownership of the cursor back to the cursor manager.
    pinnedCursor.getValue().returnCursor(cursorState);

    CursorId idToReturn = (cursorState == ClusterCursorManager::CursorState::Exhausted)
        ? CursorId(0)
        : request.cursorid;
    return CursorResponse(request.nss, idToReturn, std::move(batch), startingFrom);
}

}  // namespace mongo
