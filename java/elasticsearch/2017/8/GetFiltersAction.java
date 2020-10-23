/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.action.Action;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.ActionRequest;
import org.elasticsearch.action.ActionRequestBuilder;
import org.elasticsearch.action.ActionRequestValidationException;
import org.elasticsearch.action.ActionResponse;
import org.elasticsearch.action.get.GetRequest;
import org.elasticsearch.action.get.GetResponse;
import org.elasticsearch.action.get.TransportGetAction;
import org.elasticsearch.action.search.SearchRequest;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.action.search.TransportSearchAction;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.HandledTransportAction;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.NamedXContentRegistry;
import org.elasticsearch.common.xcontent.StatusToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.index.query.QueryBuilders;
import org.elasticsearch.rest.RestStatus;
import org.elasticsearch.search.SearchHit;
import org.elasticsearch.search.builder.SearchSourceBuilder;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.MlMetaIndex;
import org.elasticsearch.xpack.ml.action.util.PageParams;
import org.elasticsearch.xpack.ml.action.util.QueryPage;
import org.elasticsearch.xpack.ml.job.config.MlFilter;
import org.elasticsearch.xpack.ml.job.persistence.JobProvider;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

import static org.elasticsearch.action.ValidateActions.addValidationError;


public class GetFiltersAction extends Action<GetFiltersAction.Request, GetFiltersAction.Response, GetFiltersAction.RequestBuilder> {

    public static final GetFiltersAction INSTANCE = new GetFiltersAction();
    public static final String NAME = "cluster:admin/xpack/ml/filters/get";

    private GetFiltersAction() {
        super(NAME);
    }

    @Override
    public RequestBuilder newRequestBuilder(ElasticsearchClient client) {
        return new RequestBuilder(client);
    }

    @Override
    public Response newResponse() {
        return new Response();
    }

    public static class Request extends ActionRequest {

        private String filterId;
        private PageParams pageParams;

        public Request() {
        }

        public void setFilterId(String filterId) {
            if (pageParams != null) {
                throw new IllegalArgumentException("Param [" + MlFilter.ID.getPreferredName() + "] is incompatible with ["
                        + PageParams.FROM.getPreferredName()+ ", " + PageParams.SIZE.getPreferredName() + "].");
            }
            this.filterId = filterId;
        }

        public String getFilterId() {
            return filterId;
        }

        public PageParams getPageParams() {
            return pageParams;
        }

        public void setPageParams(PageParams pageParams) {
            if (filterId != null) {
                throw new IllegalArgumentException("Param [" + PageParams.FROM.getPreferredName()
                        + ", " + PageParams.SIZE.getPreferredName() + "] is incompatible with ["
                        + MlFilter.ID.getPreferredName() + "].");
            }
            this.pageParams = pageParams;
        }

        @Override
        public ActionRequestValidationException validate() {
            ActionRequestValidationException validationException = null;
            if (pageParams == null && filterId == null) {
                validationException = addValidationError("Both [" + MlFilter.ID.getPreferredName() + "] and ["
                        + PageParams.FROM.getPreferredName() + ", " + PageParams.SIZE.getPreferredName() + "] "
                        + "cannot be null" , validationException);
            }
            return validationException;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            filterId = in.readOptionalString();
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeOptionalString(filterId);
        }

        @Override
        public int hashCode() {
            return Objects.hash(filterId);
        }

        @Override
        public boolean equals(Object obj) {
            if (obj == null) {
                return false;
            }
            if (getClass() != obj.getClass()) {
                return false;
            }
            Request other = (Request) obj;
            return Objects.equals(filterId, other.filterId);
        }
    }

    public static class RequestBuilder extends ActionRequestBuilder<Request, Response, RequestBuilder> {

        public RequestBuilder(ElasticsearchClient client) {
            super(client, INSTANCE, new Request());
        }
    }

    public static class Response extends ActionResponse implements StatusToXContentObject {

        private QueryPage<MlFilter> filters;

        public Response(QueryPage<MlFilter> filters) {
            this.filters = filters;
        }

        Response() {
        }

        public QueryPage<MlFilter> getFilters() {
            return filters;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            filters = new QueryPage<>(in, MlFilter::new);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            filters.writeTo(out);
        }

        @Override
        public RestStatus status() {
            return RestStatus.OK;
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            builder.startObject();
            filters.doXContentBody(builder, params);
            builder.endObject();
            return builder;
        }

        @Override
        public int hashCode() {
            return Objects.hash(filters);
        }

        @Override
        public boolean equals(Object obj) {
            if (obj == null) {
                return false;
            }
            if (getClass() != obj.getClass()) {
                return false;
            }
            Response other = (Response) obj;
            return Objects.equals(filters, other.filters);
        }

        @Override
        public final String toString() {
            return Strings.toString(this);
        }
    }

    public static class TransportAction extends HandledTransportAction<Request, Response> {

        private final TransportGetAction transportGetAction;
        private final TransportSearchAction transportSearchAction;

        @Inject
        public TransportAction(Settings settings, ThreadPool threadPool,
                TransportService transportService, ActionFilters actionFilters,
                IndexNameExpressionResolver indexNameExpressionResolver,
                TransportGetAction transportGetAction, TransportSearchAction transportSearchAction) {
            super(settings, NAME, threadPool, transportService, actionFilters,
                    indexNameExpressionResolver, Request::new);
            this.transportGetAction = transportGetAction;
            this.transportSearchAction = transportSearchAction;
        }

        @Override
        protected void doExecute(Request request, ActionListener<Response> listener) {
            final String filterId = request.getFilterId();
            if (!Strings.isNullOrEmpty(filterId)) {
                getFilter(filterId, listener);
            } else if (request.getPageParams() != null) {
                getFilters(request.getPageParams(), listener);
            } else {
                throw new IllegalStateException("Both filterId and pageParams are null");
            }
        }

        private void getFilter(String filterId, ActionListener<Response> listener) {
            GetRequest getRequest = new GetRequest(MlMetaIndex.INDEX_NAME, MlMetaIndex.TYPE, MlFilter.documentId(filterId));
            transportGetAction.execute(getRequest, new ActionListener<GetResponse>() {
                @Override
                public void onResponse(GetResponse getDocResponse) {

                    try {
                        QueryPage<MlFilter> responseBody;
                        if (getDocResponse.isExists()) {
                            BytesReference docSource = getDocResponse.getSourceAsBytesRef();
                            XContentParser parser =
                                    XContentFactory.xContent(docSource).createParser(NamedXContentRegistry.EMPTY, docSource);
                            MlFilter filter = MlFilter.PARSER.apply(parser, null).build();
                            responseBody = new QueryPage<>(Collections.singletonList(filter), 1, MlFilter.RESULTS_FIELD);

                            Response filterResponse = new Response(responseBody);
                            listener.onResponse(filterResponse);
                        } else {
                            this.onFailure(QueryPage.emptyQueryPage(MlFilter.RESULTS_FIELD));
                        }

                    } catch (Exception e) {
                        this.onFailure(e);
                    }
                }

                @Override
                public void onFailure(Exception e) {
                    listener.onFailure(e);
                }
            });
        }

        private void getFilters(PageParams pageParams, ActionListener<Response> listener) {
            SearchSourceBuilder sourceBuilder = new SearchSourceBuilder()
                    .from(pageParams.getFrom())
                    .size(pageParams.getSize())
                    .query(QueryBuilders.termQuery(MlFilter.TYPE.getPreferredName(), MlFilter.FILTER_TYPE));

            SearchRequest searchRequest = new SearchRequest(MlMetaIndex.INDEX_NAME)
                    .indicesOptions(JobProvider.addIgnoreUnavailable(SearchRequest.DEFAULT_INDICES_OPTIONS))
                    .source(sourceBuilder);

            transportSearchAction.execute(searchRequest, new ActionListener<SearchResponse>() {
                @Override
                public void onResponse(SearchResponse response) {
                    List<MlFilter> docs = new ArrayList<>();
                    for (SearchHit hit : response.getHits().getHits()) {
                        BytesReference docSource = hit.getSourceRef();
                        try (XContentParser parser = XContentFactory.xContent(docSource).createParser(
                                NamedXContentRegistry.EMPTY, docSource)) {
                            docs.add(MlFilter.PARSER.apply(parser, null).build());
                        } catch (IOException e) {
                            this.onFailure(e);
                        }
                    }

                    Response filterResponse = new Response(new QueryPage<>(docs, docs.size(), MlFilter.RESULTS_FIELD));
                    listener.onResponse(filterResponse);
                }


                @Override
                public void onFailure(Exception e) {
                    listener.onFailure(e);
                }
            });
        }
    }
}

