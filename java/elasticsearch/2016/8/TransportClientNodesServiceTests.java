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

import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.admin.cluster.node.liveness.LivenessResponse;
import org.elasticsearch.action.admin.cluster.node.liveness.TransportLivenessAction;
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.transport.LocalTransportAddress;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.threadpool.TestThreadPool;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportResponseHandler;
import org.elasticsearch.transport.TransportException;
import org.elasticsearch.transport.TransportRequest;
import org.elasticsearch.transport.TransportRequestOptions;
import org.elasticsearch.transport.TransportResponse;
import org.elasticsearch.transport.TransportResponseHandler;
import org.elasticsearch.transport.TransportService;

import java.io.Closeable;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.instanceOf;
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
        private final int nodesCount;

        TestIteration() {
            Settings settings = Settings.builder().put("cluster.name", "test").build();
            ClusterName clusterName = ClusterName.CLUSTER_NAME_SETTING.get(settings);
            threadPool = new TestThreadPool("transport-client-nodes-service-tests");
            transport = new FailAndRetryMockTransport<TestResponse>(random(), clusterName) {
                @Override
                public List<String> getLocalAddresses() {
                    return Collections.emptyList();
                }

                @Override
                protected TestResponse newResponse() {
                    return  new TestResponse();
                }
            };
            transportService = new TransportService(settings, transport, threadPool) {
                @Override
                public <T extends TransportResponse> void sendRequest(DiscoveryNode node, String action,
                                                                      TransportRequest request, final TransportResponseHandler<T> handler) {
                    if (TransportLivenessAction.NAME.equals(action)) {
                        super.sendRequest(node, action, request, wrapLivenessResponseHandler(handler, node, clusterName));
                    } else {
                        super.sendRequest(node, action, request, handler);
                    }
                }

                @Override
                public <T extends TransportResponse> void sendRequest(DiscoveryNode node, String action, TransportRequest request,
                                                                      TransportRequestOptions options,
                                                                      TransportResponseHandler<T> handler) {
                    if (TransportLivenessAction.NAME.equals(action)) {
                        super.sendRequest(node, action, request, options, wrapLivenessResponseHandler(handler, node, clusterName));
                    } else {
                        super.sendRequest(node, action, request, options, handler);
                    }
                }
            };
            transportService.start();
            transportService.acceptIncomingRequests();
            transportClientNodesService =
                    new TransportClientNodesService(settings, transportService, threadPool);
            this.nodesCount = randomIntBetween(1, 10);
            for (int i = 0; i < nodesCount; i++) {
                transportClientNodesService.addTransportAddresses(new LocalTransportAddress("node" + i));
            }
            transport.endConnectMode();
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
                                    new LocalTransportAddress("liveness-address-" + node.getId()), node.getAttributes(), node.getRoles(),
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

                assertThat(iteration.transport.triedNodes().size(), lessThanOrEqualTo(iteration.nodesCount));
                assertThat(iteration.transport.triedNodes().size(), equalTo(iteration.transport.connectTransportExceptions() +
                        iteration.transport.failures() + iteration.transport.successes()));
            }
        }
    }

    public void testConnectedNodes() {
        int iters = iterations(10, 100);
        for (int i = 0; i <iters; i++) {
            try(final TestIteration iteration = new TestIteration()) {
                assertThat(iteration.transportClientNodesService.connectedNodes().size(), lessThanOrEqualTo(iteration.nodesCount));
                for (DiscoveryNode discoveryNode : iteration.transportClientNodesService.connectedNodes()) {
                    assertThat(discoveryNode.getHostName(), startsWith("liveness-"));
                    assertThat(discoveryNode.getHostAddress(), startsWith("liveness-"));
                    assertThat(discoveryNode.getAddress(), instanceOf(LocalTransportAddress.class));
                    LocalTransportAddress localTransportAddress = (LocalTransportAddress) discoveryNode.getAddress();
                    //the original listed transport address is kept rather than the one returned from the liveness api
                    assertThat(localTransportAddress.id(), startsWith("node"));
                }
            }
        }
    }

    public static class TestRequest extends TransportRequest {

    }

    private static class TestResponse extends TransportResponse {

    }
}
