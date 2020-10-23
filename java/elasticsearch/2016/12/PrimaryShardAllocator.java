/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.gateway;

import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.message.ParameterizedMessage;
import org.apache.logging.log4j.util.Supplier;
import org.apache.lucene.util.CollectionUtil;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.routing.RecoverySource;
import org.elasticsearch.cluster.routing.RoutingNode;
import org.elasticsearch.cluster.routing.RoutingNodes;
import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.cluster.routing.UnassignedInfo.AllocationStatus;
import org.elasticsearch.cluster.routing.allocation.AllocateUnassignedDecision;
import org.elasticsearch.cluster.routing.allocation.NodeAllocationResult;
import org.elasticsearch.cluster.routing.allocation.NodeAllocationResult.ShardStoreInfo;
import org.elasticsearch.cluster.routing.allocation.RoutingAllocation;
import org.elasticsearch.cluster.routing.allocation.decider.Decision;
import org.elasticsearch.cluster.routing.allocation.decider.Decision.Type;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Setting.Property;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.env.ShardLockObtainFailedException;
import org.elasticsearch.gateway.AsyncShardFetch.FetchResult;
import org.elasticsearch.gateway.TransportNodesListGatewayStartedShards.NodeGatewayStartedShards;
import org.elasticsearch.index.shard.ShardStateMetaData;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * The primary shard allocator allocates unassigned primary shards to nodes that hold
 * valid copies of the unassigned primaries.  It does this by iterating over all unassigned
 * primary shards in the routing table and fetching shard metadata from each node in the cluster
 * that holds a copy of the shard.  The shard metadata from each node is compared against the
 * set of valid allocation IDs and for all valid shard copies (if any), the primary shard allocator
 * executes the allocation deciders to chose a copy to assign the primary shard to.
 *
 * Note that the PrimaryShardAllocator does *not* allocate primaries on index creation
 * (see {@link org.elasticsearch.cluster.routing.allocation.allocator.BalancedShardsAllocator}),
 * nor does it allocate primaries when a primary shard failed and there is a valid replica
 * copy that can immediately be promoted to primary, as this takes place in {@link RoutingNodes#failShard}.
 */
public abstract class PrimaryShardAllocator extends BaseGatewayShardAllocator {

    private static final Function<String, String> INITIAL_SHARDS_PARSER = (value) -> {
        switch (value) {
            case "quorum":
            case "quorum-1":
            case "half":
            case "one":
            case "full":
            case "full-1":
            case "all-1":
            case "all":
                return value;
            default:
                Integer.parseInt(value); // it can be parsed that's all we care here?
                return value;
        }
    };

    public static final Setting<String> NODE_INITIAL_SHARDS_SETTING =
        new Setting<>("gateway.initial_shards", (settings) -> settings.get("gateway.local.initial_shards", "quorum"), INITIAL_SHARDS_PARSER,
            Property.Dynamic, Property.NodeScope);
    @Deprecated
    public static final Setting<String> INDEX_RECOVERY_INITIAL_SHARDS_SETTING =
        new Setting<>("index.recovery.initial_shards", (settings) -> NODE_INITIAL_SHARDS_SETTING.get(settings) , INITIAL_SHARDS_PARSER,
            Property.Dynamic, Property.IndexScope);

    public PrimaryShardAllocator(Settings settings) {
        super(settings);
        logger.debug("using initial_shards [{}]", NODE_INITIAL_SHARDS_SETTING.get(settings));
    }

    /**
     * Is the allocator responsible for allocating the given {@link ShardRouting}?
     */
    private static boolean isResponsibleFor(final ShardRouting shard) {
        return shard.primary() // must be primary
                && shard.unassigned() // must be unassigned
                // only handle either an existing store or a snapshot recovery
                && (shard.recoverySource().getType() == RecoverySource.Type.EXISTING_STORE
                    || shard.recoverySource().getType() == RecoverySource.Type.SNAPSHOT);
    }

    @Override
    public AllocateUnassignedDecision makeAllocationDecision(final ShardRouting unassignedShard,
                                                             final RoutingAllocation allocation,
                                                             final Logger logger) {
        if (isResponsibleFor(unassignedShard) == false) {
            // this allocator is not responsible for allocating this shard
            return AllocateUnassignedDecision.NOT_TAKEN;
        }

        final boolean explain = allocation.debugDecision();
        final FetchResult<NodeGatewayStartedShards> shardState = fetchData(unassignedShard, allocation);
        if (shardState.hasData() == false) {
            allocation.setHasPendingAsyncFetch();
            List<NodeAllocationResult> nodeDecisions = null;
            if (explain) {
                nodeDecisions = buildDecisionsForAllNodes(unassignedShard, allocation);
            }
            return AllocateUnassignedDecision.no(AllocationStatus.FETCHING_SHARD_DATA, nodeDecisions);
        }

        // don't create a new IndexSetting object for every shard as this could cause a lot of garbage
        // on cluster restart if we allocate a boat load of shards
        final IndexMetaData indexMetaData = allocation.metaData().getIndexSafe(unassignedShard.index());
        final Set<String> inSyncAllocationIds = indexMetaData.inSyncAllocationIds(unassignedShard.id());
        final boolean snapshotRestore = unassignedShard.recoverySource().getType() == RecoverySource.Type.SNAPSHOT;
        final boolean recoverOnAnyNode = recoverOnAnyNode(indexMetaData);

        final NodeShardsResult nodeShardsResult;
        final boolean enoughAllocationsFound;

        if (inSyncAllocationIds.isEmpty()) {
            assert Version.indexCreated(indexMetaData.getSettings()).before(Version.V_5_0_0_alpha1) :
                "trying to allocate a primary with an empty in sync allocation id set, but index is new. index: "
                    + indexMetaData.getIndex();
            // when we load an old index (after upgrading cluster) or restore a snapshot of an old index
            // fall back to old version-based allocation mode
            // Note that once the shard has been active, lastActiveAllocationIds will be non-empty
            nodeShardsResult = buildVersionBasedNodeShardsResult(unassignedShard, snapshotRestore || recoverOnAnyNode,
                                                                 allocation.getIgnoreNodes(unassignedShard.shardId()), shardState, logger);
            if (snapshotRestore || recoverOnAnyNode) {
                enoughAllocationsFound = nodeShardsResult.allocationsFound > 0;
            } else {
                enoughAllocationsFound = isEnoughVersionBasedAllocationsFound(indexMetaData, nodeShardsResult);
            }
            logger.debug("[{}][{}]: version-based allocation for pre-{} index found {} allocations of {}", unassignedShard.index(),
                         unassignedShard.id(), Version.V_5_0_0_alpha1, nodeShardsResult.allocationsFound, unassignedShard);
        } else {
            assert inSyncAllocationIds.isEmpty() == false;
            // use allocation ids to select nodes
            nodeShardsResult = buildAllocationIdBasedNodeShardsResult(unassignedShard, snapshotRestore || recoverOnAnyNode,
                allocation.getIgnoreNodes(unassignedShard.shardId()), inSyncAllocationIds, shardState, logger);
            enoughAllocationsFound = nodeShardsResult.orderedAllocationCandidates.size() > 0;
            logger.debug("[{}][{}]: found {} allocation candidates of {} based on allocation ids: [{}]", unassignedShard.index(),
                         unassignedShard.id(), nodeShardsResult.orderedAllocationCandidates.size(), unassignedShard, inSyncAllocationIds);
        }

        if (enoughAllocationsFound == false) {
            if (snapshotRestore) {
                // let BalancedShardsAllocator take care of allocating this shard
                logger.debug("[{}][{}]: missing local data, will restore from [{}]",
                             unassignedShard.index(), unassignedShard.id(), unassignedShard.recoverySource());
                return AllocateUnassignedDecision.NOT_TAKEN;
            } else if (recoverOnAnyNode) {
                // let BalancedShardsAllocator take care of allocating this shard
                logger.debug("[{}][{}]: missing local data, recover from any node", unassignedShard.index(), unassignedShard.id());
                return AllocateUnassignedDecision.NOT_TAKEN;
            } else {
                // We have a shard that was previously allocated, but we could not find a valid shard copy to allocate the primary.
                // We could just be waiting for the node that holds the primary to start back up, in which case the allocation for
                // this shard will be picked up when the node joins and we do another allocation reroute
                logger.debug("[{}][{}]: not allocating, number_of_allocated_shards_found [{}]",
                             unassignedShard.index(), unassignedShard.id(), nodeShardsResult.allocationsFound);
                return AllocateUnassignedDecision.no(AllocationStatus.NO_VALID_SHARD_COPY, null);
            }
        }

        NodesToAllocate nodesToAllocate = buildNodesToAllocate(
            allocation, nodeShardsResult.orderedAllocationCandidates, unassignedShard, false
        );
        DiscoveryNode node = null;
        String allocationId = null;
        boolean throttled = false;
        if (nodesToAllocate.yesNodeShards.isEmpty() == false) {
            DecidedNode decidedNode = nodesToAllocate.yesNodeShards.get(0);
            logger.debug("[{}][{}]: allocating [{}] to [{}] on primary allocation",
                         unassignedShard.index(), unassignedShard.id(), unassignedShard, decidedNode.nodeShardState.getNode());
            node = decidedNode.nodeShardState.getNode();
            allocationId = decidedNode.nodeShardState.allocationId();
        } else if (nodesToAllocate.throttleNodeShards.isEmpty() && !nodesToAllocate.noNodeShards.isEmpty()) {
            // The deciders returned a NO decision for all nodes with shard copies, so we check if primary shard
            // can be force-allocated to one of the nodes.
            nodesToAllocate = buildNodesToAllocate(allocation, nodeShardsResult.orderedAllocationCandidates, unassignedShard, true);
            if (nodesToAllocate.yesNodeShards.isEmpty() == false) {
                final DecidedNode decidedNode = nodesToAllocate.yesNodeShards.get(0);
                final NodeGatewayStartedShards nodeShardState = decidedNode.nodeShardState;
                logger.debug("[{}][{}]: allocating [{}] to [{}] on forced primary allocation",
                             unassignedShard.index(), unassignedShard.id(), unassignedShard, nodeShardState.getNode());
                node = nodeShardState.getNode();
                allocationId = nodeShardState.allocationId();
            } else if (nodesToAllocate.throttleNodeShards.isEmpty() == false) {
                logger.debug("[{}][{}]: throttling allocation [{}] to [{}] on forced primary allocation",
                             unassignedShard.index(), unassignedShard.id(), unassignedShard, nodesToAllocate.throttleNodeShards);
                throttled = true;
            } else {
                logger.debug("[{}][{}]: forced primary allocation denied [{}]",
                             unassignedShard.index(), unassignedShard.id(), unassignedShard);
            }
        } else {
            // we are throttling this, since we are allowed to allocate to this node but there are enough allocations
            // taking place on the node currently, ignore it for now
            logger.debug("[{}][{}]: throttling allocation [{}] to [{}] on primary allocation",
                         unassignedShard.index(), unassignedShard.id(), unassignedShard, nodesToAllocate.throttleNodeShards);
            throttled = true;
        }

        List<NodeAllocationResult> nodeResults = null;
        if (explain) {
            nodeResults = buildNodeDecisions(nodesToAllocate, inSyncAllocationIds);
        }
        if (allocation.hasPendingAsyncFetch()) {
            return AllocateUnassignedDecision.no(AllocationStatus.FETCHING_SHARD_DATA, nodeResults);
        } else if (node != null) {
            return AllocateUnassignedDecision.yes(node, allocationId, nodeResults, false);
        } else if (throttled) {
            return AllocateUnassignedDecision.throttle(nodeResults);
        } else {
            return AllocateUnassignedDecision.no(AllocationStatus.DECIDERS_NO, nodeResults, true);
        }
    }

    /**
     * Builds a map of nodes to the corresponding allocation decisions for those nodes.
     */
    private static List<NodeAllocationResult> buildNodeDecisions(NodesToAllocate nodesToAllocate, Set<String> inSyncAllocationIds) {
        return Stream.of(nodesToAllocate.yesNodeShards, nodesToAllocate.throttleNodeShards, nodesToAllocate.noNodeShards)
                   .flatMap(Collection::stream)
                   .map(dnode -> new NodeAllocationResult(dnode.nodeShardState.getNode(),
                                                             shardStoreInfo(dnode.nodeShardState, inSyncAllocationIds),
                                                             dnode.decision))
                   .collect(Collectors.toList());
    }

    private static ShardStoreInfo shardStoreInfo(NodeGatewayStartedShards nodeShardState, Set<String> inSyncAllocationIds) {
        final Exception storeErr = nodeShardState.storeException();
        final boolean inSync = nodeShardState.allocationId() != null && inSyncAllocationIds.contains(nodeShardState.allocationId());
        return new ShardStoreInfo(nodeShardState.allocationId(), inSync, storeErr);
    }

    private static final Comparator<NodeGatewayStartedShards> NO_STORE_EXCEPTION_FIRST_COMPARATOR =
        Comparator.comparing((NodeGatewayStartedShards state) -> state.storeException() == null).reversed();
    private static final Comparator<NodeGatewayStartedShards> PRIMARY_FIRST_COMPARATOR =
        Comparator.comparing(NodeGatewayStartedShards::primary).reversed();

    /**
     * Builds a list of nodes. If matchAnyShard is set to false, only nodes that have an allocation id matching
     * inSyncAllocationIds are added to the list. Otherwise, any node that has a shard is added to the list, but
     * entries with matching allocation id are always at the front of the list.
     */
    protected static NodeShardsResult buildAllocationIdBasedNodeShardsResult(ShardRouting shard, boolean matchAnyShard,
                                                                             Set<String> ignoreNodes, Set<String> inSyncAllocationIds,
                                                                             FetchResult<NodeGatewayStartedShards> shardState,
                                                                             Logger logger) {
        List<NodeGatewayStartedShards> nodeShardStates = new ArrayList<>();
        int numberOfAllocationsFound = 0;
        for (NodeGatewayStartedShards nodeShardState : shardState.getData().values()) {
            DiscoveryNode node = nodeShardState.getNode();
            String allocationId = nodeShardState.allocationId();

            if (ignoreNodes.contains(node.getId())) {
                continue;
            }

            if (nodeShardState.storeException() == null) {
                if (allocationId == null && nodeShardState.legacyVersion() == ShardStateMetaData.NO_VERSION) {
                    logger.trace("[{}] on node [{}] has no shard state information", shard, nodeShardState.getNode());
                } else if (allocationId != null) {
                    assert nodeShardState.legacyVersion() == ShardStateMetaData.NO_VERSION : "Allocation id and legacy version cannot be both present";
                    logger.trace("[{}] on node [{}] has allocation id [{}]", shard, nodeShardState.getNode(), allocationId);
                } else {
                    logger.trace("[{}] on node [{}] has no allocation id, out-dated shard (shard state version: [{}])", shard, nodeShardState.getNode(), nodeShardState.legacyVersion());
                }
            } else {
                final String finalAllocationId = allocationId;
                if (nodeShardState.storeException() instanceof ShardLockObtainFailedException) {
                    logger.trace((Supplier<?>) () -> new ParameterizedMessage("[{}] on node [{}] has allocation id [{}] but the store can not be opened as it's locked, treating as valid shard", shard, nodeShardState.getNode(), finalAllocationId), nodeShardState.storeException());
                } else {
                    logger.trace((Supplier<?>) () -> new ParameterizedMessage("[{}] on node [{}] has allocation id [{}] but the store can not be opened, treating as no allocation id", shard, nodeShardState.getNode(), finalAllocationId), nodeShardState.storeException());
                    allocationId = null;
                }
            }

            if (allocationId != null) {
                assert nodeShardState.storeException() == null ||
                    nodeShardState.storeException() instanceof ShardLockObtainFailedException :
                    "only allow store that can be opened or that throws a ShardLockObtainFailedException while being opened but got a store throwing " + nodeShardState.storeException();
                numberOfAllocationsFound++;
                if (matchAnyShard || inSyncAllocationIds.contains(nodeShardState.allocationId())) {
                    nodeShardStates.add(nodeShardState);
                }
            }
        }

        final Comparator<NodeGatewayStartedShards> comparator; // allocation preference
        if (matchAnyShard) {
            // prefer shards with matching allocation ids
            Comparator<NodeGatewayStartedShards> matchingAllocationsFirst = Comparator.comparing(
                (NodeGatewayStartedShards state) -> inSyncAllocationIds.contains(state.allocationId())).reversed();
            comparator = matchingAllocationsFirst.thenComparing(NO_STORE_EXCEPTION_FIRST_COMPARATOR).thenComparing(PRIMARY_FIRST_COMPARATOR);
        } else {
            comparator = NO_STORE_EXCEPTION_FIRST_COMPARATOR.thenComparing(PRIMARY_FIRST_COMPARATOR);
        }

        nodeShardStates.sort(comparator);

        if (logger.isTraceEnabled()) {
            logger.trace("{} candidates for allocation: {}", shard, nodeShardStates.stream().map(s -> s.getNode().getName()).collect(Collectors.joining(", ")));
        }
        return new NodeShardsResult(nodeShardStates, numberOfAllocationsFound);
    }

    /**
     * used by old version-based allocation
     */
    private boolean isEnoughVersionBasedAllocationsFound(IndexMetaData indexMetaData, NodeShardsResult nodeShardsResult) {
        // check if the counts meets the minimum set
        int requiredAllocation = 1;
        // if we restore from a repository one copy is more then enough
        String initialShards = INDEX_RECOVERY_INITIAL_SHARDS_SETTING.get(indexMetaData.getSettings(), settings);
        if ("quorum".equals(initialShards)) {
            if (indexMetaData.getNumberOfReplicas() > 1) {
                requiredAllocation = ((1 + indexMetaData.getNumberOfReplicas()) / 2) + 1;
            }
        } else if ("quorum-1".equals(initialShards) || "half".equals(initialShards)) {
            if (indexMetaData.getNumberOfReplicas() > 2) {
                requiredAllocation = ((1 + indexMetaData.getNumberOfReplicas()) / 2);
            }
        } else if ("one".equals(initialShards)) {
            requiredAllocation = 1;
        } else if ("full".equals(initialShards) || "all".equals(initialShards)) {
            requiredAllocation = indexMetaData.getNumberOfReplicas() + 1;
        } else if ("full-1".equals(initialShards) || "all-1".equals(initialShards)) {
            if (indexMetaData.getNumberOfReplicas() > 1) {
                requiredAllocation = indexMetaData.getNumberOfReplicas();
            }
        } else {
            requiredAllocation = Integer.parseInt(initialShards);
        }

        return nodeShardsResult.allocationsFound >= requiredAllocation;
    }

    /**
     * Split the list of node shard states into groups yes/no/throttle based on allocation deciders
     */
    private NodesToAllocate buildNodesToAllocate(RoutingAllocation allocation,
                                                 List<NodeGatewayStartedShards> nodeShardStates,
                                                 ShardRouting shardRouting,
                                                 boolean forceAllocate) {
        List<DecidedNode> yesNodeShards = new ArrayList<>();
        List<DecidedNode> throttledNodeShards = new ArrayList<>();
        List<DecidedNode> noNodeShards = new ArrayList<>();
        for (NodeGatewayStartedShards nodeShardState : nodeShardStates) {
            RoutingNode node = allocation.routingNodes().node(nodeShardState.getNode().getId());
            if (node == null) {
                continue;
            }

            Decision decision = forceAllocate ? allocation.deciders().canForceAllocatePrimary(shardRouting, node, allocation) :
                                                allocation.deciders().canAllocate(shardRouting, node, allocation);
            DecidedNode decidedNode = new DecidedNode(nodeShardState, decision);
            if (decision.type() == Type.THROTTLE) {
                throttledNodeShards.add(decidedNode);
            } else if (decision.type() == Type.NO) {
                noNodeShards.add(decidedNode);
            } else {
                yesNodeShards.add(decidedNode);
            }
        }
        return new NodesToAllocate(Collections.unmodifiableList(yesNodeShards), Collections.unmodifiableList(throttledNodeShards), Collections.unmodifiableList(noNodeShards));
    }

    /**
     * Builds a list of previously started shards. If matchAnyShard is set to false, only shards with the highest shard version are added to
     * the list. Otherwise, any existing shard is added to the list, but entries with highest version are always at the front of the list.
     */
    static NodeShardsResult buildVersionBasedNodeShardsResult(ShardRouting shard, boolean matchAnyShard, Set<String> ignoreNodes,
                                                              FetchResult<NodeGatewayStartedShards> shardState, Logger logger) {
        final List<NodeGatewayStartedShards> allocationCandidates = new ArrayList<>();
        int numberOfAllocationsFound = 0;
        long highestVersion = ShardStateMetaData.NO_VERSION;
        for (NodeGatewayStartedShards nodeShardState : shardState.getData().values()) {
            long version = nodeShardState.legacyVersion();
            DiscoveryNode node = nodeShardState.getNode();

            if (ignoreNodes.contains(node.getId())) {
                continue;
            }

            if (nodeShardState.storeException() == null) {
                if (version == ShardStateMetaData.NO_VERSION && nodeShardState.allocationId() == null) {
                    logger.trace("[{}] on node [{}] has no shard state information", shard, nodeShardState.getNode());
                } else if (version != ShardStateMetaData.NO_VERSION) {
                    assert nodeShardState.allocationId() == null : "Allocation id and legacy version cannot be both present";
                    logger.trace("[{}] on node [{}] has version [{}] of shard", shard, nodeShardState.getNode(), version);
                } else {
                    // shard was already selected in a 5.x cluster as primary for recovery, was initialized (and wrote a new state file) but
                    // did not make it to STARTED state before the cluster crashed (otherwise list of active allocation ids would be
                    // non-empty and allocation id - based allocation mode would be chosen).
                    // Prefer this shard copy again.
                    version = Long.MAX_VALUE;
                    logger.trace("[{}] on node [{}] has allocation id [{}]", shard, nodeShardState.getNode(), nodeShardState.allocationId());
                }
            } else {
                final long finalVersion = version;
                if (nodeShardState.storeException() instanceof ShardLockObtainFailedException) {
                    logger.trace((Supplier<?>) () -> new ParameterizedMessage("[{}] on node [{}] has version [{}] but the store can not be opened as it's locked, treating as valid shard", shard, nodeShardState.getNode(), finalVersion), nodeShardState.storeException());
                    if (nodeShardState.allocationId() != null) {
                        version = Long.MAX_VALUE; // shard was already selected in a 5.x cluster as primary, prefer this shard copy again.
                    } else {
                        version = 0L; // treat as lowest version so that this shard is the least likely to be selected as primary
                    }
                } else {
                    // disregard the reported version and assign it as no version (same as shard does not exist)
                    logger.trace((Supplier<?>) () -> new ParameterizedMessage("[{}] on node [{}] has version [{}] but the store can not be opened, treating no version", shard, nodeShardState.getNode(), finalVersion), nodeShardState.storeException());
                    version = ShardStateMetaData.NO_VERSION;
                }
            }

            if (version != ShardStateMetaData.NO_VERSION) {
                numberOfAllocationsFound++;
                // If we've found a new "best" candidate, clear the
                // current candidates and add it
                if (version > highestVersion) {
                    highestVersion = version;
                    if (matchAnyShard == false) {
                        allocationCandidates.clear();
                    }
                    allocationCandidates.add(nodeShardState);
                } else if (version == highestVersion) {
                    // If the candidate is the same, add it to the
                    // list, but keep the current candidate
                    allocationCandidates.add(nodeShardState);
                }
            }
        }
        // sort array so the node with the highest version is at the beginning
        CollectionUtil.timSort(allocationCandidates, Comparator.comparing(NodeGatewayStartedShards::legacyVersion).reversed());

        if (logger.isTraceEnabled()) {
            StringBuilder sb = new StringBuilder("[");
            for (NodeGatewayStartedShards n : allocationCandidates) {
                sb.append("[").append(n.getNode().getName()).append("]").append(" -> ").append(n.legacyVersion()).append(", ");
            }
            sb.append("]");
            logger.trace("{} candidates for allocation: {}", shard, sb.toString());
        }

        return new NodeShardsResult(Collections.unmodifiableList(allocationCandidates), numberOfAllocationsFound);
    }

    /**
     * Return {@code true} if the index is configured to allow shards to be
     * recovered on any node
     */
    private boolean recoverOnAnyNode(IndexMetaData metaData) {
        return (IndexMetaData.isOnSharedFilesystem(metaData.getSettings()) || IndexMetaData.isOnSharedFilesystem(this.settings))
            && IndexMetaData.INDEX_SHARED_FS_ALLOW_RECOVERY_ON_ANY_NODE_SETTING.get(metaData.getSettings(), this.settings);
    }

    protected abstract FetchResult<NodeGatewayStartedShards> fetchData(ShardRouting shard, RoutingAllocation allocation);

    static class NodeShardsResult {
        public final List<NodeGatewayStartedShards> orderedAllocationCandidates;
        public final int allocationsFound;

        public NodeShardsResult(List<NodeGatewayStartedShards> orderedAllocationCandidates, int allocationsFound) {
            this.orderedAllocationCandidates = orderedAllocationCandidates;
            this.allocationsFound = allocationsFound;
        }
    }

    static class NodesToAllocate {
        final List<DecidedNode> yesNodeShards;
        final List<DecidedNode> throttleNodeShards;
        final List<DecidedNode> noNodeShards;

        public NodesToAllocate(List<DecidedNode> yesNodeShards, List<DecidedNode> throttleNodeShards, List<DecidedNode> noNodeShards) {
            this.yesNodeShards = yesNodeShards;
            this.throttleNodeShards = throttleNodeShards;
            this.noNodeShards = noNodeShards;
        }
    }

    /**
     * This class encapsulates the shard state retrieved from a node and the decision that was made
     * by the allocator for allocating to the node that holds the shard copy.
     */
    private static class DecidedNode {
        final NodeGatewayStartedShards nodeShardState;
        final Decision decision;

        private DecidedNode(NodeGatewayStartedShards nodeShardState, Decision decision) {
            this.nodeShardState = nodeShardState;
            this.decision = decision;
        }
    }
}
