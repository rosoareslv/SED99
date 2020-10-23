/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache license, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the license for the specific language governing permissions and
 * limitations under the license.
 */

package org.elasticsearch.index.seqno;

import org.apache.lucene.util.IOUtils;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.cluster.action.shard.ShardStateAction;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.index.Index;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.shard.IndexShard;
import org.elasticsearch.index.shard.ShardId;
import org.elasticsearch.index.translog.Translog;
import org.elasticsearch.indices.IndicesService;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.transport.CapturingTransport;
import org.elasticsearch.threadpool.TestThreadPool;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.Transport;
import org.elasticsearch.transport.TransportService;

import java.util.Collections;

import static org.elasticsearch.mock.orig.Mockito.when;
import static org.elasticsearch.test.ClusterServiceUtils.createClusterService;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

public class GlobalCheckpointSyncActionTests extends ESTestCase {

    private ThreadPool threadPool;
    private Transport transport;
    private ClusterService clusterService;
    private TransportService transportService;
    private ShardStateAction shardStateAction;

    public void setUp() throws Exception {
        super.setUp();
        threadPool = new TestThreadPool(getClass().getName());
        transport = new CapturingTransport();
        clusterService = createClusterService(threadPool);
        transportService = new TransportService(clusterService.getSettings(), transport, threadPool,
            TransportService.NOOP_TRANSPORT_INTERCEPTOR, null);
        transportService.start();
        transportService.acceptIncomingRequests();
        shardStateAction = new ShardStateAction(Settings.EMPTY, clusterService, transportService, null, null, threadPool);
    }

    public void tearDown() throws Exception {
        try {
            IOUtils.close(transportService, clusterService, transport);
        } finally {
            terminate(threadPool);
        }
        super.tearDown();
    }

    public void testTranslogSyncAfterGlobalCheckpointSync() throws Exception {
        final IndicesService indicesService = mock(IndicesService.class);

        final Index index = new Index("index", "uuid");
        final IndexService indexService = mock(IndexService.class);
        when(indicesService.indexServiceSafe(index)).thenReturn(indexService);

        final int id = randomIntBetween(0, 4);
        final IndexShard indexShard = mock(IndexShard.class);
        when(indexService.getShard(id)).thenReturn(indexShard);

        final Translog translog = mock(Translog.class);
        when(indexShard.getTranslog()).thenReturn(translog);

        final GlobalCheckpointSyncAction action = new GlobalCheckpointSyncAction(
            Settings.EMPTY,
            transportService,
            clusterService,
            indicesService,
            threadPool,
            shardStateAction,
            new ActionFilters(Collections.emptySet()),
            new IndexNameExpressionResolver(Settings.EMPTY));
        final ShardId shardId = new ShardId(index, id);
        final GlobalCheckpointSyncAction.PrimaryRequest primaryRequest = new GlobalCheckpointSyncAction.PrimaryRequest(shardId);
        if (randomBoolean()) {
            action.shardOperationOnPrimary(primaryRequest, indexShard);
        } else {
            action.shardOperationOnReplica(
                new GlobalCheckpointSyncAction.ReplicaRequest(primaryRequest, randomNonNegativeLong()), indexShard);
        }

        verify(translog).sync();
    }

}
