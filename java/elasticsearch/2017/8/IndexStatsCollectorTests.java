/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.monitoring.collector.indices;

import org.elasticsearch.Version;
import org.elasticsearch.action.admin.indices.stats.IndexStats;
import org.elasticsearch.action.admin.indices.stats.IndicesStatsResponse;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.license.XPackLicenseState;
import org.elasticsearch.test.ESIntegTestCase.ClusterScope;
import org.elasticsearch.xpack.monitoring.MonitoredSystem;
import org.elasticsearch.xpack.monitoring.MonitoringSettings;
import org.elasticsearch.xpack.monitoring.collector.AbstractCollectorTestCase;
import org.elasticsearch.xpack.monitoring.exporter.MonitoringDoc;

import java.util.Collection;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

import static org.elasticsearch.test.hamcrest.ElasticsearchAssertions.assertHitCount;
import static org.hamcrest.Matchers.arrayWithSize;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;

@ClusterScope(numDataNodes = 0, numClientNodes = 0, transportClientRatio = 0.0)
public class IndexStatsCollectorTests extends AbstractCollectorTestCase {

    @Override
    protected int numberOfReplicas() {
        return 0;
    }

    public void testEmptyCluster() throws Exception {
        final String node = internalCluster().startNode();
        waitForNoBlocksOnNode(node);
        assertThat(newIndexStatsCollector(node).doCollect(), hasSize(1));
    }

    public void testEmptyClusterAllIndices() throws Exception {
        final String node = internalCluster().startNode(Settings.builder().put(MonitoringSettings.INDICES.getKey(), MetaData.ALL));
        waitForNoBlocksOnNode(node);
        assertThat(newIndexStatsCollector(node).doCollect(), hasSize(1));
    }

    public void testEmptyClusterMissingIndex() throws Exception {
        final String node = internalCluster().startNode(Settings.builder().put(MonitoringSettings.INDICES.getKey(), "unknown"));
        waitForNoBlocksOnNode(node);
        assertThat(newIndexStatsCollector(node).doCollect(), hasSize(1));
    }

    public void testIndexStatsCollectorOneIndex() throws Exception {
        final String node = internalCluster().startNode();
        waitForNoBlocksOnNode(node);

        final String indexName = "one-index";
        createIndex(indexName);
        ensureGreen(indexName);

        final int nbDocs = randomIntBetween(1, 20);
        for (int i = 0; i < nbDocs; i++) {
            client().prepareIndex(indexName, "test").setSource("num", i).get();
        }

        refresh();

        assertHitCount(client().prepareSearch().setSize(0).get(), nbDocs);

        Collection<MonitoringDoc> results = newIndexStatsCollector().doCollect();
        assertThat(results, hasSize(2));

        // indices stats
        final Optional<IndicesStatsMonitoringDoc> indicesStatsDoc =
            results.stream().filter(doc -> doc instanceof IndicesStatsMonitoringDoc).map(doc -> (IndicesStatsMonitoringDoc)doc).findFirst();

        assertThat(indicesStatsDoc.isPresent(), is(true));

        IndicesStatsMonitoringDoc indicesStatsMonitoringDoc = indicesStatsDoc.get();
        assertThat(indicesStatsMonitoringDoc.getClusterUUID(), equalTo(client().admin().cluster().
                prepareState().setMetaData(true).get().getState().metaData().clusterUUID()));
        assertThat(indicesStatsMonitoringDoc.getTimestamp(), greaterThan(0L));
        assertThat(indicesStatsMonitoringDoc.getSourceNode(), notNullValue());

        IndicesStatsResponse indicesStats = indicesStatsMonitoringDoc.getIndicesStats();
        assertNotNull(indicesStats);
        assertThat(indicesStats.getIndices().keySet(), hasSize(1));
        assertThat(indicesStats.getIndex(indexName).getShards(), arrayWithSize(getNumShards(indexName).totalNumShards));

        // index stats
        final Optional<IndexStatsMonitoringDoc> indexStatsDoc =
                results.stream()
                       .filter(doc -> doc instanceof IndexStatsMonitoringDoc)
                       .map(doc -> (IndexStatsMonitoringDoc)doc)
                       .findFirst();

        assertThat(indexStatsDoc.isPresent(), is(true));

        IndexStatsMonitoringDoc indexStatsMonitoringDoc = indexStatsDoc.get();
        assertThat(indexStatsMonitoringDoc.getMonitoringId(), equalTo(MonitoredSystem.ES.getSystem()));
        assertThat(indexStatsMonitoringDoc.getMonitoringVersion(), equalTo(Version.CURRENT.toString()));
        assertThat(indexStatsMonitoringDoc.getClusterUUID(),
                equalTo(client().admin().cluster().prepareState().setMetaData(true).get().getState().metaData().clusterUUID()));
        assertThat(indexStatsMonitoringDoc.getTimestamp(), greaterThan(0L));
        assertThat(indexStatsMonitoringDoc.getSourceNode(), notNullValue());

        IndexStats indexStats = indexStatsMonitoringDoc.getIndexStats();
        assertNotNull(indexStats);

        assertThat(indexStats.getIndex(), equalTo(indexName));
        assertThat(indexStats.getPrimaries().getDocs().getCount(), equalTo((long) nbDocs));
        assertNotNull(indexStats.getTotal().getStore());
        assertThat(indexStats.getTotal().getStore().getSizeInBytes(), greaterThan(0L));
        assertNotNull(indexStats.getTotal().getIndexing());
        assertThat(indexStats.getTotal().getIndexing().getTotal().getThrottleTime().millis(), equalTo(0L));
    }

    public void testIndexStatsCollectorMultipleIndices() throws Exception {
        final String node = internalCluster().startNode();
        waitForNoBlocksOnNode(node);

        final String indexPrefix = "multi-indices-";
        final int nbIndices = randomIntBetween(1, 5);
        int[] docsPerIndex = new int[nbIndices];

        for (int i = 0; i < nbIndices; i++) {
            String index = indexPrefix + i;
            createIndex(index);
            ensureGreen(index);

            docsPerIndex[i] = randomIntBetween(1, 20);
            for (int j = 0; j < docsPerIndex[i]; j++) {
                client().prepareIndex(index, "test").setSource("num", i).get();
            }
        }

        refresh();

        for (int i = 0; i < nbIndices; i++) {
            assertHitCount(client().prepareSearch(indexPrefix + i).setSize(0).get(), docsPerIndex[i]);
        }

        String clusterUUID = client().admin().cluster().prepareState().setMetaData(true).get().getState().metaData().clusterUUID();

        Collection<MonitoringDoc> results = newIndexStatsCollector().doCollect();
        // extra document is for the IndicesStatsMonitoringDoc
        assertThat(results, hasSize(nbIndices + 1));

        // indices stats
        final Optional<IndicesStatsMonitoringDoc> indicesStatsDoc =
                results.stream()
                       .filter(doc -> doc instanceof IndicesStatsMonitoringDoc)
                       .map(doc -> (IndicesStatsMonitoringDoc)doc)
                       .findFirst();

        assertThat(indicesStatsDoc.isPresent(), is(true));

        IndicesStatsMonitoringDoc indicesStatsMonitoringDoc = indicesStatsDoc.get();
        assertThat(indicesStatsMonitoringDoc.getMonitoringId(), equalTo(MonitoredSystem.ES.getSystem()));
        assertThat(indicesStatsMonitoringDoc.getMonitoringVersion(), equalTo(Version.CURRENT.toString()));
        assertThat(indicesStatsMonitoringDoc.getClusterUUID(),
                equalTo(client().admin().cluster().prepareState().setMetaData(true).get().getState().metaData().clusterUUID()));
        assertThat(indicesStatsMonitoringDoc.getTimestamp(), greaterThan(0L));

        IndicesStatsResponse indicesStats = indicesStatsMonitoringDoc.getIndicesStats();
        assertNotNull(indicesStats);
        assertThat(indicesStats.getIndices().keySet(), hasSize(nbIndices));

        // index stats
        final List<IndexStatsMonitoringDoc> indexStatsDocs =
                results.stream()
                       .filter(doc -> doc instanceof IndexStatsMonitoringDoc)
                       .map(doc -> (IndexStatsMonitoringDoc)doc)
                       .collect(Collectors.toList());

        assertThat(indexStatsDocs, hasSize(nbIndices));

        for (int i = 0; i < nbIndices; i++) {
            String indexName = indexPrefix + i;
            boolean found = false;

            Iterator<IndexStatsMonitoringDoc> it = indexStatsDocs.iterator();
            while (!found && it.hasNext()) {
                IndexStatsMonitoringDoc indexStatsMonitoringDoc = it.next();
                IndexStats indexStats = indexStatsMonitoringDoc.getIndexStats();
                assertNotNull(indexStats);

                if (indexStats.getIndex().equals(indexPrefix + i)) {
                    assertThat(indexStatsMonitoringDoc.getClusterUUID(), equalTo(clusterUUID));
                    assertThat(indexStatsMonitoringDoc.getTimestamp(), greaterThan(0L));
                    assertThat(indexStatsMonitoringDoc.getSourceNode(), notNullValue());

                    assertThat(indexStats.getIndex(), equalTo(indexName));
                    assertNotNull(indexStats.getTotal().getDocs());
                    assertThat(indexStats.getPrimaries().getDocs().getCount(), equalTo((long) docsPerIndex[i]));
                    assertNotNull(indexStats.getTotal().getStore());
                    assertThat(indexStats.getTotal().getStore().getSizeInBytes(), greaterThanOrEqualTo(0L));
                    assertNotNull(indexStats.getTotal().getIndexing());
                    assertThat(indexStats.getTotal().getIndexing().getTotal().getThrottleTime().millis(), equalTo(0L));
                    found = true;
                }
            }
            assertThat("could not find collected stats for index [" + indexPrefix + i + "]", found, is(true));
        }
    }

    private IndexStatsCollector newIndexStatsCollector() {
        // This collector runs on master node only
        return newIndexStatsCollector(internalCluster().getMasterName());
    }

    private IndexStatsCollector newIndexStatsCollector(String nodeId) {
        assertNotNull(nodeId);
        return new IndexStatsCollector(internalCluster().getInstance(Settings.class, nodeId),
                internalCluster().getInstance(ClusterService.class, nodeId),
                internalCluster().getInstance(MonitoringSettings.class, nodeId),
                internalCluster().getInstance(XPackLicenseState.class, nodeId),
                securedClient(nodeId));
    }
}
