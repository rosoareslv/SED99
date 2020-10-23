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

package org.elasticsearch.action.support.replication;

import org.elasticsearch.ElasticsearchException;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.UnavailableShardsException;
import org.elasticsearch.action.admin.indices.close.CloseIndexRequest;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.ActiveShardCount;
import org.elasticsearch.action.support.PlainActionFuture;
import org.elasticsearch.action.support.replication.ReplicationOperation.ReplicaResponse;
import org.elasticsearch.client.transport.NoNodeAvailableException;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.ESAllocationTestCase;
import org.elasticsearch.cluster.action.shard.ShardStateAction;
import org.elasticsearch.cluster.block.ClusterBlock;
import org.elasticsearch.cluster.block.ClusterBlockException;
import org.elasticsearch.cluster.block.ClusterBlockLevel;
import org.elasticsearch.cluster.block.ClusterBlocks;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.routing.AllocationId;
import org.elasticsearch.cluster.routing.IndexShardRoutingTable;
import org.elasticsearch.cluster.routing.RoutingNode;
import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.cluster.routing.ShardRoutingState;
import org.elasticsearch.cluster.routing.TestShardRouting;
import org.elasticsearch.cluster.routing.allocation.AllocationService;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.Nullable;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.lease.Releasable;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.index.Index;
import org.elasticsearch.index.IndexNotFoundException;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.engine.EngineClosedException;
import org.elasticsearch.index.shard.IndexShard;
import org.elasticsearch.index.shard.IndexShardClosedException;
import org.elasticsearch.index.shard.IndexShardState;
import org.elasticsearch.index.shard.ShardId;
import org.elasticsearch.index.shard.ShardNotFoundException;
import org.elasticsearch.indices.IndexClosedException;
import org.elasticsearch.indices.IndicesService;
import org.elasticsearch.indices.cluster.ClusterStateChanges;
import org.elasticsearch.node.NodeClosedException;
import org.elasticsearch.rest.RestStatus;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.transport.CapturingTransport;
import org.elasticsearch.threadpool.TestThreadPool;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportChannel;
import org.elasticsearch.transport.TransportException;
import org.elasticsearch.transport.TransportRequest;
import org.elasticsearch.transport.TransportResponse;
import org.elasticsearch.transport.TransportResponseOptions;
import org.elasticsearch.transport.TransportService;
import org.hamcrest.Matcher;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;

import java.io.IOException;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;

import static org.elasticsearch.action.support.replication.ClusterStateCreationUtils.state;
import static org.elasticsearch.action.support.replication.ClusterStateCreationUtils.stateWithActivePrimary;
import static org.elasticsearch.cluster.metadata.IndexMetaData.SETTING_WAIT_FOR_ACTIVE_SHARDS;
import static org.elasticsearch.test.ClusterServiceUtils.createClusterService;
import static org.elasticsearch.test.ClusterServiceUtils.setState;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.Matchers.arrayWithSize;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyInt;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class TransportReplicationActionTests extends ESTestCase {

    /**
     * takes a request that was sent by a {@link TransportReplicationAction} and captured
     * and returns the underlying request if it's wrapped or the original (cast to the expected type).
     *
     * This will throw a {@link ClassCastException} if the request is of the wrong type.
     */
    public static <R extends ReplicationRequest> R resolveRequest(TransportRequest requestOrWrappedRequest) {
        if (requestOrWrappedRequest instanceof TransportReplicationAction.ConcreteShardRequest) {
            requestOrWrappedRequest = ((TransportReplicationAction.ConcreteShardRequest<?>)requestOrWrappedRequest).getRequest();
        }
        return (R) requestOrWrappedRequest;
    }

    private static ThreadPool threadPool;

    private ClusterService clusterService;
    private TransportService transportService;
    private CapturingTransport transport;
    private Action action;
    private ShardStateAction shardStateAction;

    /* *
    * TransportReplicationAction needs an instance of IndexShard to count operations.
    * indexShards is reset to null before each test and will be initialized upon request in the tests.
    */

    @BeforeClass
    public static void beforeClass() {
        threadPool = new TestThreadPool("ShardReplicationTests");
    }

    @Override
    @Before
    public void setUp() throws Exception {
        super.setUp();
        transport = new CapturingTransport();
        clusterService = createClusterService(threadPool);
        transportService = new TransportService(clusterService.getSettings(), transport, threadPool,
            TransportService.NOOP_TRANSPORT_INTERCEPTOR, null);
        transportService.start();
        transportService.acceptIncomingRequests();
        shardStateAction = new ShardStateAction(Settings.EMPTY, clusterService, transportService, null, null, threadPool);
        action = new Action(Settings.EMPTY, "testAction", transportService, clusterService, shardStateAction, threadPool);
    }

    @After
    public void tearDown() throws Exception {
        super.tearDown();
        clusterService.close();
    }

    @AfterClass
    public static void afterClass() {
        ThreadPool.terminate(threadPool, 30, TimeUnit.SECONDS);
        threadPool = null;
    }

    <T> void assertListenerThrows(String msg, PlainActionFuture<T> listener, Class<?> klass) throws InterruptedException {
        try {
            listener.get();
            fail(msg);
        } catch (ExecutionException ex) {
            assertThat(ex.getCause(), instanceOf(klass));
        }
    }

    public void testBlocks() throws ExecutionException, InterruptedException {
        Request request = new Request();
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();
        Action action = new Action(Settings.EMPTY, "testActionWithBlocks", transportService, clusterService, shardStateAction, threadPool) {
            @Override
            protected ClusterBlockLevel globalBlockLevel() {
                return ClusterBlockLevel.WRITE;
            }
        };

        ClusterBlocks.Builder block = ClusterBlocks.builder()
            .addGlobalBlock(new ClusterBlock(1, "non retryable", false, true, RestStatus.SERVICE_UNAVAILABLE, ClusterBlockLevel.ALL));
        setState(clusterService, ClusterState.builder(clusterService.state()).blocks(block));
        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        assertListenerThrows("primary phase should fail operation", listener, ClusterBlockException.class);
        assertPhase(task, "failed");

        block = ClusterBlocks.builder()
            .addGlobalBlock(new ClusterBlock(1, "retryable", true, true, RestStatus.SERVICE_UNAVAILABLE, ClusterBlockLevel.ALL));
        setState(clusterService, ClusterState.builder(clusterService.state()).blocks(block));
        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(task, new Request().timeout("5ms"), listener);
        reroutePhase.run();
        assertListenerThrows("failed to timeout on retryable block", listener, ClusterBlockException.class);
        assertPhase(task, "failed");
        assertFalse(request.isRetrySet.get());

        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(task, request = new Request(), listener);
        reroutePhase.run();
        assertFalse("primary phase should wait on retryable block", listener.isDone());
        assertPhase(task, "waiting_for_retry");
        assertTrue(request.isRetrySet.get());

        block = ClusterBlocks.builder()
            .addGlobalBlock(new ClusterBlock(1, "non retryable", false, true, RestStatus.SERVICE_UNAVAILABLE, ClusterBlockLevel.ALL));
        setState(clusterService, ClusterState.builder(clusterService.state()).blocks(block));
        assertListenerThrows("primary phase should fail operation when moving from a retryable block to a non-retryable one", listener,
            ClusterBlockException.class);
        assertIndexShardUninitialized();

        action = new Action(Settings.EMPTY, "testActionWithNoBlocks", transportService, clusterService, shardStateAction, threadPool) {
            @Override
            protected ClusterBlockLevel globalBlockLevel() {
                return null;
            }
        };
        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(task, new Request().timeout("5ms"), listener);
        reroutePhase.run();
        assertListenerThrows("should fail with an IndexNotFoundException when no blocks checked", listener, IndexNotFoundException.class);
    }

    public void assertIndexShardUninitialized() {
        assertEquals(0, count.get());
    }

    public void testNotStartedPrimary() throws InterruptedException, ExecutionException {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        // no replicas in oder to skip the replication part
        setState(clusterService, state(index, true,
            randomBoolean() ? ShardRoutingState.INITIALIZING : ShardRoutingState.UNASSIGNED));
        ReplicationTask task = maybeTask();

        logger.debug("--> using initial state:\n{}", clusterService.state());

        Request request = new Request(shardId).timeout("1ms");
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        assertListenerThrows("unassigned primary didn't cause a timeout", listener, UnavailableShardsException.class);
        assertPhase(task, "failed");
        assertTrue(request.isRetrySet.get());

        request = new Request(shardId);
        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        assertFalse("unassigned primary didn't cause a retry", listener.isDone());
        assertPhase(task, "waiting_for_retry");
        assertTrue(request.isRetrySet.get());

        setState(clusterService, state(index, true, ShardRoutingState.STARTED));
        logger.debug("--> primary assigned state:\n{}", clusterService.state());

        final IndexShardRoutingTable shardRoutingTable = clusterService.state().routingTable().index(index).shard(shardId.id());
        final String primaryNodeId = shardRoutingTable.primaryShard().currentNodeId();
        final List<CapturingTransport.CapturedRequest> capturedRequests =
            transport.getCapturedRequestsByTargetNodeAndClear().get(primaryNodeId);
        assertThat(capturedRequests, notNullValue());
        assertThat(capturedRequests.size(), equalTo(1));
        assertThat(capturedRequests.get(0).action, equalTo("testAction[p]"));
        assertIndexShardCounter(0);
    }

    /**
     * When relocating a primary shard, there is a cluster state update at the end of relocation where the active primary is switched from
     * the relocation source to the relocation target. If relocation source receives and processes this cluster state
     * before the relocation target, there is a time span where relocation source believes active primary to be on
     * relocation target and relocation target believes active primary to be on relocation source. This results in replication
     * requests being sent back and forth.
     * <p>
     * This test checks that replication request is not routed back from relocation target to relocation source in case of
     * stale index routing table on relocation target.
     */
    public void testNoRerouteOnStaleClusterState() throws InterruptedException, ExecutionException {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ClusterState state = state(index, true, ShardRoutingState.RELOCATING);
        String relocationTargetNode = state.getRoutingTable().shardRoutingTable(shardId).primaryShard().relocatingNodeId();
        state = ClusterState.builder(state).nodes(DiscoveryNodes.builder(state.nodes()).localNodeId(relocationTargetNode)).build();
        setState(clusterService, state);
        logger.debug("--> relocation ongoing state:\n{}", clusterService.state());

        Request request = new Request(shardId).timeout("1ms").routedBasedOnClusterVersion(clusterService.state().version() + 1);
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        Action.ReroutePhase reroutePhase = action.new ReroutePhase(null, request, listener);
        reroutePhase.run();
        assertListenerThrows("cluster state too old didn't cause a timeout", listener, UnavailableShardsException.class);
        assertTrue(request.isRetrySet.compareAndSet(true, false));

        request = new Request(shardId).routedBasedOnClusterVersion(clusterService.state().version() + 1);
        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(null, request, listener);
        reroutePhase.run();
        assertFalse("cluster state too old didn't cause a retry", listener.isDone());
        assertTrue(request.isRetrySet.get());

        // finish relocation
        ShardRouting relocationTarget = clusterService.state().getRoutingTable().shardRoutingTable(shardId)
            .shardsWithState(ShardRoutingState.INITIALIZING).get(0);
        AllocationService allocationService = ESAllocationTestCase.createAllocationService();
        ClusterState updatedState = allocationService.applyStartedShards(state, Collections.singletonList(relocationTarget));

        setState(clusterService, updatedState);
        logger.debug("--> relocation complete state:\n{}", clusterService.state());

        IndexShardRoutingTable shardRoutingTable = clusterService.state().routingTable().index(index).shard(shardId.id());
        final String primaryNodeId = shardRoutingTable.primaryShard().currentNodeId();
        final List<CapturingTransport.CapturedRequest> capturedRequests =
            transport.getCapturedRequestsByTargetNodeAndClear().get(primaryNodeId);
        assertThat(capturedRequests, notNullValue());
        assertThat(capturedRequests.size(), equalTo(1));
        assertThat(capturedRequests.get(0).action, equalTo("testAction[p]"));
        assertIndexShardCounter(0);
    }

    public void testUnknownIndexOrShardOnReroute() throws InterruptedException {
        final String index = "test";
        // no replicas in oder to skip the replication part
        setState(clusterService, state(index, true,
            randomBoolean() ? ShardRoutingState.INITIALIZING : ShardRoutingState.UNASSIGNED));
        logger.debug("--> using initial state:\n{}", clusterService.state());
        Request request = new Request(new ShardId("unknown_index", "_na_", 0)).timeout("1ms");
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();

        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        assertListenerThrows("must throw index not found exception", listener, IndexNotFoundException.class);
        assertPhase(task, "failed");
        assertTrue(request.isRetrySet.get());
        request = new Request(new ShardId(index, "_na_", 10)).timeout("1ms");
        listener = new PlainActionFuture<>();
        reroutePhase = action.new ReroutePhase(null, request, listener);
        reroutePhase.run();
        assertListenerThrows("must throw shard not found exception", listener, ShardNotFoundException.class);
        assertFalse(request.isRetrySet.get()); //TODO I'd have expected this to be true but we fail too early?

    }

    public void testClosedIndexOnReroute() throws InterruptedException {
        final String index = "test";
        // no replicas in oder to skip the replication part
        setState(clusterService, new ClusterStateChanges(xContentRegistry()).closeIndices(state(index, true, ShardRoutingState.UNASSIGNED),
                new CloseIndexRequest(index)));
        logger.debug("--> using initial state:\n{}", clusterService.state());
        Request request = new Request(new ShardId("test", "_na_", 0)).timeout("1ms");
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();

        ClusterBlockLevel indexBlockLevel = randomBoolean() ? ClusterBlockLevel.WRITE : null;
        Action action = new Action(Settings.EMPTY, "testActionWithBlocks", transportService, clusterService, shardStateAction, threadPool) {
            @Override
            protected ClusterBlockLevel indexBlockLevel() {
                return indexBlockLevel;
            }
        };
        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        if (indexBlockLevel == ClusterBlockLevel.WRITE) {
            assertListenerThrows("must throw block exception", listener, ClusterBlockException.class);
        } else {
            assertListenerThrows("must throw index closed exception", listener, IndexClosedException.class);
        }
        assertPhase(task, "failed");
        assertFalse(request.isRetrySet.get());
    }

    public void testStalePrimaryShardOnReroute() throws InterruptedException {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        // no replicas in order to skip the replication part
        setState(clusterService, stateWithActivePrimary(index, true, randomInt(3)));
        logger.debug("--> using initial state:\n{}", clusterService.state());
        Request request = new Request(shardId);
        boolean timeout = randomBoolean();
        if (timeout) {
            request.timeout("0s");
        } else {
            request.timeout("1h");
        }
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();

        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        CapturingTransport.CapturedRequest[] capturedRequests = transport.getCapturedRequestsAndClear();
        assertThat(capturedRequests, arrayWithSize(1));
        assertThat(capturedRequests[0].action, equalTo("testAction[p]"));
        assertPhase(task, "waiting_on_primary");
        assertFalse(request.isRetrySet.get());
        transport.handleRemoteError(capturedRequests[0].requestId, randomRetryPrimaryException(shardId));


        if (timeout) {
            // we always try at least one more time on timeout
            assertThat(listener.isDone(), equalTo(false));
            capturedRequests = transport.getCapturedRequestsAndClear();
            assertThat(capturedRequests, arrayWithSize(1));
            assertThat(capturedRequests[0].action, equalTo("testAction[p]"));
            assertPhase(task, "waiting_on_primary");
            transport.handleRemoteError(capturedRequests[0].requestId, randomRetryPrimaryException(shardId));
            assertListenerThrows("must throw index not found exception", listener, ElasticsearchException.class);
            assertPhase(task, "failed");
        } else {
            assertThat(listener.isDone(), equalTo(false));
            // generate a CS change
            setState(clusterService, clusterService.state());
            capturedRequests = transport.getCapturedRequestsAndClear();
            assertThat(capturedRequests, arrayWithSize(1));
            assertThat(capturedRequests[0].action, equalTo("testAction[p]"));
        }
    }

    private ElasticsearchException randomRetryPrimaryException(ShardId shardId) {
        return randomFrom(
            new ShardNotFoundException(shardId),
            new IndexNotFoundException(shardId.getIndex()),
            new IndexShardClosedException(shardId),
            new EngineClosedException(shardId),
            new ReplicationOperation.RetryOnPrimaryException(shardId, "hello")
        );
    }

    public void testRoutePhaseExecutesRequest() {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ReplicationTask task = maybeTask();

        setState(clusterService, stateWithActivePrimary(index, randomBoolean(), 3));
        logger.debug("using state: \n{}", clusterService.state());

        final IndexShardRoutingTable shardRoutingTable = clusterService.state().routingTable().index(index).shard(shardId.id());
        final String primaryNodeId = shardRoutingTable.primaryShard().currentNodeId();
        Request request = new Request(shardId);
        PlainActionFuture<Response> listener = new PlainActionFuture<>();

        Action.ReroutePhase reroutePhase = action.new ReroutePhase(task, request, listener);
        reroutePhase.run();
        assertThat(request.shardId(), equalTo(shardId));
        logger.info("--> primary is assigned to [{}], checking request forwarded", primaryNodeId);
        final List<CapturingTransport.CapturedRequest> capturedRequests =
            transport.getCapturedRequestsByTargetNodeAndClear().get(primaryNodeId);
        assertThat(capturedRequests, notNullValue());
        assertThat(capturedRequests.size(), equalTo(1));
        if (clusterService.state().nodes().getLocalNodeId().equals(primaryNodeId)) {
            assertThat(capturedRequests.get(0).action, equalTo("testAction[p]"));
            assertPhase(task, "waiting_on_primary");
        } else {
            assertThat(capturedRequests.get(0).action, equalTo("testAction"));
            assertPhase(task, "rerouted");
        }
        assertFalse(request.isRetrySet.get());
        assertIndexShardUninitialized();
    }

    public void testPrimaryPhaseExecutesOrDelegatesRequestToRelocationTarget() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ClusterState state = stateWithActivePrimary(index, true, randomInt(5));
        setState(clusterService, state);
        Request request = new Request(shardId).timeout("1ms");
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();
        AtomicBoolean executed = new AtomicBoolean();

        ShardRouting primaryShard = state.getRoutingTable().shardRoutingTable(shardId).primaryShard();
        boolean executeOnPrimary = true;
        // whether shard has been marked as relocated already (i.e. relocation completed)
        if (primaryShard.relocating() && randomBoolean()) {
            isRelocated.set(true);
            executeOnPrimary = false;
        }
        action.new AsyncPrimaryAction(request, primaryShard.allocationId().getId(), createTransportChannel(listener), task) {
            @Override
            protected ReplicationOperation<Request, Request, Action.PrimaryResult> createReplicatedOperation(Request request,
                    ActionListener<Action.PrimaryResult> actionListener, Action.PrimaryShardReference primaryShardReference,
                    boolean executeOnReplicas) {
                return new NoopReplicationOperation(request, actionListener) {
                    public void execute() throws Exception {
                        assertPhase(task, "primary");
                        assertFalse(executed.getAndSet(true));
                        super.execute();
                    }
                };
            }
        }.run();
        if (executeOnPrimary) {
            assertTrue(executed.get());
            assertTrue(listener.isDone());
            listener.get();
            assertPhase(task, "finished");
            assertFalse(request.isRetrySet.get());
        } else {
            assertFalse(executed.get());
            assertIndexShardCounter(0);  // it should have been freed.
            final List<CapturingTransport.CapturedRequest> requests =
                transport.capturedRequestsByTargetNode().get(primaryShard.relocatingNodeId());
            assertThat(requests, notNullValue());
            assertThat(requests.size(), equalTo(1));
            assertThat("primary request was not delegated to relocation target", requests.get(0).action, equalTo("testAction[p]"));
            assertPhase(task, "primary_delegation");
            transport.handleResponse(requests.get(0).requestId, new Response());
            assertTrue(listener.isDone());
            listener.get();
            assertPhase(task, "finished");
            assertFalse(request.isRetrySet.get());
        }
    }

    public void testPrimaryPhaseExecutesDelegatedRequestOnRelocationTarget() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ClusterState state = state(index, true, ShardRoutingState.RELOCATING);
        final ShardRouting primaryShard = state.getRoutingTable().shardRoutingTable(shardId).primaryShard();
        String primaryTargetNodeId = primaryShard.relocatingNodeId();
        // simulate execution of the primary phase on the relocation target node
        state = ClusterState.builder(state).nodes(DiscoveryNodes.builder(state.nodes()).localNodeId(primaryTargetNodeId)).build();
        setState(clusterService, state);
        Request request = new Request(shardId).timeout("1ms");
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();
        AtomicBoolean executed = new AtomicBoolean();
        action.new AsyncPrimaryAction(request, primaryShard.allocationId().getRelocationId(), createTransportChannel(listener), task) {
            @Override
            protected ReplicationOperation<Request, Request, Action.PrimaryResult> createReplicatedOperation(Request request,
                    ActionListener<Action.PrimaryResult> actionListener, Action.PrimaryShardReference primaryShardReference,
                    boolean executeOnReplicas) {
                return new NoopReplicationOperation(request, actionListener) {
                    public void execute() throws Exception {
                        assertPhase(task, "primary");
                        assertFalse(executed.getAndSet(true));
                        super.execute();
                    }
                };
            }

            @Override
            public void onFailure(Exception e) {
                throw new RuntimeException(e);
            }
        }.run();
        assertThat(executed.get(), equalTo(true));
        assertPhase(task, "finished");
        assertFalse(request.isRetrySet.get());
    }

    public void testPrimaryReference() throws Exception {
        final IndexShard shard = mock(IndexShard.class);
        final long primaryTerm = 1 + randomInt(200);
        when(shard.getPrimaryTerm()).thenReturn(primaryTerm);

        AtomicBoolean closed = new AtomicBoolean();
        Releasable releasable = () -> {
            if (closed.compareAndSet(false, true) == false) {
                fail("releasable is closed twice");
            }
        };
        Action.PrimaryShardReference primary = action.new PrimaryShardReference(shard, releasable);
        final Request request = new Request();
        Request replicaRequest = primary.perform(request).replicaRequest;

        assertThat(replicaRequest.primaryTerm(), equalTo(primaryTerm));

        final ElasticsearchException exception = new ElasticsearchException("testing");
        primary.failShard("test", exception);

        verify(shard).failShard("test", exception);

        primary.close();

        assertTrue(closed.get());
    }

    public void testReplicaProxy() throws InterruptedException, ExecutionException {
        Action.ReplicasProxy proxy = action.new ReplicasProxy();
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ClusterState state = stateWithActivePrimary(index, true, 1 + randomInt(3), randomInt(2));
        logger.info("using state: {}", state);
        setState(clusterService, state);

        // check that at unknown node fails
        PlainActionFuture<ReplicaResponse> listener = new PlainActionFuture<>();
        proxy.performOn(
            TestShardRouting.newShardRouting(shardId, "NOT THERE", false, randomFrom(ShardRoutingState.values())),
            new Request(), listener);
        assertTrue(listener.isDone());
        assertListenerThrows("non existent node should throw a NoNodeAvailableException", listener, NoNodeAvailableException.class);

        final IndexShardRoutingTable shardRoutings = state.routingTable().shardRoutingTable(shardId);
        final ShardRouting replica = randomFrom(shardRoutings.replicaShards().stream()
            .filter(ShardRouting::assignedToNode).collect(Collectors.toList()));
        listener = new PlainActionFuture<>();
        proxy.performOn(replica, new Request(), listener);
        assertFalse(listener.isDone());

        CapturingTransport.CapturedRequest[] captures = transport.getCapturedRequestsAndClear();
        assertThat(captures, arrayWithSize(1));
        if (randomBoolean()) {
            final TransportReplicationAction.ReplicaResponse response =
                new TransportReplicationAction.ReplicaResponse(randomAsciiOfLength(10), randomLong());
            transport.handleResponse(captures[0].requestId, response);
            assertTrue(listener.isDone());
            assertThat(listener.get(), equalTo(response));
        } else if (randomBoolean()) {
            transport.handleRemoteError(captures[0].requestId, new ElasticsearchException("simulated"));
            assertTrue(listener.isDone());
            assertListenerThrows("listener should reflect remote error", listener, ElasticsearchException.class);
        } else {
            transport.handleError(captures[0].requestId, new TransportException("simulated"));
            assertTrue(listener.isDone());
            assertListenerThrows("listener should reflect remote error", listener, TransportException.class);
        }

        AtomicReference<Throwable> failure = new AtomicReference<>();
        AtomicReference<Throwable> ignoredFailure = new AtomicReference<>();
        AtomicBoolean success = new AtomicBoolean();
        proxy.failShard(replica, randomIntBetween(1, 10), "test", new ElasticsearchException("simulated"),
            () -> success.set(true), failure::set, ignoredFailure::set
        );
        CapturingTransport.CapturedRequest[] shardFailedRequests = transport.getCapturedRequestsAndClear();
        assertEquals(1, shardFailedRequests.length);
        CapturingTransport.CapturedRequest shardFailedRequest = shardFailedRequests[0];
        ShardStateAction.ShardEntry shardEntry = (ShardStateAction.ShardEntry) shardFailedRequest.request;
        // the shard the request was sent to and the shard to be failed should be the same
        assertEquals(shardEntry.getShardId(), replica.shardId());
        assertEquals(shardEntry.getAllocationId(), replica.allocationId().getId());
        if (randomBoolean()) {
            // simulate success
            transport.handleResponse(shardFailedRequest.requestId, TransportResponse.Empty.INSTANCE);
            assertTrue(success.get());
            assertNull(failure.get());
            assertNull(ignoredFailure.get());

        } else if (randomBoolean()) {
            // simulate the primary has been demoted
            transport.handleRemoteError(shardFailedRequest.requestId,
                new ShardStateAction.NoLongerPrimaryShardException(replica.shardId(),
                    "shard-failed-test"));
            assertFalse(success.get());
            assertNotNull(failure.get());
            assertNull(ignoredFailure.get());

        } else {
            // simulated an "ignored" exception
            transport.handleRemoteError(shardFailedRequest.requestId,
                new NodeClosedException(state.nodes().getLocalNode()));
            assertFalse(success.get());
            assertNull(failure.get());
            assertNotNull(ignoredFailure.get());
        }
    }

    public void testShadowIndexDisablesReplication() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);

        ClusterState state = stateWithActivePrimary(index, true, randomInt(5));
        MetaData.Builder metaData = MetaData.builder(state.metaData());
        Settings.Builder settings = Settings.builder().put(metaData.get(index).getSettings());
        settings.put(IndexMetaData.SETTING_SHADOW_REPLICAS, true);
        metaData.put(IndexMetaData.builder(metaData.get(index)).settings(settings));
        state = ClusterState.builder(state).metaData(metaData).build();
        setState(clusterService, state);
        AtomicBoolean executed = new AtomicBoolean();
        ShardRouting primaryShard = state.routingTable().shardRoutingTable(shardId).primaryShard();
        action.new AsyncPrimaryAction(new Request(shardId), primaryShard.allocationId().getId(),
            createTransportChannel(new PlainActionFuture<>()), null) {
            @Override
            protected ReplicationOperation<Request, Request, Action.PrimaryResult> createReplicatedOperation(Request request,
                    ActionListener<Action.PrimaryResult> actionListener, Action.PrimaryShardReference primaryShardReference,
                    boolean executeOnReplicas) {
                assertFalse(executeOnReplicas);
                assertFalse(executed.getAndSet(true));
                return new NoopReplicationOperation(request, actionListener);
            }
        }.run();
        assertThat(executed.get(), equalTo(true));
    }

    public void testSeqNoIsSetOnPrimary() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        // we use one replica to check the primary term was set on the operation and sent to the replica
        setState(clusterService,
            state(index, true, ShardRoutingState.STARTED, randomFrom(ShardRoutingState.INITIALIZING, ShardRoutingState.STARTED)));
        logger.debug("--> using initial state:\n{}", clusterService.state());
        final ShardRouting routingEntry = clusterService.state().getRoutingTable().index("test").shard(0).primaryShard();
        Request request = new Request(shardId);
        TransportReplicationAction.ConcreteShardRequest<Request> concreteShardRequest =
            new TransportReplicationAction.ConcreteShardRequest<>(request, routingEntry.allocationId().getId());
        PlainActionFuture<Response> listener = new PlainActionFuture<>();


        final IndexShard shard = mock(IndexShard.class);
        long primaryTerm = clusterService.state().getMetaData().index(index).primaryTerm(0);
        when(shard.getPrimaryTerm()).thenReturn(primaryTerm);
        when(shard.routingEntry()).thenReturn(routingEntry);

        AtomicBoolean closed = new AtomicBoolean();
        Releasable releasable = () -> {
            if (closed.compareAndSet(false, true) == false) {
                fail("releasable is closed twice");
            }
        };

        Action action =
            new Action(Settings.EMPTY, "testSeqNoIsSetOnPrimary", transportService, clusterService, shardStateAction, threadPool);

        TransportReplicationAction<Request, Request, Response>.PrimaryOperationTransportHandler primaryPhase =
            action.new PrimaryOperationTransportHandler();
        primaryPhase.messageReceived(concreteShardRequest, createTransportChannel(listener), null);
        CapturingTransport.CapturedRequest[] requestsToReplicas = transport.capturedRequests();
        assertThat(requestsToReplicas, arrayWithSize(1));
        assertThat(((TransportReplicationAction.ConcreteShardRequest<Request>) requestsToReplicas[0].request).getRequest().primaryTerm(),
            equalTo(primaryTerm));
    }

    public void testCounterOnPrimary() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        // no replica, we only want to test on primary
        final ClusterState state = state(index, true, ShardRoutingState.STARTED);
        setState(clusterService, state);
        logger.debug("--> using initial state:\n{}", clusterService.state());
        final ShardRouting primaryShard = state.routingTable().shardRoutingTable(shardId).primaryShard();
        Request request = new Request(shardId);
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        ReplicationTask task = maybeTask();
        int i = randomInt(3);
        final boolean throwExceptionOnCreation = i == 1;
        final boolean throwExceptionOnRun = i == 2;
        final boolean respondWithError = i == 3;
        action.new AsyncPrimaryAction(request, primaryShard.allocationId().getId(), createTransportChannel(listener), task) {
            @Override
            protected ReplicationOperation<Request, Request, Action.PrimaryResult> createReplicatedOperation(Request request,
                    ActionListener<Action.PrimaryResult> actionListener, Action.PrimaryShardReference primaryShardReference,
                    boolean executeOnReplicas) {
                assertIndexShardCounter(1);
                if (throwExceptionOnCreation) {
                    throw new ElasticsearchException("simulated exception, during createReplicatedOperation");
                }
                return new NoopReplicationOperation(request, actionListener) {
                    @Override
                    public void execute() throws Exception {
                        assertIndexShardCounter(1);
                        assertPhase(task, "primary");
                        if (throwExceptionOnRun) {
                            throw new ElasticsearchException("simulated exception, during performOnPrimary");
                        } else if (respondWithError) {
                            this.resultListener.onFailure(new ElasticsearchException("simulated exception, as a response"));
                        } else {
                            super.execute();
                        }
                    }
                };
            }
        }.run();
        assertIndexShardCounter(0);
        assertTrue(listener.isDone());
        assertPhase(task, "finished");

        try {
            listener.get();
        } catch (ExecutionException e) {
            if (throwExceptionOnCreation || throwExceptionOnRun || respondWithError) {
                Throwable cause = e.getCause();
                assertThat(cause, instanceOf(ElasticsearchException.class));
                assertThat(cause.getMessage(), containsString("simulated"));
            } else {
                throw e;
            }
        }
    }

    public void testReplicasCounter() throws Exception {
        final ShardId shardId = new ShardId("test", "_na_", 0);
        final ClusterState state = state(shardId.getIndexName(), true, ShardRoutingState.STARTED, ShardRoutingState.STARTED);
        setState(clusterService, state);
        final ShardRouting replicaRouting = state.getRoutingTable().shardRoutingTable(shardId).replicaShards().get(0);
        boolean throwException = randomBoolean();
        final ReplicationTask task = maybeTask();
        Action action = new Action(Settings.EMPTY, "testActionWithExceptions", transportService, clusterService, shardStateAction,
            threadPool) {
            @Override
            protected ReplicaResult shardOperationOnReplica(Request request, IndexShard replica) {
                assertIndexShardCounter(1);
                assertPhase(task, "replica");
                if (throwException) {
                    throw new ElasticsearchException("simulated");
                }
                return new ReplicaResult();
            }
        };
        final Action.ReplicaOperationTransportHandler replicaOperationTransportHandler = action.new ReplicaOperationTransportHandler();
        try {
            replicaOperationTransportHandler.messageReceived(
                new TransportReplicationAction.ConcreteShardRequest<>(
                        new Request().setShardId(shardId), replicaRouting.allocationId().getId()),
                createTransportChannel(new PlainActionFuture<>()), task);
        } catch (ElasticsearchException e) {
            assertThat(e.getMessage(), containsString("simulated"));
            assertTrue(throwException);
        }
        assertPhase(task, "finished");
        // operation should have finished and counter decreased because no outstanding replica requests
        assertIndexShardCounter(0);
    }

    /**
     * This test ensures that replication operations adhere to the {@link IndexMetaData#SETTING_WAIT_FOR_ACTIVE_SHARDS} setting
     * when the request is using the default value for waitForActiveShards.
     */
    public void testDefaultWaitForActiveShardsUsesIndexSetting() throws Exception {
        final String indexName = "test";
        final ShardId shardId = new ShardId(indexName, "_na_", 0);

        // test wait_for_active_shards index setting used when the default is set on the request
        int numReplicas = randomIntBetween(0, 5);
        int idxSettingWaitForActiveShards = randomIntBetween(0, numReplicas + 1);
        ClusterState state = stateWithActivePrimary(indexName, randomBoolean(), numReplicas);
        IndexMetaData indexMetaData = state.metaData().index(indexName);
        Settings indexSettings = Settings.builder().put(indexMetaData.getSettings())
                                     .put(SETTING_WAIT_FOR_ACTIVE_SHARDS.getKey(), Integer.toString(idxSettingWaitForActiveShards))
                                     .build();
        MetaData.Builder metaDataBuilder = MetaData.builder(state.metaData())
                                               .put(IndexMetaData.builder(indexMetaData).settings(indexSettings).build(), true);
        state = ClusterState.builder(state).metaData(metaDataBuilder).build();
        setState(clusterService, state);
        Request request = new Request(shardId).waitForActiveShards(ActiveShardCount.DEFAULT); // set to default so index settings are used
        action.resolveRequest(state.metaData(), state.metaData().index(indexName), request);
        assertEquals(ActiveShardCount.from(idxSettingWaitForActiveShards), request.waitForActiveShards());

        // test wait_for_active_shards when default not set on the request (request value should be honored over index setting)
        int requestWaitForActiveShards = randomIntBetween(0, numReplicas + 1);
        request = new Request(shardId).waitForActiveShards(ActiveShardCount.from(requestWaitForActiveShards));
        action.resolveRequest(state.metaData(), state.metaData().index(indexName), request);
        assertEquals(ActiveShardCount.from(requestWaitForActiveShards), request.waitForActiveShards());
    }

    /** test that a primary request is rejected if it arrives at a shard with a wrong allocation id */
    public void testPrimaryActionRejectsWrongAid() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        setState(clusterService, state(index, true, ShardRoutingState.STARTED));
        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        Request request = new Request(shardId).timeout("1ms");
            action.new PrimaryOperationTransportHandler().messageReceived(
                new TransportReplicationAction.ConcreteShardRequest<>(request, "_not_a_valid_aid_"),
                createTransportChannel(listener), maybeTask()
            );
        try {
            listener.get();
            fail("using a wrong aid didn't fail the operation");
        } catch (ExecutionException execException) {
            Throwable throwable = execException.getCause();
            logger.debug("got exception:" , throwable);
            assertTrue(throwable.getClass() + " is not a retry exception", action.retryPrimaryException(throwable));
        }
    }

    /** test that a replica request is rejected if it arrives at a shard with a wrong allocation id */
    public void testReplicaActionRejectsWrongAid() throws Exception {
        final String index = "test";
        final ShardId shardId = new ShardId(index, "_na_", 0);
        ClusterState state = state(index, false, ShardRoutingState.STARTED, ShardRoutingState.STARTED);
        final ShardRouting replica = state.routingTable().shardRoutingTable(shardId).replicaShards().get(0);
        // simulate execution of the node holding the replica
        state = ClusterState.builder(state).nodes(DiscoveryNodes.builder(state.nodes()).localNodeId(replica.currentNodeId())).build();
        setState(clusterService, state);

        PlainActionFuture<Response> listener = new PlainActionFuture<>();
        Request request = new Request(shardId).timeout("1ms");
        action.new ReplicaOperationTransportHandler().messageReceived(
            new TransportReplicationAction.ConcreteShardRequest<>(request, "_not_a_valid_aid_"),
            createTransportChannel(listener), maybeTask()
        );
        try {
            listener.get();
            fail("using a wrong aid didn't fail the operation");
        } catch (ExecutionException execException) {
            Throwable throwable = execException.getCause();
            if (action.retryPrimaryException(throwable) == false) {
                throw new AssertionError("thrown exception is not retriable", throwable);
            }
            assertThat(throwable.getMessage(), containsString("_not_a_valid_aid_"));
        }
    }

    /**
     * test throwing a {@link org.elasticsearch.action.support.replication.TransportReplicationAction.RetryOnReplicaException}
     * causes a retry
     */
    public void testRetryOnReplica() throws Exception {
        final ShardId shardId = new ShardId("test", "_na_", 0);
        ClusterState state = state(shardId.getIndexName(), true, ShardRoutingState.STARTED, ShardRoutingState.STARTED);
        final ShardRouting replica = state.getRoutingTable().shardRoutingTable(shardId).replicaShards().get(0);
        // simulate execution of the node holding the replica
        state = ClusterState.builder(state).nodes(DiscoveryNodes.builder(state.nodes()).localNodeId(replica.currentNodeId())).build();
        setState(clusterService, state);
        AtomicBoolean throwException = new AtomicBoolean(true);
        final ReplicationTask task = maybeTask();
        Action action = new Action(Settings.EMPTY, "testActionWithExceptions", transportService, clusterService, shardStateAction,
            threadPool) {
            @Override
            protected ReplicaResult shardOperationOnReplica(Request request, IndexShard replica) {
                assertPhase(task, "replica");
                if (throwException.get()) {
                    throw new RetryOnReplicaException(shardId, "simulation");
                }
                return new ReplicaResult();
            }
        };
        final Action.ReplicaOperationTransportHandler replicaOperationTransportHandler = action.new ReplicaOperationTransportHandler();
        final PlainActionFuture<Response> listener = new PlainActionFuture<>();
        final Request request = new Request().setShardId(shardId);
        request.primaryTerm(state.metaData().getIndexSafe(shardId.getIndex()).primaryTerm(shardId.id()));
        replicaOperationTransportHandler.messageReceived(
                new TransportReplicationAction.ConcreteShardRequest<>(request, replica.allocationId().getId()),
                createTransportChannel(listener), task);
        if (listener.isDone()) {
            listener.get(); // fail with the exception if there
            fail("listener shouldn't be done");
        }

        // no retry yet
        List<CapturingTransport.CapturedRequest> capturedRequests =
            transport.getCapturedRequestsByTargetNodeAndClear().get(replica.currentNodeId());
        assertThat(capturedRequests, nullValue());

        // release the waiting
        throwException.set(false);
        setState(clusterService, state);

        capturedRequests = transport.getCapturedRequestsByTargetNodeAndClear().get(replica.currentNodeId());
        assertThat(capturedRequests, notNullValue());
        assertThat(capturedRequests.size(), equalTo(1));
        final CapturingTransport.CapturedRequest capturedRequest = capturedRequests.get(0);
        assertThat(capturedRequest.action, equalTo("testActionWithExceptions[r]"));
        assertThat(capturedRequest.request, instanceOf(TransportReplicationAction.ConcreteShardRequest.class));
        assertConcreteShardRequest(capturedRequest.request, request, replica.allocationId());
    }

    private void assertConcreteShardRequest(TransportRequest capturedRequest, Request expectedRequest, AllocationId expectedAllocationId) {
        final TransportReplicationAction.ConcreteShardRequest<?> concreteShardRequest =
            (TransportReplicationAction.ConcreteShardRequest<?>) capturedRequest;
        assertThat(concreteShardRequest.getRequest(), equalTo(expectedRequest));
        assertThat(((Request)concreteShardRequest.getRequest()).isRetrySet.get(), equalTo(true));
        assertThat(concreteShardRequest.getTargetAllocationID(), equalTo(expectedAllocationId.getId()));
    }


    private void assertIndexShardCounter(int expected) {
        assertThat(count.get(), equalTo(expected));
    }

    private final AtomicInteger count = new AtomicInteger(0);

    private final AtomicBoolean isRelocated = new AtomicBoolean(false);

    /**
     * Sometimes build a ReplicationTask for tracking the phase of the
     * TransportReplicationAction. Since TransportReplicationAction has to work
     * if the task as null just as well as if it is supplied this returns null
     * half the time.
     */
    private ReplicationTask maybeTask() {
        return random().nextBoolean() ? new ReplicationTask(0, null, null, null, null) : null;
    }

    /**
     * If the task is non-null this asserts that the phrase matches.
     */
    private void assertPhase(@Nullable ReplicationTask task, String phase) {
        assertPhase(task, equalTo(phase));
    }

    private void assertPhase(@Nullable ReplicationTask task, Matcher<String> phaseMatcher) {
        if (task != null) {
            assertThat(task.getPhase(), phaseMatcher);
        }
    }

    public static class Request extends ReplicationRequest<Request> {
        public AtomicBoolean processedOnPrimary = new AtomicBoolean();
        public AtomicInteger processedOnReplicas = new AtomicInteger();
        public AtomicBoolean isRetrySet = new AtomicBoolean(false);

        public Request() {
        }

        Request(ShardId shardId) {
            this();
            this.shardId = shardId;
            this.index = shardId.getIndexName();
            this.waitForActiveShards = ActiveShardCount.NONE;
            // keep things simple
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
        }

        @Override
        public void onRetry() {
            super.onRetry();
            isRetrySet.set(true);
        }

        @Override
        public String toString() {
            return "Request{}";
        }
    }

    static class Response extends ReplicationResponse {
    }

    class Action extends TransportReplicationAction<Request, Request, Response> {

        Action(Settings settings, String actionName, TransportService transportService,
               ClusterService clusterService,
               ShardStateAction shardStateAction,
               ThreadPool threadPool) {
            super(settings, actionName, transportService, clusterService, mockIndicesService(clusterService), threadPool,
                shardStateAction,
                new ActionFilters(new HashSet<>()), new IndexNameExpressionResolver(Settings.EMPTY),
                Request::new, Request::new, ThreadPool.Names.SAME);
        }

        @Override
        protected Response newResponseInstance() {
            return new Response();
        }

        @Override
        protected PrimaryResult shardOperationOnPrimary(Request shardRequest, IndexShard primary) throws Exception {
            boolean executedBefore = shardRequest.processedOnPrimary.getAndSet(true);
            assert executedBefore == false : "request has already been executed on the primary";
            return new PrimaryResult(shardRequest, new Response());
        }

        @Override
        protected ReplicaResult shardOperationOnReplica(Request request, IndexShard replica) {
            request.processedOnReplicas.incrementAndGet();
            return new ReplicaResult();
        }

        @Override
        protected boolean resolveIndex() {
            return false;
        }
    }

    final IndicesService mockIndicesService(ClusterService clusterService) {
        final IndicesService indicesService = mock(IndicesService.class);
        when(indicesService.indexServiceSafe(any(Index.class))).then(invocation -> {
            Index index = (Index)invocation.getArguments()[0];
            final ClusterState state = clusterService.state();
            final IndexMetaData indexSafe = state.metaData().getIndexSafe(index);
            return mockIndexService(indexSafe, clusterService);
        });
        when(indicesService.indexService(any(Index.class))).then(invocation -> {
            Index index = (Index) invocation.getArguments()[0];
            final ClusterState state = clusterService.state();
            if (state.metaData().hasIndex(index.getName())) {
                final IndexMetaData indexSafe = state.metaData().getIndexSafe(index);
                return mockIndexService(clusterService.state().metaData().getIndexSafe(index), clusterService);
            } else {
                return null;
            }
        });
        return indicesService;
    }

    final IndexService mockIndexService(final IndexMetaData indexMetaData, ClusterService clusterService) {
        final IndexService indexService = mock(IndexService.class);
        when(indexService.getShard(anyInt())).then(invocation -> {
            int shard = (Integer) invocation.getArguments()[0];
            final ShardId shardId = new ShardId(indexMetaData.getIndex(), shard);
            if (shard > indexMetaData.getNumberOfShards()) {
                throw new ShardNotFoundException(shardId);
            }
            return mockIndexShard(shardId, clusterService);
        });
        return indexService;
    }

    private IndexShard mockIndexShard(ShardId shardId, ClusterService clusterService) {
        final IndexShard indexShard = mock(IndexShard.class);
        doAnswer(invocation -> {
            ActionListener<Releasable> callback = (ActionListener<Releasable>) invocation.getArguments()[0];
            count.incrementAndGet();
            callback.onResponse(count::decrementAndGet);
            return null;
        }).when(indexShard).acquirePrimaryOperationLock(any(ActionListener.class), anyString());
        doAnswer(invocation -> {
            long term = (Long)invocation.getArguments()[0];
            ActionListener<Releasable> callback = (ActionListener<Releasable>) invocation.getArguments()[1];
            final long primaryTerm = indexShard.getPrimaryTerm();
            if (term < primaryTerm) {
                throw new IllegalArgumentException(String.format(Locale.ROOT, "%s operation term [%d] is too old (current [%d])",
                    shardId, term, primaryTerm));
            }
            count.incrementAndGet();
            callback.onResponse(count::decrementAndGet);
            return null;
        }).when(indexShard).acquireReplicaOperationLock(anyLong(), any(ActionListener.class), anyString());
        when(indexShard.routingEntry()).thenAnswer(invocationOnMock -> {
            final ClusterState state = clusterService.state();
            final RoutingNode node = state.getRoutingNodes().node(state.nodes().getLocalNodeId());
            final ShardRouting routing = node.getByShardId(shardId);
            if (routing == null) {
                throw new ShardNotFoundException(shardId, "shard is no longer assigned to current node");
            }
            return routing;
        });
        when(indexShard.state()).thenAnswer(invocationOnMock -> isRelocated.get() ? IndexShardState.RELOCATED : IndexShardState.STARTED);
        doThrow(new AssertionError("failed shard is not supported")).when(indexShard).failShard(anyString(), any(Exception.class));
        when(indexShard.getPrimaryTerm()).thenAnswer(i ->
            clusterService.state().metaData().getIndexSafe(shardId.getIndex()).primaryTerm(shardId.id()));
        return indexShard;
    }

    class NoopReplicationOperation extends ReplicationOperation<Request, Request, Action.PrimaryResult> {
        public NoopReplicationOperation(Request request, ActionListener<Action.PrimaryResult> listener) {
            super(request, null, listener, true, null, null, TransportReplicationActionTests.this.logger, "noop");
        }

        @Override
        public void execute() throws Exception {
            this.resultListener.onResponse(action.new PrimaryResult(null, new Response()));
        }
    }

    /**
     * Transport channel that is needed for replica operation testing.
     */
    public TransportChannel createTransportChannel(final PlainActionFuture<Response> listener) {
        return new TransportChannel() {

            @Override
            public String action() {
                return null;
            }

            @Override
            public String getProfileName() {
                return "";
            }

            @Override
            public void sendResponse(TransportResponse response) throws IOException {
                listener.onResponse(((Response) response));
            }

            @Override
            public void sendResponse(TransportResponse response, TransportResponseOptions options) throws IOException {
                listener.onResponse(((Response) response));
            }

            @Override
            public void sendResponse(Exception exception) throws IOException {
                listener.onFailure(exception);
            }

            @Override
            public long getRequestId() {
                return 0;
            }

            @Override
            public String getChannelType() {
                return "replica_test";
            }
        };
    }

}
