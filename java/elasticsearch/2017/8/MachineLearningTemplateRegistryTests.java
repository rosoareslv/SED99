/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml;

import org.elasticsearch.Version;
import org.elasticsearch.action.admin.indices.template.put.PutIndexTemplateRequest;
import org.elasticsearch.client.Client;
import org.elasticsearch.cluster.ClusterChangedEvent;
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.metadata.IndexTemplateMetaData;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.routing.UnassignedInfo;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.transport.TransportAddress;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.index.mapper.MapperService;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.xpack.ml.job.persistence.AnomalyDetectorsIndex;
import org.elasticsearch.xpack.ml.job.persistence.ElasticsearchMappings;
import org.elasticsearch.xpack.ml.job.persistence.MockClientBuilder;
import org.elasticsearch.xpack.ml.notifications.AuditMessage;
import org.elasticsearch.xpack.ml.notifications.Auditor;
import org.junit.Before;
import org.mockito.ArgumentCaptor;

import java.net.InetAddress;
import java.util.Collections;
import java.util.concurrent.ExecutorService;

import static org.elasticsearch.mock.orig.Mockito.doAnswer;
import static org.elasticsearch.mock.orig.Mockito.times;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class MachineLearningTemplateRegistryTests extends ESTestCase {
    private static final String CLUSTER_NAME = "clusterMcClusterFace";

    private ClusterService clusterService;
    private ExecutorService executorService;
    private Client client;
    private ThreadPool threadPool;

    @Before
    public void setUpMocks() {
        threadPool = mock(ThreadPool.class);
        executorService = mock(ExecutorService.class);
        clusterService = mock(ClusterService.class);
        client = mock(Client.class);

        doAnswer(invocation -> {
            ((Runnable) invocation.getArguments()[0]).run();
            return null;
        }).when(executorService).execute(any(Runnable.class));
        when(threadPool.executor(ThreadPool.Names.GENERIC)).thenReturn(executorService);
    }

    public void testAddsListener() throws Exception {
        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(Settings.EMPTY, clusterService, client, threadPool);

        verify(clusterService, times(1)).addListener(templateRegistry);
    }

    public void testAddTemplatesIfMissing() throws Exception {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(Settings.EMPTY, clusterService, clientBuilder.build(), threadPool);

        ClusterState cs = ClusterState.builder(new ClusterName("_name"))
                .nodes(DiscoveryNodes.builder()
                        .add(new DiscoveryNode("_node_id", new TransportAddress(InetAddress.getLoopbackAddress(), 9200), Version.CURRENT))
                        .localNodeId("_node_id")
                        .masterNodeId("_node_id"))
                .metaData(MetaData.builder())
                .build();
        templateRegistry.clusterChanged(new ClusterChangedEvent("_source", cs, cs));

        verify(threadPool, times(4)).executor(anyString());
        assertFalse(templateRegistry.putMlNotificationsIndexTemplateCheck.get());
        assertFalse(templateRegistry.putMlMetaIndexTemplateCheck.get());
        assertFalse(templateRegistry.putMlNotificationsIndexTemplateCheck.get());
        assertFalse(templateRegistry.putResultsIndexTemplateCheck.get());
    }

    public void testAddTemplatesIfMissing_alreadyInitialized() throws Exception {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(Settings.EMPTY, clusterService, clientBuilder.build(), threadPool);

        ClusterState cs = ClusterState.builder(new ClusterName("_name"))
                .nodes(DiscoveryNodes.builder()
                        .add(new DiscoveryNode("_node_id", new TransportAddress(InetAddress.getLoopbackAddress(), 9200), Version.CURRENT))
                        .localNodeId("_node_id")
                        .masterNodeId("_node_id"))
                .metaData(MetaData.builder()
                        .put(IndexMetaData.builder(Auditor.NOTIFICATIONS_INDEX).settings(Settings.builder()
                                .put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, 1)
                                .put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, 0)
                                .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT)
                        ))
                        .put(IndexMetaData.builder(MlMetaIndex.INDEX_NAME).settings(Settings.builder()
                                .put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, 1)
                                .put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, 0)
                                .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT)
                        ))
                        .put(IndexMetaData.builder(AnomalyDetectorsIndex.jobStateIndexName()).settings(Settings.builder()
                                .put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, 1)
                                .put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, 0)
                                .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT)
                        ))
                        .put(IndexTemplateMetaData.builder(Auditor.NOTIFICATIONS_INDEX).version(Version.CURRENT.id).build())
                        .put(IndexTemplateMetaData.builder(MlMetaIndex.INDEX_NAME).version(Version.CURRENT.id).build())
                        .put(IndexTemplateMetaData.builder(AnomalyDetectorsIndex.jobStateIndexName()).version(Version.CURRENT.id).build())
                        .put(IndexTemplateMetaData.builder(
                                AnomalyDetectorsIndex.jobResultsIndexPrefix()).version(Version.CURRENT.id).build())
                        .putCustom(MlMetadata.TYPE, new MlMetadata.Builder().build()))
                .build();
        templateRegistry.clusterChanged(new ClusterChangedEvent("_source", cs, cs));

        verify(threadPool, times(0)).executor(anyString());
        assertFalse(templateRegistry.putMlNotificationsIndexTemplateCheck.get());
        assertFalse(templateRegistry.putMlMetaIndexTemplateCheck.get());
        assertFalse(templateRegistry.putStateIndexTemplateCheck.get());
        assertFalse(templateRegistry.putResultsIndexTemplateCheck.get());
    }

    public void testMlResultsIndexSettings() {
        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, client, threadPool);
        Settings settings = templateRegistry.mlResultsIndexSettings().build();

        assertEquals(4, settings.size());
        assertThat(settings.get("index.number_of_shards"), is(nullValue()));
        assertEquals("async", settings.get("index.translog.durability"));
        assertEquals("true", settings.get("index.mapper.dynamic"));
        assertEquals("all_field_values", settings.get("index.query.default_field"));
        assertEquals("2s", settings.get("index.unassigned.node_left.delayed_timeout"));
    }

    public void testMlAuditIndexSettings() {
        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, client, threadPool);
        Settings settings = templateRegistry.mlNotificationIndexSettings().build();

        assertEquals(3, settings.size());
        assertEquals("1", settings.get("index.number_of_shards"));
        assertEquals("true", settings.get("index.mapper.dynamic"));
        assertEquals("2s", settings.get("index.unassigned.node_left.delayed_timeout"));
    }

    public void testMlStateIndexSettings() {
        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, client, threadPool);
        Settings settings = templateRegistry.mlStateIndexSettings().build();

        assertEquals(2, settings.size());
        assertEquals("async", settings.get("index.translog.durability"));
        assertEquals("2s", settings.get("index.unassigned.node_left.delayed_timeout"));
    }

    public void testPutNotificationIndexTemplate() {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, clientBuilder.build(), threadPool);

        templateRegistry.putNotificationMessageIndexTemplate((result, error) -> {
            assertTrue(result);
            PutIndexTemplateRequest request = captor.getValue();
            assertNotNull(request);
            assertEquals(templateRegistry.mlNotificationIndexSettings().build(), request.settings());
            assertTrue(request.mappings().containsKey(AuditMessage.TYPE.getPreferredName()));
            assertEquals(1, request.mappings().size());
            assertEquals(Collections.singletonList(Auditor.NOTIFICATIONS_INDEX), request.patterns());
            assertEquals(new Integer(Version.CURRENT.id), request.version());
        });
    }

    public void testPutMetaIndexTemplate() {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, clientBuilder.build(), threadPool);

        templateRegistry.putMetaIndexTemplate((result, error) -> {
            assertTrue(result);
            PutIndexTemplateRequest request = captor.getValue();
            assertNotNull(request);
            assertEquals(templateRegistry.mlNotificationIndexSettings().build(), request.settings());
            assertEquals(1, request.mappings().size());
            assertThat(request.mappings().containsKey("doc"), is(true));
            assertEquals(Collections.singletonList(MlMetaIndex.INDEX_NAME), request.patterns());
            assertEquals(new Integer(Version.CURRENT.id), request.version());
        });
    }

    public void testPutJobStateIndexTemplate() {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, clientBuilder.build(), threadPool);

        templateRegistry.putJobStateIndexTemplate((result, error) -> {
            assertTrue(result);
            PutIndexTemplateRequest request = captor.getValue();
            assertNotNull(request);
            assertEquals(templateRegistry.mlStateIndexSettings().build(), request.settings());
            assertTrue(request.mappings().containsKey(ElasticsearchMappings.DOC_TYPE));
            assertEquals(1, request.mappings().size());
            assertEquals(Collections.singletonList(AnomalyDetectorsIndex.jobStateIndexName()), request.patterns());
            assertEquals(new Integer(Version.CURRENT.id), request.version());
        });
    }

    public void testPutJobResultsIndexTemplate() {
        MockClientBuilder clientBuilder = new MockClientBuilder(CLUSTER_NAME);
        ArgumentCaptor<PutIndexTemplateRequest> captor = ArgumentCaptor.forClass(PutIndexTemplateRequest.class);
        clientBuilder.putTemplate(captor);

        MachineLearningTemplateRegistry templateRegistry =
                new MachineLearningTemplateRegistry(createSettings(), clusterService, clientBuilder.build(), threadPool);

        templateRegistry.putJobResultsIndexTemplate((result, error) -> {
            assertTrue(result);
            PutIndexTemplateRequest request = captor.getValue();
            assertNotNull(request);
            assertEquals(templateRegistry.mlResultsIndexSettings().build(), request.settings());
            assertTrue(request.mappings().containsKey("doc"));
            assertEquals(1, request.mappings().size());
            assertEquals(Collections.singletonList(AnomalyDetectorsIndex.jobResultsIndexPrefix() + "*"), request.patterns());
            assertEquals(new Integer(Version.CURRENT.id), request.version());
        });
    }

    public void testTemplateIsPresentAndUpToDate() {
        // missing template
        MetaData metaData = MetaData.builder().build();
        assertFalse(MachineLearningTemplateRegistry.templateIsPresentAndUpToDate(Auditor.NOTIFICATIONS_INDEX, metaData));

        // old version of template
        IndexTemplateMetaData templateMetaData = IndexTemplateMetaData.builder(Auditor.NOTIFICATIONS_INDEX)
                .version(Version.CURRENT.id - 1).build();
        metaData = MetaData.builder().put(templateMetaData).build();
        assertFalse(MachineLearningTemplateRegistry.templateIsPresentAndUpToDate(Auditor.NOTIFICATIONS_INDEX, metaData));

        // latest template
        templateMetaData = IndexTemplateMetaData.builder(Auditor.NOTIFICATIONS_INDEX)
                .version(Version.CURRENT.id).build();
        metaData = MetaData.builder().put(templateMetaData).build();
        assertTrue(MachineLearningTemplateRegistry.templateIsPresentAndUpToDate(Auditor.NOTIFICATIONS_INDEX, metaData));
    }

    public void testAllTemplatesInstalled() {
        MetaData metaData = MetaData.builder()
                .put(IndexTemplateMetaData.builder(Auditor.NOTIFICATIONS_INDEX).version(Version.CURRENT.id).build())
                .put(IndexTemplateMetaData.builder(MlMetaIndex.INDEX_NAME).version(Version.CURRENT.id).build())
                .put(IndexTemplateMetaData.builder(AnomalyDetectorsIndex.jobStateIndexName()).version(Version.CURRENT.id).build())
                .put(IndexTemplateMetaData.builder(
                        AnomalyDetectorsIndex.jobResultsIndexPrefix()).version(Version.CURRENT.id).build()).build();

        assertTrue(MachineLearningTemplateRegistry.allTemplatesInstalled(metaData));
    }

    public void testAllTemplatesInstalled_OneMissing() {
        MetaData.Builder metaDataBuilder = MetaData.builder();

        String missing = randomFrom(MachineLearningTemplateRegistry.TEMPLATE_NAMES);
        for (String templateName : MachineLearningTemplateRegistry.TEMPLATE_NAMES) {
            if (templateName.equals(missing)) {
                continue;
            }
            metaDataBuilder.put(IndexTemplateMetaData.builder(templateName).version(Version.CURRENT.id).build());
        }
        assertFalse(MachineLearningTemplateRegistry.allTemplatesInstalled(metaDataBuilder.build()));
    }

    private Settings createSettings() {
        return Settings.builder()
                .put(UnassignedInfo.INDEX_DELAYED_NODE_LEFT_TIMEOUT_SETTING.getKey(), TimeValue.timeValueSeconds(2))
                .put(MapperService.INDEX_MAPPING_TOTAL_FIELDS_LIMIT_SETTING.getKey(), 1001L)
                .build();
    }
}
