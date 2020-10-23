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

package org.elasticsearch.client.transport;

import java.io.Closeable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import org.elasticsearch.Version;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.admin.cluster.node.liveness.LivenessResponse;
import org.elasticsearch.action.admin.cluster.node.liveness.TransportLivenessAction;
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.transport.TransportAddress;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.threadpool.TestThreadPool;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.Transport;
import org.elasticsearch.transport.TransportException;
import org.elasticsearch.transport.TransportInterceptor;
import org.elasticsearch.transport.TransportRequest;
import org.elasticsearch.transport.TransportRequestOptions;
import org.elasticsearch.transport.TransportResponse;
import org.elasticsearch.transport.TransportResponseHandler;
import org.elasticsearch.transport.TransportService;
import org.hamcrest.CustomMatcher;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.everyItem;
import static org.hamcrest.CoreMatchers.hasItem;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.CoreMatchers.not;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.CoreMatchers.startsWith;
import static org.hamcrest.Matchers.lessThanOrEqualTo;
import static org.hamcrest.Matchers.notNullValue;

public class TransportClientNodesServiceTests extends ESTestCase {

    private static class TestIteration implements Closeable {
        private final ThreadPool threadPool;
        private final FailAndRetryMockTransport<TestResponse> transport;
        private final TransportService transportService;
        private final TransportClientNodesService transportClientNodesService;
        private final int listNodesCount;
        private final int sniffNodesCount;
        private TransportAddress livenessAddress = buildNewFakeTransportAddress();
        final List<TransportAddress> listNodeAddresses;
        // map for each address of the nodes a cluster state request should respond with
        final Map<TransportAddress, DiscoveryNodes> nodeMap;


        TestIteration(Object... extraSettings) {
            Settings settings = Settings.builder().put(extraSettings).put("cluster.name", "test").build();
            ClusterName clusterName = ClusterName.CLUSTER_NAME_SETTING.get(settings);
            List<TransportAddress> listNodes = new ArrayList<>();
            Map<TransportAddress, DiscoveryNodes> nodeMap = new HashMap<>();
            this.listNodesCount = randomIntBetween(1, 10);
            int sniffNodesCount = 0;
            for (int i = 0; i < listNodesCount; i++) {
                TransportAddress transportAddress = buildNewFakeTransportAddress();
                listNodes.add(transportAddress);
                DiscoveryNodes.Builder discoNodes = DiscoveryNodes.builder();
                discoNodes.add(new DiscoveryNode("#list-node#-" + transportAddress, transportAddress, Version.CURRENT));

                if (TransportClient.CLIENT_TRANSPORT_SNIFF.get(settings)) {
                    final int numSniffNodes = randomIntBetween(0, 3);
                    for (int j = 0; j < numSniffNodes; ++j) {
                        TransportAddress sniffAddress = buildNewFakeTransportAddress();
                        DiscoveryNode sniffNode = new DiscoveryNode("#sniff-node#-" + sniffAddress, sniffAddress, Version.CURRENT);
                        discoNodes.add(sniffNode);
                        // also allow sniffing of the sniff node itself
                        nodeMap.put(sniffAddress, DiscoveryNodes.builder().add(sniffNode).build());
                        ++sniffNodesCount;
                    }
                }
                nodeMap.put(transportAddress, discoNodes.build());
            }
            listNodeAddresses = listNodes;
            this.nodeMap = nodeMap;
            this.sniffNodesCount = sniffNodesCount;

            threadPool = new TestThreadPool("transport-client-nodes-service-tests");
            transport = new FailAndRetryMockTransport<TestResponse>(random(), clusterName) {
                @Override
                public List<String> getLocalAddresses() {
                    return Collections.emptyList();
                }

                @Override
                protected TestResponse newResponse() {
                    return new TestResponse();
                }

                @Override
                protected ClusterState getMockClusterState(DiscoveryNode node) {
                    return ClusterState.builder(clusterName).nodes(TestIteration.this.nodeMap.get(node.getAddress())).build();
                }
            };
            transportService = new TransportService(settings, transport, threadPool, new TransportInterceptor() {
                @Override
                public AsyncSender interceptSender(AsyncSender sender) {
                    return new AsyncSender() {
                        @Override
                        public <T extends TransportResponse> void sendRequest(Transport.Connection connection, String action,
                                                                              TransportRequest request,
                                                                              TransportRequestOptions options,
                                                                              TransportResponseHandler<T> handler) {
                            if (TransportLivenessAction.NAME.equals(action)) {
                                sender.sendRequest(connection, action, request, options, wrapLivenessResponseHandler(handler,
                                    connection.getNode(), clusterName));
                            } else {
                                sender.sendRequest(connection, action, request, options, handler);
                            }
                        }
                    };
                }
            }, null);
            transportService.start();
            transportService.acceptIncomingRequests();
            transportClientNodesService =
                new TransportClientNodesService(settings, transportService, threadPool, (a, b) -> {});
            transportClientNodesService.addTransportAddresses(listNodeAddresses.toArray(new TransportAddress[0]));
        }

        private <T extends TransportResponse> TransportResponseHandler wrapLivenessResponseHandler(TransportResponseHandler<T> handler,
                                                                                                   DiscoveryNode node,
                                                                                                   ClusterName clusterName) {
            return new TransportResponseHandler<T>() {
                @Override
                public T newInstance() {
                    return handler.newInstance();
                }

                @Override
                @SuppressWarnings("unchecked")
                public void handleResponse(T response) {
                    LivenessResponse livenessResponse = new LivenessResponse(clusterName,
                            new DiscoveryNode(node.getName(), node.getId(), node.getEphemeralId(), "liveness-hostname" + node.getId(),
                                    "liveness-hostaddress" + node.getId(),
                                    livenessAddress, node.getAttributes(), node.getRoles(),
                                    node.getVersion()));
                    handler.handleResponse((T)livenessResponse);
                }

                @Override
                public void handleException(TransportException exp) {
                    handler.handleException(exp);
                }

                @Override
                public String executor() {
                    return handler.executor();
                }
            };
        }

        @Override
        public void close() {
            transport.endConnectMode();
            transportService.stop();
            transportClientNodesService.close();
            try {
                terminate(threadPool);
            } catch (InterruptedException e) {
                throw new AssertionError(e);
            }
        }
    }

    public void testListenerFailures() throws InterruptedException {
        int iters = iterations(10, 100);
        for (int i = 0; i <iters; i++) {
            try(final TestIteration iteration = new TestIteration()) {
                iteration.transport.endConnectMode(); // stop transport from responding early
                final CountDownLatch latch = new CountDownLatch(1);
                final AtomicInteger finalFailures = new AtomicInteger();
                final AtomicReference<Throwable> finalFailure = new AtomicReference<>();
                final AtomicReference<TestResponse> response = new AtomicReference<>();
                ActionListener<TestResponse> actionListener = new ActionListener<TestResponse>() {
                    @Override
                    public void onResponse(TestResponse testResponse) {
                        response.set(testResponse);
                        latch.countDown();
                    }

                    @Override
                    public void onFailure(Exception e) {
                        finalFailures.incrementAndGet();
                        finalFailure.set(e);
                        latch.countDown();
                    }
                };

                final AtomicInteger preSendFailures = new AtomicInteger();

                iteration.transportClientNodesService.execute((node, retryListener) -> {
                    if (rarely()) {
                        preSendFailures.incrementAndGet();
                        //throw whatever exception that is not a subclass of ConnectTransportException
                        throw new IllegalArgumentException();
                    }

                    iteration.transportService.sendRequest(node, "action", new TestRequest(),
                            TransportRequestOptions.EMPTY, new TransportResponseHandler<TestResponse>() {
                        @Override
                        public TestResponse newInstance() {
                            return new TestResponse();
                        }

                        @Override
                        public void handleResponse(TestResponse response1) {
                            retryListener.onResponse(response1);
                        }

                        @Override
                        public void handleException(TransportException exp) {
                            retryListener.onFailure(exp);
                        }

                        @Override
                        public String executor() {
                            return randomBoolean() ? ThreadPool.Names.SAME : ThreadPool.Names.GENERIC;
                        }
                    });
                }, actionListener);

                assertThat(latch.await(1, TimeUnit.SECONDS), equalTo(true));

                //there can be only either one failure that causes the request to fail straightaway or success
                assertThat(preSendFailures.get() + iteration.transport.failures() + iteration.transport.successes(), lessThanOrEqualTo(1));

                if (iteration.transport.successes() == 1) {
                    assertThat(finalFailures.get(), equalTo(0));
                    assertThat(finalFailure.get(), nullValue());
                    assertThat(response.get(), notNullValue());
                } else {
                    assertThat(finalFailures.get(), equalTo(1));
                    assertThat(finalFailure.get(), notNullValue());
                    assertThat(response.get(), nullValue());
                    if (preSendFailures.get() == 0 && iteration.transport.failures() == 0) {
                        assertThat(finalFailure.get(), instanceOf(NoNodeAvailableException.class));
                    }
                }

                assertThat(iteration.transport.triedNodes().size(), lessThanOrEqualTo(iteration.listNodesCount));
                assertThat(iteration.transport.triedNodes().size(), equalTo(iteration.transport.connectTransportExceptions() +
                        iteration.transport.failures() + iteration.transport.successes()));
            }
        }
    }

    public void testConnectedNodes() {
        int iters = iterations(10, 100);
        for (int i = 0; i <iters; i++) {
            try(final TestIteration iteration = new TestIteration()) {
                assertThat(iteration.transportClientNodesService.connectedNodes().size(), lessThanOrEqualTo(iteration.listNodesCount));
                for (DiscoveryNode discoveryNode : iteration.transportClientNodesService.connectedNodes()) {
                    assertThat(discoveryNode.getHostName(), startsWith("liveness-"));
                    assertThat(discoveryNode.getHostAddress(), startsWith("liveness-"));
                    assertNotEquals(discoveryNode.getAddress(), iteration.livenessAddress);
                    assertThat(iteration.listNodeAddresses, hasItem(discoveryNode.getAddress()));
                }
            }
        }
    }

    public void testRemoveAddressSniff() {
        checkRemoveAddress(true);
    }

    public void testRemoveAddressSimple() {
        checkRemoveAddress(false);
    }

    private void checkRemoveAddress(boolean sniff) {
        Object[] extraSettings = {TransportClient.CLIENT_TRANSPORT_SNIFF.getKey(), sniff};
        try(final TestIteration iteration = new TestIteration(extraSettings)) {
            final TransportClientNodesService service = iteration.transportClientNodesService;
            assertEquals(iteration.listNodesCount + iteration.sniffNodesCount, service.connectedNodes().size());
            final TransportAddress addressToRemove = randomFrom(iteration.listNodeAddresses);
            service.removeTransportAddress(addressToRemove);
            assertThat(service.connectedNodes(), everyItem(not(new CustomMatcher<DiscoveryNode>("removed address") {
                @Override
                public boolean matches(Object item) {
                    return item instanceof DiscoveryNode && ((DiscoveryNode)item).getAddress().equals(addressToRemove);
                }
            })));
            assertEquals(iteration.listNodesCount + iteration.sniffNodesCount - 1, service.connectedNodes().size());
        }
    }

    public static class TestRequest extends TransportRequest {

    }

    private static class TestResponse extends TransportResponse {

    }
}
