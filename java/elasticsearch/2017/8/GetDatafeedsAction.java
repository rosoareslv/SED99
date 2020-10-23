/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.Version;
import org.elasticsearch.action.Action;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.ActionRequestValidationException;
import org.elasticsearch.action.ActionResponse;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.master.MasterNodeReadOperationRequestBuilder;
import org.elasticsearch.action.support.master.MasterNodeReadRequest;
import org.elasticsearch.action.support.master.TransportMasterNodeReadAction;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.block.ClusterBlockException;
import org.elasticsearch.cluster.block.ClusterBlockLevel;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.ToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.MlMetadata;
import org.elasticsearch.xpack.ml.action.util.QueryPage;
import org.elasticsearch.xpack.ml.datafeed.DatafeedConfig;
import org.elasticsearch.xpack.ml.utils.ExceptionsHelper;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;

public class GetDatafeedsAction extends Action<GetDatafeedsAction.Request, GetDatafeedsAction.Response,
        GetDatafeedsAction.RequestBuilder> {

    public static final GetDatafeedsAction INSTANCE = new GetDatafeedsAction();
    public static final String NAME = "cluster:monitor/xpack/ml/datafeeds/get";

    public static final String ALL = "_all";

    private GetDatafeedsAction() {
        super(NAME);
    }

    @Override
    public RequestBuilder newRequestBuilder(ElasticsearchClient client) {
        return new RequestBuilder(client, this);
    }

    @Override
    public Response newResponse() {
        return new Response();
    }

    public static class Request extends MasterNodeReadRequest<Request> {

        public static final ParseField ALLOW_NO_DATAFEEDS = new ParseField("allow_no_datafeeds");

        private String datafeedId;
        private boolean allowNoDatafeeds = true;

        public Request(String datafeedId) {
            this();
            this.datafeedId = ExceptionsHelper.requireNonNull(datafeedId, DatafeedConfig.ID.getPreferredName());
        }

        Request() {
            local(true);
        }

        public String getDatafeedId() {
            return datafeedId;
        }

        public boolean allowNoDatafeeds() {
            return allowNoDatafeeds;
        }

        public void setAllowNoDatafeeds(boolean allowNoDatafeeds) {
            this.allowNoDatafeeds = allowNoDatafeeds;
        }

        @Override
        public ActionRequestValidationException validate() {
            return null;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            datafeedId = in.readString();
            if (in.getVersion().onOrAfter(Version.V_6_1_0)) {
                allowNoDatafeeds = in.readBoolean();
            }
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeString(datafeedId);
            if (out.getVersion().onOrAfter(Version.V_6_1_0)) {
                out.writeBoolean(allowNoDatafeeds);
            }
        }

        @Override
        public int hashCode() {
            return Objects.hash(datafeedId, allowNoDatafeeds);
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
            return Objects.equals(datafeedId, other.datafeedId) && Objects.equals(allowNoDatafeeds, other.allowNoDatafeeds);
        }
    }

    public static class RequestBuilder extends MasterNodeReadOperationRequestBuilder<Request, Response, RequestBuilder> {

        public RequestBuilder(ElasticsearchClient client, GetDatafeedsAction action) {
            super(client, action, new Request());
        }
    }

    public static class Response extends ActionResponse implements ToXContentObject {

        private QueryPage<DatafeedConfig> datafeeds;

        public Response(QueryPage<DatafeedConfig> datafeeds) {
            this.datafeeds = datafeeds;
        }

        public Response() {}

        public QueryPage<DatafeedConfig> getResponse() {
            return datafeeds;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            datafeeds = new QueryPage<>(in, DatafeedConfig::new);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            datafeeds.writeTo(out);
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            builder.startObject();
            datafeeds.doXContentBody(builder, params);
            builder.endObject();
            return builder;
        }

        @Override
        public int hashCode() {
            return Objects.hash(datafeeds);
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
            return Objects.equals(datafeeds, other.datafeeds);
        }

        @Override
        public final String toString() {
            return Strings.toString(this);
        }
    }

    public static class TransportAction extends TransportMasterNodeReadAction<Request, Response> {

        @Inject
        public TransportAction(Settings settings, TransportService transportService, ClusterService clusterService,
                ThreadPool threadPool, ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver) {
            super(settings, GetDatafeedsAction.NAME, transportService, clusterService, threadPool, actionFilters,
                    indexNameExpressionResolver, Request::new);
        }

        @Override
        protected String executor() {
            return ThreadPool.Names.SAME;
        }

        @Override
        protected Response newResponse() {
            return new Response();
        }

        @Override
        protected void masterOperation(Request request, ClusterState state, ActionListener<Response> listener) throws Exception {
            logger.debug("Get datafeed '{}'", request.getDatafeedId());

            MlMetadata mlMetadata = state.metaData().custom(MlMetadata.TYPE);
            if (mlMetadata == null) {
                mlMetadata = MlMetadata.EMPTY_METADATA;
            }
            Set<String> expandedDatafeedIds = mlMetadata.expandDatafeedIds(request.getDatafeedId(), request.allowNoDatafeeds());
            List<DatafeedConfig> datafeedConfigs = new ArrayList<>();
            for (String expandedDatafeedId : expandedDatafeedIds) {
                datafeedConfigs.add(mlMetadata.getDatafeed(expandedDatafeedId));
            }

            listener.onResponse(new Response(new QueryPage<>(datafeedConfigs, datafeedConfigs.size(), DatafeedConfig.RESULTS_FIELD)));
        }

        @Override
        protected ClusterBlockException checkBlock(Request request, ClusterState state) {
            return state.blocks().globalBlockedException(ClusterBlockLevel.METADATA_READ);
        }
    }
}
