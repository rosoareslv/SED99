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

package org.elasticsearch.index.translog;

import com.carrotsearch.randomizedtesting.generators.RandomPicks;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import org.apache.lucene.index.IndexWriter;
import org.apache.lucene.store.Directory;
import org.apache.lucene.store.FSDirectory;
import org.apache.lucene.store.Lock;
import org.apache.lucene.store.LockObtainFailedException;
import org.apache.lucene.store.NativeFSLockFactory;
import org.elasticsearch.action.admin.cluster.node.stats.NodesStatsResponse;
import org.elasticsearch.action.index.IndexRequestBuilder;
import org.elasticsearch.action.search.SearchPhaseExecutionException;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.cli.MockTerminal;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.routing.GroupShardsIterator;
import org.elasticsearch.cluster.routing.ShardIterator;
import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.common.Priority;
import org.elasticsearch.common.io.PathUtils;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.unit.ByteSizeUnit;
import org.elasticsearch.common.unit.ByteSizeValue;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.env.Environment;
import org.elasticsearch.index.Index;
import org.elasticsearch.index.IndexSettings;
import org.elasticsearch.index.MockEngineFactoryPlugin;
import org.elasticsearch.monitor.fs.FsInfo;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.test.ESIntegTestCase;
import org.elasticsearch.test.engine.MockEngineSupport;
import org.elasticsearch.test.hamcrest.ElasticsearchAssertions;
import org.elasticsearch.test.transport.MockTransportService;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.TimeUnit;

import static org.elasticsearch.common.util.CollectionUtils.iterableAsArrayList;
import static org.elasticsearch.index.query.QueryBuilders.matchAllQuery;
import static org.elasticsearch.test.hamcrest.ElasticsearchAssertions.assertAcked;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.notNullValue;

@ESIntegTestCase.ClusterScope(scope = ESIntegTestCase.Scope.SUITE, numDataNodes = 0)
public class TruncateTranslogIT extends ESIntegTestCase {

    @Override
    protected Collection<Class<? extends Plugin>> nodePlugins() {
        return Arrays.asList(MockTransportService.TestPlugin.class, MockEngineFactoryPlugin.class);
    }

    public void testCorruptTranslogTruncation() throws Exception {
        internalCluster().startNodes(1, Settings.EMPTY);

        assertAcked(prepareCreate("test").setSettings(Settings.builder()
                        .put("index.number_of_shards", 1)
                        .put("index.number_of_replicas", 0)
                        .put("index.refresh_interval", "-1")
                        .put(MockEngineSupport.DISABLE_FLUSH_ON_CLOSE.getKey(), true) // never flush - always recover from translog
                ));
        ensureYellow();

        // Index some documents
        int numDocs = scaledRandomIntBetween(100, 1000);
        IndexRequestBuilder[] builders = new IndexRequestBuilder[numDocs];
        for (int i = 0; i < builders.length; i++) {
            builders[i] = client().prepareIndex("test", "type").setSource("foo", "bar");
        }
        disableTranslogFlush("test");
        indexRandom(false, false, false, Arrays.asList(builders));
        Set<Path> translogDirs = getTranslogDirs("test");

        TruncateTranslogCommand ttc = new TruncateTranslogCommand();
        MockTerminal t = new MockTerminal();
        OptionParser parser = ttc.getParser();

        for (Path translogDir : translogDirs) {
            OptionSet options = parser.parse("-d", translogDir.toAbsolutePath().toString(), "-b");
            // Try running it before the shard is closed, it should flip out because it can't acquire the lock
            try {
                logger.info("--> running truncate while index is open on [{}]", translogDir.toAbsolutePath());
                ttc.execute(t, options, null /* TODO: env should be real here, and ttc should actually use it... */);
                fail("expected the truncate command to fail not being able to acquire the lock");
            } catch (Exception e) {
                assertThat(e.getMessage(), containsString("Failed to lock shard's directory"));
            }
        }

        // Corrupt the translog file(s)
        logger.info("--> corrupting translog");
        corruptRandomTranslogFiles("test");

        // Restart the single node
        logger.info("--> restarting node");
        internalCluster().fullRestart();
        client().admin().cluster().prepareHealth().setWaitForYellowStatus()
                .setTimeout(new TimeValue(1000, TimeUnit.MILLISECONDS))
                .setWaitForEvents(Priority.LANGUID)
                .get();

        try {
            client().prepareSearch("test").setQuery(matchAllQuery()).get();
            fail("all shards should be failed due to a corrupted translog");
        } catch (SearchPhaseExecutionException e) {
            // Good, all shards should be failed because there is only a
            // single shard and its translog is corrupt
        }

        // Close the index so we can actually truncate the translog
        logger.info("--> closing 'test' index");
        client().admin().indices().prepareClose("test").get();

        for (Path translogDir : translogDirs) {
            final Path idxLocation = translogDir.getParent().resolve("index");
            assertBusy(() -> {
                logger.info("--> checking that lock has been released for {}", idxLocation);
                try (Directory dir = FSDirectory.open(idxLocation, NativeFSLockFactory.INSTANCE);
                        Lock writeLock = dir.obtainLock(IndexWriter.WRITE_LOCK_NAME)) {
                    // Great, do nothing, we just wanted to obtain the lock
                }  catch (LockObtainFailedException lofe) {
                    logger.info("--> failed acquiring lock for {}", idxLocation);
                    fail("still waiting for lock release at [" + idxLocation + "]");
                } catch (IOException ioe) {
                    fail("Got an IOException: " + ioe);
                }
            });

            OptionSet options = parser.parse("-d", translogDir.toAbsolutePath().toString(), "-b");
            logger.info("--> running truncate translog command for [{}]", translogDir.toAbsolutePath());
            ttc.execute(t, options, null /* TODO: env should be real here, and ttc should actually use it... */);
            logger.info("--> output:\n{}", t.getOutput());
        }

        // Re-open index
        logger.info("--> opening 'test' index");
        client().admin().indices().prepareOpen("test").get();
        ensureYellow("test");

        // Run a search and make sure it succeeds
        SearchResponse resp = client().prepareSearch("test").setQuery(matchAllQuery()).get();
        ElasticsearchAssertions.assertNoFailures(resp);
    }

    private Set<Path> getTranslogDirs(String indexName) throws IOException {
        ClusterState state = client().admin().cluster().prepareState().get().getState();
        GroupShardsIterator shardIterators = state.getRoutingTable().activePrimaryShardsGrouped(new String[]{indexName}, false);
        final Index idx = state.metaData().index(indexName).getIndex();
        List<ShardIterator> iterators = iterableAsArrayList(shardIterators);
        ShardIterator shardIterator = RandomPicks.randomFrom(random(), iterators);
        ShardRouting shardRouting = shardIterator.nextOrNull();
        assertNotNull(shardRouting);
        assertTrue(shardRouting.primary());
        assertTrue(shardRouting.assignedToNode());
        String nodeId = shardRouting.currentNodeId();
        NodesStatsResponse nodeStatses = client().admin().cluster().prepareNodesStats(nodeId).setFs(true).get();
        Set<Path> translogDirs = new TreeSet<>(); // treeset makes sure iteration order is deterministic
        for (FsInfo.Path fsPath : nodeStatses.getNodes().get(0).getFs()) {
            String path = fsPath.getPath();
            final String relativeDataLocationPath =  "indices/"+ idx.getUUID() +"/" + Integer.toString(shardRouting.getId()) + "/translog";
            Path translogPath = PathUtils.get(path).resolve(relativeDataLocationPath);
            if (Files.isDirectory(translogPath)) {
                translogDirs.add(translogPath);
            }
        }
        return translogDirs;
    }

    private void corruptRandomTranslogFiles(String indexName) throws IOException {
        Set<Path> translogDirs = getTranslogDirs(indexName);
        Set<Path> files = new TreeSet<>(); // treeset makes sure iteration order is deterministic
        for (Path translogDir : translogDirs) {
            if (Files.isDirectory(translogDir)) {
                logger.info("--> path: {}", translogDir);
                try (DirectoryStream<Path> stream = Files.newDirectoryStream(translogDir)) {
                    for (Path item : stream) {
                        logger.info("--> File: {}", item);
                        if (Files.isRegularFile(item) && item.getFileName().toString().startsWith("translog-")) {
                            files.add(item);
                        }
                    }
                }
            }
        }
        Path fileToCorrupt = null;
        if (!files.isEmpty()) {
            int corruptions = randomIntBetween(5, 20);
            for (int i = 0; i < corruptions; i++) {
                fileToCorrupt = RandomPicks.randomFrom(random(), files);
                try (FileChannel raf = FileChannel.open(fileToCorrupt, StandardOpenOption.READ, StandardOpenOption.WRITE)) {
                    // read
                    raf.position(randomIntBetween(0, (int) Math.min(Integer.MAX_VALUE, raf.size() - 1)));
                    long filePointer = raf.position();
                    ByteBuffer bb = ByteBuffer.wrap(new byte[1]);
                    raf.read(bb);
                    bb.flip();

                    // corrupt
                    byte oldValue = bb.get(0);
                    byte newValue = (byte) (oldValue + 1);
                    bb.put(0, newValue);

                    // rewrite
                    raf.position(filePointer);
                    raf.write(bb);
                    logger.info("--> corrupting file {} --  flipping at position {} from {} to {} file: {}",
                            fileToCorrupt, filePointer, Integer.toHexString(oldValue),
                            Integer.toHexString(newValue), fileToCorrupt);
                }
            }
        }
        assertThat("no file corrupted", fileToCorrupt, notNullValue());
    }

    /** Disables translog flushing for the specified index */
    private static void disableTranslogFlush(String index) {
        Settings settings = Settings.builder()
                .put(IndexSettings.INDEX_TRANSLOG_FLUSH_THRESHOLD_SIZE_SETTING.getKey(), new ByteSizeValue(1, ByteSizeUnit.PB))
                .build();
        client().admin().indices().prepareUpdateSettings(index).setSettings(settings).get();
    }

}
