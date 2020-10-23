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

package org.elasticsearch.action.bulk;

import org.apache.logging.log4j.message.ParameterizedMessage;
import org.apache.logging.log4j.util.Supplier;
import org.apache.lucene.util.SparseFixedBitSet;
import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.ExceptionsHelper;
import org.elasticsearch.ResourceAlreadyExistsException;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.DocWriteRequest;
import org.elasticsearch.action.RoutingMissingException;
import org.elasticsearch.action.admin.indices.create.CreateIndexRequest;
import org.elasticsearch.action.admin.indices.create.CreateIndexResponse;
import org.elasticsearch.action.admin.indices.create.TransportCreateIndexAction;
import org.elasticsearch.action.delete.DeleteRequest;
import org.elasticsearch.action.delete.TransportDeleteAction;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.action.ingest.IngestActionForwarder;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.AutoCreateIndex;
import org.elasticsearch.action.support.HandledTransportAction;
import org.elasticsearch.action.update.TransportUpdateAction;
import org.elasticsearch.action.update.UpdateRequest;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.block.ClusterBlockLevel;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.metadata.MappingMetaData;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.util.concurrent.AtomicArray;
import org.elasticsearch.index.Index;
import org.elasticsearch.index.IndexNotFoundException;
import org.elasticsearch.index.shard.ShardId;
import org.elasticsearch.indices.IndexClosedException;
import org.elasticsearch.ingest.IngestService;
import org.elasticsearch.tasks.Task;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.LongSupplier;
import java.util.stream.Collectors;

/**
 * Groups bulk request items by shard, optionally creating non-existent indices and
 * delegates to {@link TransportShardBulkAction} for shard-level bulk execution
 */
public class TransportBulkAction extends HandledTransportAction<BulkRequest, BulkResponse> {

    private final AutoCreateIndex autoCreateIndex;
    private final boolean allowIdGeneration;
    private final ClusterService clusterService;
    private final IngestService ingestService;
    private final TransportShardBulkAction shardBulkAction;
    private final TransportCreateIndexAction createIndexAction;
    private final LongSupplier relativeTimeProvider;
    private final IngestActionForwarder ingestForwarder;

    @Inject
    public TransportBulkAction(Settings settings, ThreadPool threadPool, TransportService transportService,
                               ClusterService clusterService, IngestService ingestService,
                               TransportShardBulkAction shardBulkAction, TransportCreateIndexAction createIndexAction,
                               ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                               AutoCreateIndex autoCreateIndex) {
        this(settings, threadPool, transportService, clusterService, ingestService,
                shardBulkAction, createIndexAction,
                actionFilters, indexNameExpressionResolver,
                autoCreateIndex,
                System::nanoTime);
    }

    public TransportBulkAction(Settings settings, ThreadPool threadPool, TransportService transportService,
                               ClusterService clusterService, IngestService ingestService,
                               TransportShardBulkAction shardBulkAction, TransportCreateIndexAction createIndexAction,
                               ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                               AutoCreateIndex autoCreateIndex, LongSupplier relativeTimeProvider) {
        super(settings, BulkAction.NAME, threadPool, transportService, actionFilters, indexNameExpressionResolver, BulkRequest::new);
        Objects.requireNonNull(relativeTimeProvider);
        this.clusterService = clusterService;
        this.ingestService = ingestService;
        this.shardBulkAction = shardBulkAction;
        this.createIndexAction = createIndexAction;
        this.autoCreateIndex = autoCreateIndex;
        this.allowIdGeneration = this.settings.getAsBoolean("action.bulk.action.allow_id_generation", true);
        this.relativeTimeProvider = relativeTimeProvider;
        this.ingestForwarder = new IngestActionForwarder(transportService);
        clusterService.addStateApplier(this.ingestForwarder);
    }

    @Override
    protected final void doExecute(final BulkRequest bulkRequest, final ActionListener<BulkResponse> listener) {
        throw new UnsupportedOperationException("task parameter is required for this operation");
    }

    @Override
    protected void doExecute(Task task, BulkRequest bulkRequest, ActionListener<BulkResponse> listener) {
        if (bulkRequest.hasIndexRequestsWithPipelines()) {
            if (clusterService.localNode().isIngestNode()) {
                processBulkIndexIngestRequest(task, bulkRequest, listener);
            } else {
                ingestForwarder.forwardIngestRequest(BulkAction.INSTANCE, bulkRequest, listener);
            }
            return;
        }

        final long startTime = relativeTime();
        final AtomicArray<BulkItemResponse> responses = new AtomicArray<>(bulkRequest.requests.size());

        if (needToCheck()) {
            // Keep track of all unique indices and all unique types per index for the create index requests:
            final Set<String> autoCreateIndices = bulkRequest.requests.stream()
                .map(DocWriteRequest::index)
                .collect(Collectors.toSet());
            final AtomicInteger counter = new AtomicInteger(autoCreateIndices.size());
            ClusterState state = clusterService.state();
            for (String index : autoCreateIndices) {
                if (shouldAutoCreate(index, state)) {
                    CreateIndexRequest createIndexRequest = new CreateIndexRequest();
                    createIndexRequest.index(index);
                    createIndexRequest.cause("auto(bulk api)");
                    createIndexRequest.masterNodeTimeout(bulkRequest.timeout());
                    createIndexAction.execute(createIndexRequest, new ActionListener<CreateIndexResponse>() {
                        @Override
                        public void onResponse(CreateIndexResponse result) {
                            if (counter.decrementAndGet() == 0) {
                                try {
                                    executeBulk(task, bulkRequest, startTime, listener, responses);
                                } catch (Exception e) {
                                    listener.onFailure(e);
                                }
                            }
                        }

                        @Override
                        public void onFailure(Exception e) {
                            if (!(ExceptionsHelper.unwrapCause(e) instanceof ResourceAlreadyExistsException)) {
                                // fail all requests involving this index, if create didnt work
                                for (int i = 0; i < bulkRequest.requests.size(); i++) {
                                    DocWriteRequest request = bulkRequest.requests.get(i);
                                    if (request != null && setResponseFailureIfIndexMatches(responses, i, request, index, e)) {
                                        bulkRequest.requests.set(i, null);
                                    }
                                }
                            }
                            if (counter.decrementAndGet() == 0) {
                                try {
                                    executeBulk(task, bulkRequest, startTime, listener, responses);
                                } catch (Exception inner) {
                                    inner.addSuppressed(e);
                                    listener.onFailure(inner);
                                }
                            }
                        }
                    });
                } else {
                    if (counter.decrementAndGet() == 0) {
                        executeBulk(task, bulkRequest, startTime, listener, responses);
                    }
                }
            }
        } else {
            executeBulk(task, bulkRequest, startTime, listener, responses);
        }
    }

    boolean needToCheck() {
        return autoCreateIndex.needToCheck();
    }

    boolean shouldAutoCreate(String index, ClusterState state) {
        return autoCreateIndex.shouldAutoCreate(index, state);
    }

    private boolean setResponseFailureIfIndexMatches(AtomicArray<BulkItemResponse> responses, int idx, DocWriteRequest request, String index, Exception e) {
        if (index.equals(request.index())) {
            responses.set(idx, new BulkItemResponse(idx, request.opType(), new BulkItemResponse.Failure(request.index(), request.type(), request.id(), e)));
            return true;
        }
        return false;
    }

    /**
     * This method executes the {@link BulkRequest} and calls the given listener once the request returns.
     * This method will not create any indices even if auto-create indices is enabled.
     *
     * @see #doExecute(BulkRequest, org.elasticsearch.action.ActionListener)
     */
    public void executeBulk(final BulkRequest bulkRequest, final ActionListener<BulkResponse> listener) {
        final long startTimeNanos = relativeTime();
        executeBulk(null, bulkRequest, startTimeNanos, listener, new AtomicArray<>(bulkRequest.requests.size()));
    }

    private long buildTookInMillis(long startTimeNanos) {
        return TimeUnit.NANOSECONDS.toMillis(relativeTime() - startTimeNanos);
    }

    void executeBulk(Task task, final BulkRequest bulkRequest, final long startTimeNanos, final ActionListener<BulkResponse> listener, final AtomicArray<BulkItemResponse> responses ) {
        final ClusterState clusterState = clusterService.state();
        // TODO use timeout to wait here if its blocked...
        clusterState.blocks().globalBlockedRaiseException(ClusterBlockLevel.WRITE);

        final ConcreteIndices concreteIndices = new ConcreteIndices(clusterState, indexNameExpressionResolver);
        MetaData metaData = clusterState.metaData();
        for (int i = 0; i < bulkRequest.requests.size(); i++) {
            DocWriteRequest docWriteRequest = bulkRequest.requests.get(i);
            //the request can only be null because we set it to null in the previous step, so it gets ignored
            if (docWriteRequest == null) {
                continue;
            }
            if (addFailureIfIndexIsUnavailable(docWriteRequest, bulkRequest, responses, i, concreteIndices, metaData)) {
                continue;
            }
            Index concreteIndex = concreteIndices.resolveIfAbsent(docWriteRequest);
            try {
                switch (docWriteRequest.opType()) {
                    case CREATE:
                    case INDEX:
                        IndexRequest indexRequest = (IndexRequest) docWriteRequest;
                        MappingMetaData mappingMd = null;
                        final IndexMetaData indexMetaData = metaData.index(concreteIndex);
                        if (indexMetaData != null) {
                            mappingMd = indexMetaData.mappingOrDefault(indexRequest.type());
                        }
                        indexRequest.resolveRouting(metaData);
                        indexRequest.process(mappingMd, allowIdGeneration, concreteIndex.getName());
                        break;
                    case UPDATE:
                        TransportUpdateAction.resolveAndValidateRouting(metaData, concreteIndex.getName(), (UpdateRequest) docWriteRequest);
                        break;
                    case DELETE:
                        TransportDeleteAction.resolveAndValidateRouting(metaData, concreteIndex.getName(), (DeleteRequest) docWriteRequest);
                        break;
                    default: throw new AssertionError("request type not supported: [" + docWriteRequest.opType() + "]");
                }
            } catch (ElasticsearchParseException | RoutingMissingException e) {
                BulkItemResponse.Failure failure = new BulkItemResponse.Failure(concreteIndex.getName(), docWriteRequest.type(), docWriteRequest.id(), e);
                BulkItemResponse bulkItemResponse = new BulkItemResponse(i, docWriteRequest.opType(), failure);
                responses.set(i, bulkItemResponse);
                // make sure the request gets never processed again
                bulkRequest.requests.set(i, null);
            }
        }

        // first, go over all the requests and create a ShardId -> Operations mapping
        Map<ShardId, List<BulkItemRequest>> requestsByShard = new HashMap<>();
        for (int i = 0; i < bulkRequest.requests.size(); i++) {
            DocWriteRequest request = bulkRequest.requests.get(i);
            if (request == null) {
                continue;
            }
            String concreteIndex = concreteIndices.getConcreteIndex(request.index()).getName();
            ShardId shardId = clusterService.operationRouting().indexShards(clusterState, concreteIndex, request.id(), request.routing()).shardId();
            List<BulkItemRequest> shardRequests = requestsByShard.computeIfAbsent(shardId, shard -> new ArrayList<>());
            shardRequests.add(new BulkItemRequest(i, request));
        }

        if (requestsByShard.isEmpty()) {
            listener.onResponse(new BulkResponse(responses.toArray(new BulkItemResponse[responses.length()]), buildTookInMillis(startTimeNanos)));
            return;
        }

        final AtomicInteger counter = new AtomicInteger(requestsByShard.size());
        String nodeId = clusterService.localNode().getId();
        for (Map.Entry<ShardId, List<BulkItemRequest>> entry : requestsByShard.entrySet()) {
            final ShardId shardId = entry.getKey();
            final List<BulkItemRequest> requests = entry.getValue();
            BulkShardRequest bulkShardRequest = new BulkShardRequest(shardId, bulkRequest.getRefreshPolicy(),
                    requests.toArray(new BulkItemRequest[requests.size()]));
            bulkShardRequest.waitForActiveShards(bulkRequest.waitForActiveShards());
            bulkShardRequest.timeout(bulkRequest.timeout());
            if (task != null) {
                bulkShardRequest.setParentTask(nodeId, task.getId());
            }
            shardBulkAction.execute(bulkShardRequest, new ActionListener<BulkShardResponse>() {
                @Override
                public void onResponse(BulkShardResponse bulkShardResponse) {
                    for (BulkItemResponse bulkItemResponse : bulkShardResponse.getResponses()) {
                        // we may have no response if item failed
                        if (bulkItemResponse.getResponse() != null) {
                            bulkItemResponse.getResponse().setShardInfo(bulkShardResponse.getShardInfo());
                        }
                        responses.set(bulkItemResponse.getItemId(), bulkItemResponse);
                    }
                    if (counter.decrementAndGet() == 0) {
                        finishHim();
                    }
                }

                @Override
                public void onFailure(Exception e) {
                    // create failures for all relevant requests
                    for (BulkItemRequest request : requests) {
                        final String indexName = concreteIndices.getConcreteIndex(request.index()).getName();
                        DocWriteRequest docWriteRequest = request.request();
                        responses.set(request.id(), new BulkItemResponse(request.id(), docWriteRequest.opType(),
                            new BulkItemResponse.Failure(indexName, docWriteRequest.type(), docWriteRequest.id(), e)));
                    }
                    if (counter.decrementAndGet() == 0) {
                        finishHim();
                    }
                }

                private void finishHim() {
                    listener.onResponse(new BulkResponse(responses.toArray(new BulkItemResponse[responses.length()]), buildTookInMillis(startTimeNanos)));
                }
            });
        }
    }

    private boolean addFailureIfIndexIsUnavailable(DocWriteRequest request, BulkRequest bulkRequest, AtomicArray<BulkItemResponse> responses, int idx,
                                                   final ConcreteIndices concreteIndices,
                                                   final MetaData metaData) {
        Index concreteIndex = concreteIndices.getConcreteIndex(request.index());
        Exception unavailableException = null;
        if (concreteIndex == null) {
            try {
                concreteIndex = concreteIndices.resolveIfAbsent(request);
            } catch (IndexClosedException | IndexNotFoundException ex) {
                // Fix for issue where bulk request references an index that
                // cannot be auto-created see issue #8125
                unavailableException = ex;
            }
        }
        if (unavailableException == null) {
            IndexMetaData indexMetaData = metaData.getIndexSafe(concreteIndex);
            if (indexMetaData.getState() == IndexMetaData.State.CLOSE) {
                unavailableException = new IndexClosedException(concreteIndex);
            }
        }
        if (unavailableException != null) {
            BulkItemResponse.Failure failure = new BulkItemResponse.Failure(request.index(), request.type(), request.id(),
                    unavailableException);
            BulkItemResponse bulkItemResponse = new BulkItemResponse(idx, request.opType(), failure);
            responses.set(idx, bulkItemResponse);
            // make sure the request gets never processed again
            bulkRequest.requests.set(idx, null);
            return true;
        }
        return false;
    }

    private static class ConcreteIndices  {
        private final ClusterState state;
        private final IndexNameExpressionResolver indexNameExpressionResolver;
        private final Map<String, Index> indices = new HashMap<>();

        ConcreteIndices(ClusterState state, IndexNameExpressionResolver indexNameExpressionResolver) {
            this.state = state;
            this.indexNameExpressionResolver = indexNameExpressionResolver;
        }

        Index getConcreteIndex(String indexOrAlias) {
            return indices.get(indexOrAlias);
        }

        Index resolveIfAbsent(DocWriteRequest request) {
            Index concreteIndex = indices.get(request.index());
            if (concreteIndex == null) {
                concreteIndex = indexNameExpressionResolver.concreteSingleIndex(state, request);
                indices.put(request.index(), concreteIndex);
            }
            return concreteIndex;
        }
    }

    private long relativeTime() {
        return relativeTimeProvider.getAsLong();
    }

    void processBulkIndexIngestRequest(Task task, BulkRequest original, ActionListener<BulkResponse> listener) {
        long ingestStartTimeInNanos = System.nanoTime();
        BulkRequestModifier bulkRequestModifier = new BulkRequestModifier(original);
        ingestService.getPipelineExecutionService().executeBulkRequest(() -> bulkRequestModifier, (indexRequest, exception) -> {
            logger.debug((Supplier<?>) () -> new ParameterizedMessage("failed to execute pipeline [{}] for document [{}/{}/{}]",
                indexRequest.getPipeline(), indexRequest.index(), indexRequest.type(), indexRequest.id()), exception);
            bulkRequestModifier.markCurrentItemAsFailed(exception);
        }, (exception) -> {
            if (exception != null) {
                logger.error("failed to execute pipeline for a bulk request", exception);
                listener.onFailure(exception);
            } else {
                long ingestTookInMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - ingestStartTimeInNanos);
                BulkRequest bulkRequest = bulkRequestModifier.getBulkRequest();
                ActionListener<BulkResponse> actionListener = bulkRequestModifier.wrapActionListenerIfNeeded(ingestTookInMillis, listener);
                if (bulkRequest.requests().isEmpty()) {
                    // at this stage, the transport bulk action can't deal with a bulk request with no requests,
                    // so we stop and send an empty response back to the client.
                    // (this will happen if pre-processing all items in the bulk failed)
                    actionListener.onResponse(new BulkResponse(new BulkItemResponse[0], 0));
                } else {
                    doExecute(task, bulkRequest, actionListener);
                }
            }
        });
    }

    static final class BulkRequestModifier implements Iterator<DocWriteRequest> {

        final BulkRequest bulkRequest;
        final SparseFixedBitSet failedSlots;
        final List<BulkItemResponse> itemResponses;

        int currentSlot = -1;
        int[] originalSlots;

        BulkRequestModifier(BulkRequest bulkRequest) {
            this.bulkRequest = bulkRequest;
            this.failedSlots = new SparseFixedBitSet(bulkRequest.requests().size());
            this.itemResponses = new ArrayList<>(bulkRequest.requests().size());
        }

        @Override
        public DocWriteRequest next() {
            return bulkRequest.requests().get(++currentSlot);
        }

        @Override
        public boolean hasNext() {
            return (currentSlot + 1) < bulkRequest.requests().size();
        }

        BulkRequest getBulkRequest() {
            if (itemResponses.isEmpty()) {
                return bulkRequest;
            } else {
                BulkRequest modifiedBulkRequest = new BulkRequest();
                modifiedBulkRequest.setRefreshPolicy(bulkRequest.getRefreshPolicy());
                modifiedBulkRequest.waitForActiveShards(bulkRequest.waitForActiveShards());
                modifiedBulkRequest.timeout(bulkRequest.timeout());

                int slot = 0;
                List<DocWriteRequest> requests = bulkRequest.requests();
                originalSlots = new int[requests.size()]; // oversize, but that's ok
                for (int i = 0; i < requests.size(); i++) {
                    DocWriteRequest request = requests.get(i);
                    if (failedSlots.get(i) == false) {
                        modifiedBulkRequest.add(request);
                        originalSlots[slot++] = i;
                    }
                }
                return modifiedBulkRequest;
            }
        }

        ActionListener<BulkResponse> wrapActionListenerIfNeeded(long ingestTookInMillis, ActionListener<BulkResponse> actionListener) {
            if (itemResponses.isEmpty()) {
                return ActionListener.wrap(
                    response -> actionListener.onResponse(
                        new BulkResponse(response.getItems(), response.getTookInMillis(), ingestTookInMillis)),
                    actionListener::onFailure);
            } else {
                return new IngestBulkResponseListener(ingestTookInMillis, originalSlots, itemResponses, actionListener);
            }
        }

        void markCurrentItemAsFailed(Exception e) {
            IndexRequest indexRequest = (IndexRequest) bulkRequest.requests().get(currentSlot);
            // We hit a error during preprocessing a request, so we:
            // 1) Remember the request item slot from the bulk, so that we're done processing all requests we know what failed
            // 2) Add a bulk item failure for this request
            // 3) Continue with the next request in the bulk.
            failedSlots.set(currentSlot);
            BulkItemResponse.Failure failure = new BulkItemResponse.Failure(indexRequest.index(), indexRequest.type(), indexRequest.id(), e);
            itemResponses.add(new BulkItemResponse(currentSlot, indexRequest.opType(), failure));
        }

    }

    static final class IngestBulkResponseListener implements ActionListener<BulkResponse> {

        private final long ingestTookInMillis;
        private final int[] originalSlots;
        private final List<BulkItemResponse> itemResponses;
        private final ActionListener<BulkResponse> actionListener;

        IngestBulkResponseListener(long ingestTookInMillis, int[] originalSlots, List<BulkItemResponse> itemResponses, ActionListener<BulkResponse> actionListener) {
            this.ingestTookInMillis = ingestTookInMillis;
            this.itemResponses = itemResponses;
            this.actionListener = actionListener;
            this.originalSlots = originalSlots;
        }

        @Override
        public void onResponse(BulkResponse response) {
            BulkItemResponse[] items = response.getItems();
            for (int i = 0; i < items.length; i++) {
                itemResponses.add(originalSlots[i], response.getItems()[i]);
            }
            actionListener.onResponse(new BulkResponse(itemResponses.toArray(new BulkItemResponse[itemResponses.size()]), response.getTookInMillis(), ingestTookInMillis));
        }

        @Override
        public void onFailure(Exception e) {
            actionListener.onFailure(e);
        }
    }
}
