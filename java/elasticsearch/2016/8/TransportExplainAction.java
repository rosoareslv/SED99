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

package org.elasticsearch.action.explain;

import org.apache.lucene.index.Term;
import org.apache.lucene.search.Explanation;
import org.elasticsearch.ElasticsearchException;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.RoutingMissingException;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.single.shard.TransportSingleShardAction;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.routing.ShardIterator;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.util.BigArrays;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.engine.Engine;
import org.elasticsearch.index.get.GetResult;
import org.elasticsearch.index.mapper.Uid;
import org.elasticsearch.index.mapper.UidFieldMapper;
import org.elasticsearch.index.shard.IndexShard;
import org.elasticsearch.index.shard.ShardId;
import org.elasticsearch.indices.IndicesService;
import org.elasticsearch.script.ScriptService;
import org.elasticsearch.search.SearchService;
import org.elasticsearch.search.fetch.FetchPhase;
import org.elasticsearch.search.internal.DefaultSearchContext;
import org.elasticsearch.search.internal.SearchContext;
import org.elasticsearch.search.internal.ShardSearchLocalRequest;
import org.elasticsearch.search.rescore.RescoreSearchContext;
import org.elasticsearch.search.rescore.Rescorer;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;

import java.io.IOException;

/**
 * Explain transport action. Computes the explain on the targeted shard.
 */
// TODO: AggregatedDfs. Currently the idf can be different then when executing a normal search with explain.
public class TransportExplainAction extends TransportSingleShardAction<ExplainRequest, ExplainResponse> {

    private final IndicesService indicesService;

    private final ScriptService scriptService;


    private final BigArrays bigArrays;

    private final FetchPhase fetchPhase;

    @Inject
    public TransportExplainAction(Settings settings, ThreadPool threadPool, ClusterService clusterService,
            TransportService transportService, IndicesService indicesService, ScriptService scriptService,
            BigArrays bigArrays, ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
            FetchPhase fetchPhase) {
        super(settings, ExplainAction.NAME, threadPool, clusterService, transportService, actionFilters, indexNameExpressionResolver,
                ExplainRequest::new, ThreadPool.Names.GET);
        this.indicesService = indicesService;
        this.scriptService = scriptService;
        this.bigArrays = bigArrays;
        this.fetchPhase = fetchPhase;
    }

    @Override
    protected void doExecute(ExplainRequest request, ActionListener<ExplainResponse> listener) {
        request.nowInMillis = System.currentTimeMillis();
        super.doExecute(request, listener);
    }

    @Override
    protected boolean resolveIndex(ExplainRequest request) {
        return true;
    }

    @Override
    protected void resolveRequest(ClusterState state, InternalRequest request) {
        request.request().filteringAlias(indexNameExpressionResolver.filteringAliases(state, request.concreteIndex(), request.request().index()));
        // Fail fast on the node that received the request.
        if (request.request().routing() == null && state.getMetaData().routingRequired(request.concreteIndex(), request.request().type())) {
            throw new RoutingMissingException(request.concreteIndex(), request.request().type(), request.request().id());
        }
    }

    @Override
    protected ExplainResponse shardOperation(ExplainRequest request, ShardId shardId) {
        IndexService indexService = indicesService.indexServiceSafe(shardId.getIndex());
        IndexShard indexShard = indexService.getShard(shardId.id());
        Term uidTerm = new Term(UidFieldMapper.NAME, Uid.createUidAsBytes(request.type(), request.id()));
        Engine.GetResult result = indexShard.get(new Engine.Get(false, uidTerm));
        if (!result.exists()) {
            return new ExplainResponse(shardId.getIndexName(), request.type(), request.id(), false);
        }

        SearchContext context = new DefaultSearchContext(0,
                new ShardSearchLocalRequest(new String[] { request.type() }, request.nowInMillis, request.filteringAlias()), null,
                result.searcher(), indexService, indexShard, scriptService, bigArrays,
                threadPool.estimatedTimeInMillisCounter(), parseFieldMatcher, SearchService.NO_TIMEOUT, fetchPhase);
        SearchContext.setCurrent(context);

        try {
            context.parsedQuery(context.getQueryShardContext().toQuery(request.query()));
            context.preProcess();
            int topLevelDocId = result.docIdAndVersion().docId + result.docIdAndVersion().context.docBase;
            Explanation explanation = context.searcher().explain(context.query(), topLevelDocId);
            for (RescoreSearchContext ctx : context.rescore()) {
                Rescorer rescorer = ctx.rescorer();
                explanation = rescorer.explain(topLevelDocId, context, ctx, explanation);
            }
            if (request.fields() != null || (request.fetchSourceContext() != null && request.fetchSourceContext().fetchSource())) {
                // Advantage is that we're not opening a second searcher to retrieve the _source. Also
                // because we are working in the same searcher in engineGetResult we can be sure that a
                // doc isn't deleted between the initial get and this call.
                GetResult getResult = indexShard.getService().get(result, request.id(), request.type(), request.fields(), request.fetchSourceContext());
                return new ExplainResponse(shardId.getIndexName(), request.type(), request.id(), true, explanation, getResult);
            } else {
                return new ExplainResponse(shardId.getIndexName(), request.type(), request.id(), true, explanation);
            }
        } catch (IOException e) {
            throw new ElasticsearchException("Could not explain", e);
        } finally {
            context.close();
            SearchContext.removeCurrent();
        }
    }

    @Override
    protected ExplainResponse newResponse() {
        return new ExplainResponse();
    }

    @Override
    protected ShardIterator shards(ClusterState state, InternalRequest request) {
        return clusterService.operationRouting().getShards(
                clusterService.state(), request.concreteIndex(), request.request().id(), request.request().routing(), request.request().preference()
        );
    }
}
