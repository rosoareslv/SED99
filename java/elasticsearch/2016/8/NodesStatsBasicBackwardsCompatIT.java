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

package org.elasticsearch.bwcompat;

import org.elasticsearch.action.admin.cluster.node.info.NodeInfo;
import org.elasticsearch.action.admin.cluster.node.info.NodesInfoResponse;
import org.elasticsearch.action.admin.cluster.node.stats.NodesStatsRequestBuilder;
import org.elasticsearch.client.transport.TransportClient;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.test.ESBackcompatTestCase;
import org.elasticsearch.test.ESIntegTestCase;
import org.elasticsearch.transport.MockTransportClient;

import java.lang.reflect.Method;


@ESIntegTestCase.ClusterScope(scope= ESIntegTestCase.Scope.SUITE,  numClientNodes = 0)
public class NodesStatsBasicBackwardsCompatIT extends ESBackcompatTestCase {
    public void testNodeStatsSetIndices() throws Exception {
        createIndex("test");

        NodesInfoResponse nodesInfo = client().admin().cluster().prepareNodesInfo().execute().actionGet();

        Settings settings = Settings.builder()
                .put("client.transport.ignore_cluster_name", true)
                .put("node.name", "transport_client_" + getTestName()).build();

        // We explicitly connect to each node with a custom TransportClient
        for (NodeInfo n : nodesInfo.getNodes()) {
            TransportClient tc = new MockTransportClient(settings).addTransportAddress(n.getNode().getAddress());
            // Just verify that the NS can be sent and serialized/deserialized between nodes with basic indices
            tc.admin().cluster().prepareNodesStats().setIndices(true).execute().actionGet();
            tc.close();
        }
    }

    public void testNodeStatsSetRandom() throws Exception {
        createIndex("test");

        NodesInfoResponse nodesInfo = client().admin().cluster().prepareNodesInfo().execute().actionGet();

        Settings settings = Settings.builder()
                .put("node.name", "transport_client_" + getTestName())
                .put("client.transport.ignore_cluster_name", true).build();

        // We explicitly connect to each node with a custom TransportClient
        for (NodeInfo n : nodesInfo.getNodes()) {
            TransportClient tc = new MockTransportClient(settings).addTransportAddress(n.getNode().getAddress());

            // randomize the combination of flags set
            // Uses reflection to find methods in an attempt to future-proof this test against newly added flags
            NodesStatsRequestBuilder nsBuilder = tc.admin().cluster().prepareNodesStats();

            Class c = nsBuilder.getClass();
            for (Method method : c.getMethods()) {
                if (method.getName().startsWith("set")) {
                    if (method.getParameterTypes().length == 1 && method.getParameterTypes()[0] == boolean.class) {
                        method.invoke(nsBuilder, randomBoolean());
                    }
                } else if ((method.getName().equals("all") || method.getName().equals("clear")) && randomBoolean()) {
                    method.invoke(nsBuilder);
                }
            }
            nsBuilder.execute().actionGet();
            tc.close();

        }
    }

}
