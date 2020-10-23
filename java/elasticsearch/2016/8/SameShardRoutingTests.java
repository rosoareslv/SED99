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
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.routing.RoutingTable;
import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.cluster.routing.ShardRoutingState;
import org.elasticsearch.cluster.routing.allocation.decider.SameShardAllocationDecider;
import org.elasticsearch.common.logging.ESLogger;
import org.elasticsearch.common.logging.Loggers;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.transport.LocalTransportAddress;
import org.elasticsearch.cluster.ESAllocationTestCase;

import static java.util.Collections.emptyMap;
import static org.elasticsearch.cluster.routing.ShardRoutingState.INITIALIZING;
import static org.elasticsearch.cluster.routing.allocation.RoutingNodesUtils.numberOfShardsOfType;
import static org.hamcrest.Matchers.equalTo;

/**
 */
public class SameShardRoutingTests extends ESAllocationTestCase {
    private final ESLogger logger = Loggers.getLogger(SameShardRoutingTests.class);

    public void testSameHost() {
        AllocationService strategy = createAllocationService(
            Settings.builder().put(SameShardAllocationDecider.CLUSTER_ROUTING_ALLOCATION_SAME_HOST_SETTING.getKey(), true).build());

        MetaData metaData = MetaData.builder()
                .put(IndexMetaData.builder("test").settings(settings(Version.CURRENT)).numberOfShards(2).numberOfReplicas(1))
                .build();

        RoutingTable routingTable = RoutingTable.builder()
                .addAsNew(metaData.index("test"))
                .build();
        ClusterState clusterState = ClusterState.builder(ClusterName.CLUSTER_NAME_SETTING.getDefault(Settings.EMPTY)).metaData(metaData)
                .routingTable(routingTable).build();

        logger.info("--> adding two nodes with the same host");
        clusterState = ClusterState.builder(clusterState).nodes(
                DiscoveryNodes.builder()
                .add(new DiscoveryNode("node1", "node1", "node1", "test1", "test1", LocalTransportAddress.buildUnique(), emptyMap(),
                        MASTER_DATA_ROLES, Version.CURRENT))
                .add(new DiscoveryNode("node2", "node2", "node2", "test1", "test1", LocalTransportAddress.buildUnique(), emptyMap(),
                        MASTER_DATA_ROLES, Version.CURRENT))).build();
        RoutingAllocation.Result routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertThat(numberOfShardsOfType(clusterState.getRoutingNodes(), ShardRoutingState.INITIALIZING), equalTo(2));

        logger.info("--> start all primary shards, no replica will be started since its on the same host");
        routingResult = strategy.applyStartedShards(clusterState, clusterState.getRoutingNodes().shardsWithState(INITIALIZING));
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertThat(numberOfShardsOfType(clusterState.getRoutingNodes(), ShardRoutingState.STARTED), equalTo(2));
        assertThat(numberOfShardsOfType(clusterState.getRoutingNodes(), ShardRoutingState.INITIALIZING), equalTo(0));

        logger.info("--> add another node, with a different host, replicas will be allocating");
        clusterState = ClusterState.builder(clusterState).nodes(DiscoveryNodes.builder(clusterState.nodes())
                .add(new DiscoveryNode("node3", "node3", "node3", "test2", "test2", LocalTransportAddress.buildUnique(), emptyMap(),
                        MASTER_DATA_ROLES, Version.CURRENT))).build();
        routingResult = strategy.reroute(clusterState, "reroute");
        clusterState = ClusterState.builder(clusterState).routingResult(routingResult).build();

        assertThat(numberOfShardsOfType(clusterState.getRoutingNodes(), ShardRoutingState.STARTED), equalTo(2));
        assertThat(numberOfShardsOfType(clusterState.getRoutingNodes(), ShardRoutingState.INITIALIZING), equalTo(2));
        for (ShardRouting shardRouting : clusterState.getRoutingNodes().shardsWithState(INITIALIZING)) {
            assertThat(shardRouting.currentNodeId(), equalTo("node3"));
        }
    }
}
