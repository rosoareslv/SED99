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

package org.elasticsearch.cluster.routing.allocation;

import org.elasticsearch.Version;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.routing.RoutingNodes;
import org.elasticsearch.cluster.routing.RoutingTable;
import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.cluster.routing.allocation.command.AllocationCommands;
import org.elasticsearch.cluster.routing.allocation.command.MoveAllocationCommand;
import org.elasticsearch.cluster.routing.allocation.decider.ClusterRebalanceAllocationDecider;
import org.elasticsearch.common.logging.ESLogger;
import org.elasticsearch.common.logging.Loggers;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.cluster.ESAllocationTestCase;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

import static org.elasticsearch.cluster.routing.ShardRoutingState.INITIALIZING;
import static org.elasticsearch.cluster.routing.ShardRoutingState.RELOCATING;
import static org.elasticsearch.cluster.routing.ShardRoutingState.STARTED;
import static org.elasticsearch.cluster.routing.ShardRoutingState.UNASSIGNED;
import static org.hamcrest.Matchers.anyOf;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.lessThan;
import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.nullValue;

/**
 *
 */
public class FailedShardsRoutingTests extends ESAllocationTestCase {
    private final ESLogger logger = Loggers.getLogger(FailedShardsRoutingTests.class);

    public void testFailedShardPrimaryRelocatingToAndFrom() {
        AllocationService allocation = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("--> building initial routing table");
        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(1))
                .build();
        RoutingTable routingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();
        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(routingTable).build();

        logger.info("--> adding 2 nodes on same rack and do rerouting");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder()
                        .add(newNode("node1"))
                        .add(newNode("node2"))
        ).build();

        RoutingAllocation.Result rerouteResult = allocation.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();

        // starting primaries
        rerouteResult = allocation.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();
        // starting replicas
        rerouteResult = allocation.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();

        logger.info("--> verifying all is allocated");
        assertThat(clusterState.getRoutingNodes().node("node1").size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().node("node1").iterator().next().state(), equalTo(STARTED));
        assertThat(clusterState.getRoutingNodes().node("node2").size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().node("node2").iterator().next().state(), equalTo(STARTED));

        logger.info("--> adding additional node");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder(clusterState.nodes())
                        .add(newNode("node3"))
        ).build();
        rerouteResult = allocation.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();

        assertThat(clusterState.getRoutingNodes().node("node1").size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().node("node1").iterator().next().state(), equalTo(STARTED));
        assertThat(clusterState.getRoutingNodes().node("node2").size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().node("node2").iterator().next().state(), equalTo(STARTED));
        assertThat(clusterState.getRoutingNodes().node("node3").size(), equalTo(0));

        String origPrimaryNodeId = clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId();
        String origReplicaNodeId = clusterState.routingTable().index("test").shard(0).replicaShards().get(0).currentNodeId();

        logger.info("--> moving primary shard to node3");
        rerouteResult = allocation.reroute(clusterState, new AllocationCommands(
                        new MoveAllocationCommand("test", 0, clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId(), "node3")),
            false, false);
        assertThat(rerouteResult.changed(), equalTo(true));
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();
        assertThat(clusterState.getRoutingNodes().node(origPrimaryNodeId).iterator().next().state(), equalTo(RELOCATING));
        assertThat(clusterState.getRoutingNodes().node("node3").iterator().next().state(), equalTo(INITIALIZING));

        logger.info("--> fail primary shard recovering instance on node3 being initialized");
        rerouteResult = allocation.applyFailedShard(clusterState, clusterState.getRoutingNodes().node("node3").iterator().next());
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();

        assertThat(clusterState.getRoutingNodes().node(origPrimaryNodeId).iterator().next().state(), equalTo(STARTED));
        assertThat(clusterState.getRoutingNodes().node("node3").size(), equalTo(0));

        logger.info("--> moving primary shard to node3");
        rerouteResult = allocation.reroute(clusterState, new AllocationCommands(
                        new MoveAllocationCommand("test", 0, clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId(), "node3")),
            false, false);
        assertThat(rerouteResult.changed(), equalTo(true));
        clusterState = ClusterState.builder(clusterState).routingTable(rerouteResult.routingTable()).build();
        assertThat(clusterState.getRoutingNodes().node(origPrimaryNodeId).iterator().next().state(), equalTo(RELOCATING));
        assertThat(clusterState.getRoutingNodes().node("node3").iterator().next().state(), equalTo(INITIALIZING));

        logger.info("--> fail primary shard recovering instance on node1 being relocated");
        rerouteResult = allocation.applyFailedShard(clusterState, clusterState.getRoutingNodes().node(origPrimaryNodeId).iterator().next());
        clusterState = ClusterState.builder(clusterState).routingResult(rerouteResult).build();

        // check promotion of replica to primary
        assertThat(clusterState.getRoutingNodes().node(origReplicaNodeId).iterator().next().state(), equalTo(STARTED));
        assertThat(clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId(), equalTo(origReplicaNodeId));
        assertThat(clusterState.routingTable().index("test").shard(0).replicaShards().get(0).currentNodeId(), anyOf(equalTo(origPrimaryNodeId), equalTo("node3")));
    }

    public void testFailPrimaryStartedCheckReplicaElected() {
        AllocationService strategy = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("Building initial routing table");

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(1))
                .build();

        RoutingTable initialRoutingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(initialRoutingTable).build();

        logger.info("Adding two nodes and performing rerouting");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1")).add(newNode("node2"))).build();
        RoutingAllocation.Result routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        logger.info("Start the shards (primaries)");
        RoutingNodes routingNodes = clusterState.getRoutingNodes();

        routingResult = strategy.applyStartedShards(clusterState, routingNodes.shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), anyOf(equalTo("node1"), equalTo("node2")));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(INITIALIZING));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).currentNodeId(), anyOf(equalTo("node2"), equalTo("node1")));
        }

        logger.info("Start the shards (backups)");
        routingNodes = clusterState.getRoutingNodes();
        routingResult = strategy.applyStartedShards(clusterState, routingNodes.shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), anyOf(equalTo("node1"), equalTo("node2")));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).currentNodeId(), anyOf(equalTo("node2"), equalTo("node1")));
        }

        logger.info("fail the primary shard, will have no place to be rerouted to (single node), so stays unassigned");
        ShardRouting shardToFail = clusterState.routingTable().index("test").shard(0).primaryShard();
        routingResult = strategy.applyFailedShard(clusterState, shardToFail);
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        routingNodes = clusterState.getRoutingNodes();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shard(0).size(), equalTo(2));
        assertThat(clusterState.routingTable().index("test").shard(0).size(), equalTo(2));
        assertThat(clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId(), not(equalTo(shardToFail.currentNodeId())));
        assertThat(clusterState.routingTable().index("test").shard(0).primaryShard().state(), equalTo(STARTED));
        assertThat(clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId(), anyOf(equalTo("node1"), equalTo("node2")));
        assertThat(clusterState.routingTable().index("test").shard(0).replicaShards().size(), equalTo(1));
        assertThat(clusterState.routingTable().index("test").shard(0).replicaShards().get(0).state(), equalTo(UNASSIGNED));
    }

    public void testFirstAllocationFailureSingleNode() {
        AllocationService strategy = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("Building initial routing table");

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(1))
                .build();

        RoutingTable initialRoutingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(initialRoutingTable).build();

        logger.info("Adding single node and performing rerouting");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1"))).build();
        RoutingAllocation.Result routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(INITIALIZING));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), equalTo("node1"));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(UNASSIGNED));
        }

        logger.info("fail the first shard, will have no place to be rerouted to (single node), so stays unassigned");
        ShardRouting firstShard = clusterState.getRoutingNodes().node("node1").iterator().next();
        routingResult = strategy.applyFailedShard(clusterState, firstShard);
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(UNASSIGNED));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), nullValue());
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(UNASSIGNED));
        }
    }

    public void testSingleShardMultipleAllocationFailures() {
        AllocationService strategy = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("Building initial routing table");
        int numberOfReplicas = scaledRandomIntBetween(2, 10);
        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(numberOfReplicas))
                .build();

        RoutingTable initialRoutingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(initialRoutingTable).build();

        logger.info("Adding {} nodes and performing rerouting", numberOfReplicas + 1);
        DiscoveryNodes.Builder nodeBuilder = DiscoveryNodes.builder();
        for (int i = 0; i < numberOfReplicas + 1; i++) {
            nodeBuilder.add(newNode("node" + Integer.toString(i)));
        }
        clusterState = ClusterState.builder(clusterState).nodes(nodeBuilder).build();
        while (!clusterState.routingTable().shardsWithState(UNASSIGNED).isEmpty()) {
            // start all initializing
            clusterState = ClusterState.builder(clusterState)
                    .routingResult(strategy
                                    .applyStartedShards(clusterState, clusterState.routingTable().shardsWithState(INITIALIZING))
                    )
                    .build();
            // and assign more unassigned
            clusterState = ClusterState.builder(clusterState).routingResult(strategy.reroute(clusterState, "reroute")).build();
        }

        int shardsToFail = randomIntBetween(1, numberOfReplicas);
        ArrayList<FailedRerouteAllocation.FailedShard> failedShards = new ArrayList<>();
        RoutingNodes routingNodes = clusterState.getRoutingNodes();
        Set<String> failedNodes = new HashSet<>();
        Set<ShardRouting> shardRoutingsToFail = new HashSet<>();
        for (int i = 0; i < shardsToFail; i++) {
            String failedNode = "node" + Integer.toString(randomInt(numberOfReplicas));
            logger.info("failing shard on node [{}]", failedNode);
            ShardRouting shardToFail = routingNodes.node(failedNode).iterator().next();
            if (shardRoutingsToFail.contains(shardToFail) == false) {
                failedShards.add(new FailedRerouteAllocation.FailedShard(shardToFail, null, null));
                failedNodes.add(failedNode);
                shardRoutingsToFail.add(shardToFail);
            }
        }

        RoutingAllocation.Result routingResult = strategy.applyFailedShards(clusterState, failedShards);

        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        routingNodes = clusterState.getRoutingNodes();
        for (FailedRerouteAllocation.FailedShard failedShard : failedShards) {
            if (routingNodes.getByAllocationId(failedShard.routingEntry.shardId(), failedShard.routingEntry.allocationId().getId()) != null) {
                fail("shard " + failedShard + " was not failed");
            }
        }

        for (String failedNode : failedNodes) {
            if (!routingNodes.node(failedNode).isEmpty()) {
                fail("shard was re-assigned to failed node " + failedNode);
            }
        }
    }

    public void testFirstAllocationFailureTwoNodes() {
        AllocationService strategy = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("Building initial routing table");

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(1))
                .build();

        RoutingTable initialRoutingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(initialRoutingTable).build();

        logger.info("Adding two nodes and performing rerouting");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1")).add(newNode("node2"))).build();
        RoutingAllocation.Result routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        final String nodeHoldingPrimary = clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(INITIALIZING));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), equalTo(nodeHoldingPrimary));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(UNASSIGNED));
        }

        logger.info("fail the first shard, will start INITIALIZING on the second node");
        final ShardRouting firstShard = clusterState.getRoutingNodes().node(nodeHoldingPrimary).iterator().next();
        routingResult = strategy.applyFailedShard(clusterState, firstShard);
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        assertTrue(routingResult.changed());

        final String nodeHoldingPrimary2 = clusterState.routingTable().index("test").shard(0).primaryShard().currentNodeId();
        assertThat(nodeHoldingPrimary2, not(equalTo(nodeHoldingPrimary)));

        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(1));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(INITIALIZING));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), not(equalTo(nodeHoldingPrimary)));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(UNASSIGNED));
        }
    }

    public void testRebalanceFailure() {
        AllocationService strategy = createAllocationService(Settings.builder()
                .put("cluster.routing.allocation.node_concurrent_recoveries", 10)
                .put(ClusterRebalanceAllocationDecider.CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getKey(), "always")
                .build());

        logger.info("Building initial routing table");

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(2).numberOfReplicas(1))
                .build();

        RoutingTable initialRoutingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(initialRoutingTable).build();

        logger.info("Adding two nodes and performing rerouting");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1")).add(newNode("node2"))).build();
        RoutingAllocation.Result routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        logger.info("Start the shards (primaries)");
        RoutingNodes routingNodes = clusterState.getRoutingNodes();
        routingResult = strategy.applyStartedShards(clusterState, routingNodes.shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(2));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), anyOf(equalTo("node1"), equalTo("node2")));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(INITIALIZING));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).currentNodeId(), anyOf(equalTo("node2"), equalTo("node1")));
        }

        logger.info("Start the shards (backups)");
        routingNodes = clusterState.getRoutingNodes();
        routingResult = strategy.applyStartedShards(clusterState, routingNodes.shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(2));
        for (int i = 0; i < clusterState.routingTable().index("test").shards().size(); i++) {
            assertThat(clusterState.routingTable().index("test").shard(i).size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).shards().size(), equalTo(2));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).primaryShard().currentNodeId(), anyOf(equalTo("node1"), equalTo("node2")));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().size(), equalTo(1));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).state(), equalTo(STARTED));
            assertThat(clusterState.routingTable().index("test").shard(i).replicaShards().get(0).currentNodeId(), anyOf(equalTo("node2"), equalTo("node1")));
        }

        logger.info("Adding third node and reroute");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder(clusterState.nodes()).add(newNode("node3"))).build();
        routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        routingNodes = clusterState.getRoutingNodes();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(2));
        assertThat(routingNodes.node("node1").numberOfShardsWithState(STARTED, RELOCATING), equalTo(2));
        assertThat(routingNodes.node("node1").numberOfShardsWithState(STARTED), lessThan(3));
        assertThat(routingNodes.node("node2").numberOfShardsWithState(STARTED, RELOCATING), equalTo(2));
        assertThat(routingNodes.node("node2").numberOfShardsWithState(STARTED), lessThan(3));
        assertThat(routingNodes.node("node3").numberOfShardsWithState(INITIALIZING), equalTo(1));


        logger.info("Fail the shards on node 3");
        ShardRouting shardToFail = routingNodes.node("node3").iterator().next();
        routingNodes = clusterState.getRoutingNodes();
        routingResult = strategy.applyFailedShard(clusterState, shardToFail);
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        routingNodes = clusterState.getRoutingNodes();

        assertTrue(routingResult.changed());
        assertThat(clusterState.routingTable().index("test").shards().size(), equalTo(2));
        assertThat(routingNodes.node("node1").numberOfShardsWithState(STARTED, RELOCATING), equalTo(2));
        assertThat(routingNodes.node("node1").numberOfShardsWithState(STARTED), lessThan(3));
        assertThat(routingNodes.node("node2").numberOfShardsWithState(STARTED, RELOCATING), equalTo(2));
        assertThat(routingNodes.node("node2").numberOfShardsWithState(STARTED), lessThan(3));
        assertThat(routingNodes.node("node3").numberOfShardsWithState(INITIALIZING), equalTo(1));
        // make sure the failedShard is not INITIALIZING again on node3
        assertThat(routingNodes.node("node3").iterator().next().shardId(), not(equalTo(shardToFail.shardId())));
    }

    public void testFailAllReplicasInitializingOnPrimaryFail() {
        AllocationService allocation = createAllocationService(Settings.builder()
                .build());

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(2))
                .build();

        RoutingTable routingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(routingTable).build();

        // add 4 nodes
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1")).add(newNode("node2")).add(newNode("node3")).add(newNode("node4"))).build();
        clusterState = ClusterState.builder(clusterState).routingTable(allocation.reroute(clusterState, "reroute").routingTable()).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(UNASSIGNED).size(), equalTo(2));
        // start primary shards
        clusterState = ClusterState.builder(clusterState).routingTable(allocation.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING)).routingTable()).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(STARTED).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(2));

        // fail the primary shard, check replicas get removed as well...
        ShardRouting primaryShardToFail = clusterState.routingTable().index("test").shard(0).primaryShard();
        RoutingAllocation.Result routingResult = allocation.applyFailedShard(clusterState, primaryShardToFail);
        assertThat(routingResult.changed(), equalTo(true));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        // the primary gets allocated on another node, replicas are unassigned
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(UNASSIGNED).size(), equalTo(2));

        ShardRouting newPrimaryShard = clusterState.routingTable().index("test").shard(0).primaryShard();
        assertThat(newPrimaryShard, not(equalTo(primaryShardToFail)));

        // start the primary shard
        clusterState = ClusterState.builder(clusterState).routingResult(allocation.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING))).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(STARTED).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(2));
    }

    public void testFailAllReplicasInitializingOnPrimaryFailWhileHavingAReplicaToElect() {
        AllocationService allocation = createAllocationService(Settings.builder()
                .build());

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(1).numberOfReplicas(2))
                .build();

        RoutingTable routingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();

        ClusterState clusterState = ClusterState.builder(org.elasticsearch.cluster.ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData).routingTable(routingTable).build();

        // add 4 nodes
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder().add(newNode("node1")).add(newNode("node2")).add(newNode("node3")).add(newNode("node4"))).build();
        clusterState = ClusterState.builder(clusterState).routingResult(allocation.reroute(clusterState, "reroute")).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(UNASSIGNED).size(), equalTo(2));
        // start primary shards
        clusterState = ClusterState.builder(clusterState).routingResult(allocation.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING))).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(STARTED).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(2));

        // start another replica shard, while keep one initializing
        clusterState = ClusterState.builder(clusterState).routingResult(allocation.applyStartedShards(clusterState, Collections.singletonList(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).get(0)))).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(STARTED).size(), equalTo(2));
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(1));

        // fail the primary shard, check one replica gets elected to primary, others become INITIALIZING (from it)
        ShardRouting primaryShardToFail = clusterState.routingTable().index("test").shard(0).primaryShard();
        RoutingAllocation.Result routingResult = allocation.applyFailedShard(clusterState, primaryShardToFail);
        assertThat(routingResult.changed(), equalTo(true));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();
        assertThat(clusterState.getRoutingNodes().shardsWithState(STARTED).size(), equalTo(1));
        assertThat(clusterState.getRoutingNodes().shardsWithState(INITIALIZING).size(), equalTo(2));

        ShardRouting newPrimaryShard = clusterState.routingTable().index("test").shard(0).primaryShard();
        assertThat(newPrimaryShard, not(equalTo(primaryShardToFail)));
    }
}
