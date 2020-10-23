/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.monitoring.action;

import org.elasticsearch.Version;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.PlainActionFuture;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.block.ClusterBlocks;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.settings.ClusterSettings;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.util.concurrent.ConcurrentCollections;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.discovery.DiscoverySettings;
import org.elasticsearch.license.XPackLicenseState;
import org.elasticsearch.test.ClusterServiceUtils;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.transport.CapturingTransport;
import org.elasticsearch.threadpool.TestThreadPool;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.monitoring.MonitoredSystem;
import org.elasticsearch.xpack.monitoring.MonitoringSettings;
import org.elasticsearch.xpack.monitoring.exporter.ExportException;
import org.elasticsearch.xpack.monitoring.exporter.Exporters;
import org.elasticsearch.xpack.monitoring.exporter.MonitoringDoc;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.rules.ExpectedException;

import java.io.IOException;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

import static java.util.Collections.emptyMap;
import static java.util.Collections.emptySet;
import static org.elasticsearch.test.VersionUtils.randomVersion;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.hasToString;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.hamcrest.core.IsEqual.equalTo;
import static org.mockito.Mockito.mock;

public class TransportMonitoringBulkActionTests extends ESTestCase {

    private static ThreadPool threadPool;

    @Rule
    public ExpectedException expectedException = ExpectedException.none();

    private ClusterService clusterService;
    private final XPackLicenseState licenseState = mock(XPackLicenseState.class);
    private TransportService transportService;
    private CapturingExporters exportService;
    private TransportMonitoringBulkAction action;

    @BeforeClass
    public static void beforeClass() {
        threadPool = new TestThreadPool(TransportMonitoringBulkActionTests.class.getSimpleName());
    }

    @AfterClass
    public static void afterClass() {
        ThreadPool.terminate(threadPool, 30, TimeUnit.SECONDS);
        threadPool = null;
    }

    @Before
    public void setUp() throws Exception {
        super.setUp();
        CapturingTransport transport = new CapturingTransport();
        Set<Setting<?>>  clusterSettings = new HashSet<>();
        clusterSettings.addAll(ClusterSettings.BUILT_IN_CLUSTER_SETTINGS);
        clusterSettings.add(MonitoringSettings.EXPORTERS_SETTINGS);
        final DiscoveryNode node = new DiscoveryNode("node", buildNewFakeTransportAddress(), emptyMap(), emptySet(),
                Version.CURRENT);
        clusterService = ClusterServiceUtils.createClusterService(threadPool, node, new ClusterSettings(Settings.EMPTY, clusterSettings));
        transportService = new TransportService(clusterService.getSettings(), transport, threadPool,
                TransportService.NOOP_TRANSPORT_INTERCEPTOR, x -> node, null);
        transportService.start();
        transportService.acceptIncomingRequests();
        exportService = new CapturingExporters();
        action = new TransportMonitoringBulkAction(
                Settings.EMPTY,
                threadPool,
                clusterService,
                transportService,
                new ActionFilters(Collections.emptySet()),
                new IndexNameExpressionResolver(Settings.EMPTY),
                exportService
        );
    }

    @After
    public void tearDown() throws Exception {
        super.tearDown();
        clusterService.close();
        transportService.close();
    }

    public void testGlobalBlock() throws Exception {
        expectedException.expect(ExecutionException.class);
        expectedException.expect(hasToString(containsString("ClusterBlockException[blocked by: [SERVICE_UNAVAILABLE/2/no master]")));

        final ClusterBlocks.Builder block = ClusterBlocks.builder().addGlobalBlock(DiscoverySettings.NO_MASTER_BLOCK_ALL);
        ClusterState currentState = clusterService.state();
        ClusterServiceUtils.setState(clusterService,
                ClusterState.builder(currentState).blocks(block).version(currentState.version() + 1).build());

        MonitoringBulkRequest request = randomRequest();
        action.execute(request).get();
    }

    public void testEmptyRequest() throws Exception {
        expectedException.expect(ExecutionException.class);
        expectedException.expect(hasToString(containsString("no monitoring documents added")));

        MonitoringBulkRequest request = randomRequest(0);
        action.execute(request).get();

        assertThat(exportService.getExported(), hasSize(0));
    }

    public void testBasicRequest() throws Exception {
        MonitoringBulkRequest request = randomRequest();
        action.execute(request).get();

        assertThat(exportService.getExported(), hasSize(request.getDocs().size()));
    }

    public void testAsyncActionPrepareDocs() throws Exception {
        final PlainActionFuture<MonitoringBulkResponse> listener = new PlainActionFuture<>();
        final MonitoringBulkRequest request = randomRequest();

        Collection<MonitoringDoc> results = action.new AsyncAction(request, listener, exportService, clusterService)
                                                            .prepareForExport(request.getDocs());

        assertThat(results, hasSize(request.getDocs().size()));
        for (MonitoringDoc exported : results) {
            assertThat(exported.getClusterUUID(), equalTo(clusterService.state().metaData().clusterUUID()));
            assertThat(exported.getTimestamp(), greaterThan(0L));
            assertThat(exported.getSourceNode(), notNullValue());
            assertThat(exported.getSourceNode().getUUID(), equalTo(clusterService.localNode().getId()));
            assertThat(exported.getSourceNode().getName(), equalTo(clusterService.localNode().getName()));
        }
    }

    public void testAsyncActionExecuteExport() throws Exception {
        final PlainActionFuture<MonitoringBulkResponse> listener = new PlainActionFuture<>();
        final MonitoringBulkRequest request = randomRequest();
        final Collection<MonitoringDoc> docs = Collections.unmodifiableCollection(request.getDocs());

        action.new AsyncAction(request, listener, exportService, clusterService).executeExport(docs, 0L, listener);
        assertThat(listener.get().getError(), nullValue());

        Collection<MonitoringDoc> exported = exportService.getExported();
        assertThat(exported, hasSize(request.getDocs().size()));
    }

    public void testAsyncActionExportThrowsException() throws Exception {
        final PlainActionFuture<MonitoringBulkResponse> listener = new PlainActionFuture<>();
        final MonitoringBulkRequest request = randomRequest();

        final Exporters exporters = new ConsumingExporters(docs -> {
            throw new IllegalStateException();
        });

        action.new AsyncAction(request, listener, exporters, clusterService).start();
        assertThat(listener.get().getError(), notNullValue());
        assertThat(listener.get().getError().getCause(), instanceOf(IllegalStateException.class));
    }

    /**
     * @return a new MonitoringBulkRequest instance with random number of documents
     */
    private static MonitoringBulkRequest randomRequest() throws IOException {
        return randomRequest(scaledRandomIntBetween(1, 100));
    }

    /**
     * @return a new MonitoringBulkRequest instance with given number of documents
     */
    private static MonitoringBulkRequest randomRequest(final int numDocs) throws IOException {
        MonitoringBulkRequest request = new MonitoringBulkRequest();
        for (int i = 0; i < numDocs; i++) {
            String monitoringId = randomFrom(MonitoredSystem.values()).getSystem();
            String monitoringVersion = randomVersion(random()).toString();
            MonitoringIndex index = randomBoolean() ? randomFrom(MonitoringIndex.values()) : null;
            String type = randomFrom("type1", "type2", "type3");
            String id = randomAlphaOfLength(10);

            BytesReference source;
            XContentType xContentType = randomFrom(XContentType.values());
            try (XContentBuilder builder = XContentBuilder.builder(xContentType.xContent())) {
                source = builder.startObject().field("key", i).endObject().bytes();
            }
            request.add(new MonitoringBulkDoc(monitoringId, monitoringVersion, index, type, id,
                    null, 0L, null, source, xContentType));
        }
        return request;
    }

    /**
     * A Exporters implementation that captures the documents to export
     */
    class CapturingExporters extends Exporters {

        private final Collection<MonitoringDoc> exported = ConcurrentCollections.newConcurrentSet();

        CapturingExporters() {
            super(Settings.EMPTY, Collections.emptyMap(), clusterService, licenseState, threadPool.getThreadContext());
        }

        @Override
        public synchronized void export(Collection<MonitoringDoc> docs, ActionListener<Void> listener) throws ExportException {
            exported.addAll(docs);
            listener.onResponse(null);
        }

        public Collection<MonitoringDoc> getExported() {
            return exported;
        }
    }

    /**
     * A Exporters implementation that applies a Consumer when exporting documents
     */
    class ConsumingExporters extends Exporters {

        private final Consumer<Collection<? extends MonitoringDoc>> consumer;

        ConsumingExporters(Consumer<Collection<? extends MonitoringDoc>> consumer) {
            super(Settings.EMPTY, Collections.emptyMap(), clusterService, licenseState, threadPool.getThreadContext());
            this.consumer = consumer;
        }

        @Override
        public synchronized void export(Collection<MonitoringDoc> docs, ActionListener<Void> listener) throws ExportException {
            consumer.accept(docs);
            listener.onResponse(null);
        }
    }
}
