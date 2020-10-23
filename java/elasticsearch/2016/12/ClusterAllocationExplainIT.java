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

package org.elasticsearch.action.admin.cluster.allocation;

import org.elasticsearch.action.admin.indices.shards.IndicesShardStoresResponse;
import org.elasticsearch.action.support.ActiveShardCount;
import org.elasticsearch.client.Requests;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.routing.UnassignedInfo;
import org.elasticsearch.cluster.routing.allocation.decider.Decision;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.test.ESIntegTestCase;
import org.elasticsearch.test.junit.annotations.TestLogging;

import java.util.Map;

import static org.elasticsearch.test.hamcrest.ElasticsearchAssertions.assertAcked;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.greaterThan;

/**
 * Tests for the cluster allocation explanation
 */
@ESIntegTestCase.ClusterScope(scope = ESIntegTestCase.Scope.TEST, numDataNodes = 0)
public final class ClusterAllocationExplainIT extends ESIntegTestCase {
    @TestLogging("_root:DEBUG")
    public void testDelayShards() throws Exception {
        logger.info("--> starting 3 nodes");
        internalCluster().startNodes(3);

        // Wait for all 3 nodes to be up
        logger.info("--> waiting for 3 nodes to be up");
        ensureStableCluster(3);

        logger.info("--> creating 'test' index");
        assertAcked(prepareCreate("test").setSettings(Settings.builder()
                .put(UnassignedInfo.INDEX_DELAYED_NODE_LEFT_TIMEOUT_SETTING.getKey(), "1m")
                .put(IndexMetaData.INDEX_NUMBER_OF_SHARDS_SETTING.getKey(), 5)
                .put(IndexMetaData.INDEX_NUMBER_OF_REPLICAS_SETTING.getKey(), 1))
                .setWaitForActiveShards(ActiveShardCount.ALL).get());

        logger.info("--> stopping a random node");
        assertTrue(internalCluster().stopRandomDataNode());

        logger.info("--> waiting for the master to remove the stopped node from the cluster state");
        ensureStableCluster(2);

        ClusterAllocationExplainResponse resp = client().admin().cluster().prepareAllocationExplain().useAnyUnassignedShard().get();
        ClusterAllocationExplanation cae = resp.getExplanation();
        assertThat(cae.getShard().getIndexName(), equalTo("test"));
        assertFalse(cae.isPrimary());
        assertFalse(cae.isAssigned());
        assertThat("expecting a remaining delay, got: " + cae.getRemainingDelayMillis(), cae.getRemainingDelayMillis(), greaterThan(0L));
    }

    public void testUnassignedShards() throws Exception {
        logger.info("--> starting 3 nodes");
        String noAttrNode = internalCluster().startNode();
        String barAttrNode = internalCluster().startNode(Settings.builder().put("node.attr.bar", "baz"));
        String fooBarAttrNode = internalCluster().startNode(Settings.builder()
                .put("node.attr.foo", "bar")
                .put("node.attr.bar", "baz"));

        // Wait for all 3 nodes to be up
        logger.info("--> waiting for 3 nodes to be up");
        client().admin().cluster().health(Requests.clusterHealthRequest().waitForNodes("3")).actionGet();

        client().admin().indices().prepareCreate("anywhere")
                .setSettings(Settings.builder()
                        .put("index.number_of_shards", 5)
                        .put("index.number_of_replicas", 1))
                .setWaitForActiveShards(ActiveShardCount.ALL)  // wait on all shards
                .get();

        client().admin().indices().prepareCreate("only-baz")
                .setSettings(Settings.builder()
                        .put("index.routing.allocation.include.bar", "baz")
                        .put("index.number_of_shards", 5)
                        .put("index.number_of_replicas", 1))
                .setWaitForActiveShards(ActiveShardCount.ALL)
                .get();

        client().admin().indices().prepareCreate("only-foo")
                .setSettings(Settings.builder()
                        .put("index.routing.allocation.include.foo", "bar")
                        .put("index.number_of_shards", 1)
                        .put("index.number_of_replicas", 1))
                .get();

        ClusterAllocationExplainResponse resp = client().admin().cluster().prepareAllocationExplain()
                .setIndex("only-foo")
                .setShard(0)
                .setPrimary(false)
                .get();
        ClusterAllocationExplanation cae = resp.getExplanation();
        assertThat(cae.getShard().getIndexName(), equalTo("only-foo"));
        assertFalse(cae.isPrimary());
        assertFalse(cae.isAssigned());
        assertFalse(cae.isStillFetchingShardData());
        assertThat(UnassignedInfo.Reason.INDEX_CREATED, equalTo(cae.getUnassignedInfo().getReason()));
        assertThat("expecting no remaining delay: " + cae.getRemainingDelayMillis(), cae.getRemainingDelayMillis(), equalTo(0L));

        Map<DiscoveryNode, NodeExplanation> explanations = cae.getNodeExplanations();

        Float barAttrWeight = -1f;
        Float fooBarAttrWeight = -1f;
        for (Map.Entry<DiscoveryNode, NodeExplanation> entry : explanations.entrySet()) {
            DiscoveryNode node = entry.getKey();
            String nodeName = node.getName();
            NodeExplanation explanation = entry.getValue();
            ClusterAllocationExplanation.FinalDecision finalDecision = explanation.getFinalDecision();
            ClusterAllocationExplanation.StoreCopy storeCopy = explanation.getStoreCopy();
            Decision d = explanation.getDecision();
            float weight = explanation.getWeight();
            IndicesShardStoresResponse.StoreStatus storeStatus = explanation.getStoreStatus();

            assertEquals(d.type(), Decision.Type.NO);
            if (noAttrNode.equals(nodeName)) {
                assertThat(d.toString(), containsString("node does not match index setting [index.routing.allocation.include] " +
                                                            "filters [foo:\"bar\"]"));
                assertNull(storeStatus);
                assertEquals("the shard cannot be assigned because one or more allocation decider returns a 'NO' decision",
                        explanation.getFinalExplanation());
                assertEquals(ClusterAllocationExplanation.FinalDecision.NO, finalDecision);
            } else if (barAttrNode.equals(nodeName)) {
                assertThat(d.toString(), containsString("node does not match index setting [index.routing.allocation.include] " +
                                                            "filters [foo:\"bar\"]"));
                barAttrWeight = weight;
                assertNull(storeStatus);
                assertEquals("the shard cannot be assigned because one or more allocation decider returns a 'NO' decision",
                        explanation.getFinalExplanation());
                assertEquals(ClusterAllocationExplanation.FinalDecision.NO, finalDecision);
            } else if (fooBarAttrNode.equals(nodeName)) {
                assertThat(d.toString(), containsString("the shard cannot be allocated to the same node"));
                fooBarAttrWeight = weight;
                assertEquals(storeStatus.getAllocationStatus(),
                        IndicesShardStoresResponse.StoreStatus.AllocationStatus.PRIMARY);
                assertEquals(ClusterAllocationExplanation.FinalDecision.NO, finalDecision);
                assertEquals(ClusterAllocationExplanation.StoreCopy.AVAILABLE, storeCopy);
                assertEquals("the shard cannot be assigned because one or more allocation decider returns a 'NO' decision",
                        explanation.getFinalExplanation());
            } else {
                fail("unexpected node with name: " + nodeName +
                     ", I have: " + noAttrNode + ", " + barAttrNode + ", " + fooBarAttrNode);
            }
        }
        assertFalse(barAttrWeight == fooBarAttrWeight);
    }
}
