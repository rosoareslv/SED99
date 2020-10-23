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

package org.elasticsearch.action.search;

import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.message.ParameterizedMessage;
import org.apache.logging.log4j.util.Supplier;
import org.apache.lucene.search.ScoreDoc;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.util.concurrent.AtomicArray;
import org.elasticsearch.search.fetch.QueryFetchSearchResult;
import org.elasticsearch.search.fetch.ScrollQueryFetchSearchResult;
import org.elasticsearch.search.internal.InternalScrollSearchRequest;
import org.elasticsearch.search.internal.InternalSearchResponse;
import org.elasticsearch.search.query.ScrollQuerySearchResult;

import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import static org.elasticsearch.action.search.TransportSearchHelper.internalScrollSearchRequest;

final class SearchScrollQueryAndFetchAsyncAction extends AbstractAsyncAction {

    private final Logger logger;
    private final SearchPhaseController searchPhaseController;
    private final SearchTransportService searchTransportService;
    private final SearchScrollRequest request;
    private final SearchTask task;
    private final ActionListener<SearchResponse> listener;
    private final ParsedScrollId scrollId;
    private final DiscoveryNodes nodes;
    private volatile AtomicArray<ShardSearchFailure> shardFailures;
    private final AtomicArray<QueryFetchSearchResult> queryFetchResults;
    private final AtomicInteger successfulOps;
    private final AtomicInteger counter;

    SearchScrollQueryAndFetchAsyncAction(Logger logger, ClusterService clusterService, SearchTransportService searchTransportService,
                                         SearchPhaseController searchPhaseController, SearchScrollRequest request, SearchTask task,
                                         ParsedScrollId scrollId, ActionListener<SearchResponse> listener) {
        this.logger = logger;
        this.searchPhaseController = searchPhaseController;
        this.searchTransportService = searchTransportService;
        this.request = request;
        this.task = task;
        this.listener = listener;
        this.scrollId = scrollId;
        this.nodes = clusterService.state().nodes();
        this.successfulOps = new AtomicInteger(scrollId.getContext().length);
        this.counter = new AtomicInteger(scrollId.getContext().length);

        this.queryFetchResults = new AtomicArray<>(scrollId.getContext().length);
    }

    private ShardSearchFailure[] buildShardFailures() {
        if (shardFailures == null) {
            return ShardSearchFailure.EMPTY_ARRAY;
        }
        List<ShardSearchFailure> failures = shardFailures.asList();
        return failures.toArray(new ShardSearchFailure[failures.size()]);
    }

    // we do our best to return the shard failures, but its ok if its not fully concurrently safe
    // we simply try and return as much as possible
    private void addShardFailure(final int shardIndex, ShardSearchFailure failure) {
        if (shardFailures == null) {
            shardFailures = new AtomicArray<>(scrollId.getContext().length);
        }
        shardFailures.set(shardIndex, failure);
    }

    public void start() {
        if (scrollId.getContext().length == 0) {
            listener.onFailure(new SearchPhaseExecutionException("query", "no nodes to search on", ShardSearchFailure.EMPTY_ARRAY));
            return;
        }

        ScrollIdForNode[] context = scrollId.getContext();
        for (int i = 0; i < context.length; i++) {
            ScrollIdForNode target = context[i];
            DiscoveryNode node = nodes.get(target.getNode());
            if (node != null) {
                executePhase(i, node, target.getScrollId());
            } else {
                if (logger.isDebugEnabled()) {
                    logger.debug("Node [{}] not available for scroll request [{}]", target.getNode(), scrollId.getSource());
                }
                successfulOps.decrementAndGet();
                if (counter.decrementAndGet() == 0) {
                    finishHim();
                }
            }
        }

        for (ScrollIdForNode target : scrollId.getContext()) {
            DiscoveryNode node = nodes.get(target.getNode());
            if (node == null) {
                if (logger.isDebugEnabled()) {
                    logger.debug("Node [{}] not available for scroll request [{}]", target.getNode(), scrollId.getSource());
                }
                successfulOps.decrementAndGet();
                if (counter.decrementAndGet() == 0) {
                    finishHim();
                }
            }
        }
    }

    void executePhase(final int shardIndex, DiscoveryNode node, final long searchId) {
        InternalScrollSearchRequest internalRequest = internalScrollSearchRequest(searchId, request);
        searchTransportService.sendExecuteScrollFetch(node, internalRequest, task,
            new SearchActionListener<ScrollQueryFetchSearchResult>(null, shardIndex) {
            @Override
            protected void setSearchShardTarget(ScrollQueryFetchSearchResult response) {
                // don't do this - it's part of the response...
                assert response.getSearchShardTarget() != null : "search shard target must not be null";
            }
            @Override
            protected void innerOnResponse(ScrollQueryFetchSearchResult response) {
                queryFetchResults.set(response.getShardIndex(), response.result());
                if (counter.decrementAndGet() == 0) {
                    finishHim();
                }
            }
            @Override
            public void onFailure(Exception t) {
                onPhaseFailure(t, searchId, shardIndex);
            }
        });
    }

    private void onPhaseFailure(Exception e, long searchId, int shardIndex) {
        if (logger.isDebugEnabled()) {
            logger.debug((Supplier<?>) () -> new ParameterizedMessage("[{}] Failed to execute query phase", searchId), e);
        }
        addShardFailure(shardIndex, new ShardSearchFailure(e));
        successfulOps.decrementAndGet();
        if (counter.decrementAndGet() == 0) {
            if (successfulOps.get() == 0) {
                listener.onFailure(new SearchPhaseExecutionException("query_fetch", "all shards failed", e, buildShardFailures()));
            } else {
                finishHim();
            }
        }
    }

    private void finishHim() {
        try {
            innerFinishHim();
        } catch (Exception e) {
            listener.onFailure(new ReduceSearchPhaseException("fetch", "", e, buildShardFailures()));
        }
    }

    private void innerFinishHim() throws Exception {
        List<QueryFetchSearchResult> queryFetchSearchResults = queryFetchResults.asList();
        final InternalSearchResponse internalResponse = searchPhaseController.merge(true,
            searchPhaseController.reducedQueryPhase(queryFetchSearchResults, true), queryFetchSearchResults, queryFetchResults::get);
        String scrollId = null;
        if (request.scroll() != null) {
            scrollId = request.scrollId();
        }
        listener.onResponse(new SearchResponse(internalResponse, scrollId, this.scrollId.getContext().length, successfulOps.get(),
            buildTookInMillis(), buildShardFailures()));
    }
}
