/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.action.Action;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.ActionRequestBuilder;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.tasks.BaseTasksResponse;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.StatusToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.rest.RestStatus;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.job.config.DataDescription;
import org.elasticsearch.xpack.ml.job.process.autodetect.AutodetectProcessManager;
import org.elasticsearch.xpack.ml.job.process.autodetect.params.DataLoadParams;
import org.elasticsearch.xpack.ml.job.process.autodetect.params.TimeRange;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.DataCounts;

import java.io.IOException;
import java.util.Objects;
import java.util.Optional;

public class PostDataAction extends Action<PostDataAction.Request, PostDataAction.Response, PostDataAction.RequestBuilder> {

    public static final PostDataAction INSTANCE = new PostDataAction();
    public static final String NAME = "cluster:admin/xpack/ml/job/data/post";

    private PostDataAction() {
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

    static class RequestBuilder extends ActionRequestBuilder<Request, Response, RequestBuilder> {

        RequestBuilder(ElasticsearchClient client, PostDataAction action) {
            super(client, action, new Request());
        }
    }

    public static class Response extends BaseTasksResponse implements StatusToXContentObject, Writeable {

        private DataCounts dataCounts;

        Response(String jobId) {
            super(null, null);
            dataCounts = new DataCounts(jobId);
        }

        private Response() {
            super(null, null);
        }

        public Response(DataCounts counts) {
            super(null, null);
            this.dataCounts = counts;
        }

        public DataCounts getDataCounts() {
            return dataCounts;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            dataCounts = new DataCounts(in);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            dataCounts.writeTo(out);
        }

        @Override
        public RestStatus status() {
            return RestStatus.ACCEPTED;
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            builder.startObject();
            dataCounts.doXContentBody(builder, params);
            builder.endObject();
            return builder;
        }

        @Override
        public int hashCode() {
            return Objects.hashCode(dataCounts);
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

            return Objects.equals(dataCounts, other.dataCounts);

        }
    }

    public static class Request extends TransportJobTaskAction.JobTaskRequest<Request> {

        public static final ParseField RESET_START = new ParseField("reset_start");
        public static final ParseField RESET_END = new ParseField("reset_end");

        private String resetStart = "";
        private String resetEnd = "";
        private DataDescription dataDescription;
        private XContentType xContentType;
        private BytesReference content;

        Request() {
        }

        public Request(String jobId) {
            super(jobId);
        }

        public String getResetStart() {
            return resetStart;
        }

        public void setResetStart(String resetStart) {
            this.resetStart = resetStart;
        }

        public String getResetEnd() {
            return resetEnd;
        }

        public void setResetEnd(String resetEnd) {
            this.resetEnd = resetEnd;
        }

        public DataDescription getDataDescription() {
            return dataDescription;
        }

        public void setDataDescription(DataDescription dataDescription) {
            this.dataDescription = dataDescription;
        }

        public BytesReference getContent() { return content; }

        public XContentType getXContentType() {
            return xContentType;
        }

        public void setContent(BytesReference content, XContentType xContentType) {
            this.content = content;
            this.xContentType = xContentType;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            resetStart = in.readOptionalString();
            resetEnd = in.readOptionalString();
            dataDescription = in.readOptionalWriteable(DataDescription::new);
            content = in.readBytesReference();
            if (in.readBoolean()) {
                xContentType = XContentType.readFrom(in);
            }
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeOptionalString(resetStart);
            out.writeOptionalString(resetEnd);
            out.writeOptionalWriteable(dataDescription);
            out.writeBytesReference(content);
            boolean hasXContentType = xContentType != null;
            out.writeBoolean(hasXContentType);
            if (hasXContentType) {
                xContentType.writeTo(out);
            }
        }

        @Override
        public int hashCode() {
            // content stream not included
            return Objects.hash(jobId, resetStart, resetEnd, dataDescription, xContentType);
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

            // content stream not included
            return Objects.equals(jobId, other.jobId) &&
                    Objects.equals(resetStart, other.resetStart) &&
                    Objects.equals(resetEnd, other.resetEnd) &&
                    Objects.equals(dataDescription, other.dataDescription) &&
                    Objects.equals(xContentType, other.xContentType);
        }
    }


    public static class TransportAction extends TransportJobTaskAction<Request, Response> {

        @Inject
        public TransportAction(Settings settings, TransportService transportService, ThreadPool threadPool, ClusterService clusterService,
                ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                               AutodetectProcessManager processManager) {
            super(settings, PostDataAction.NAME, threadPool, clusterService, transportService, actionFilters, indexNameExpressionResolver,
                    Request::new, Response::new, ThreadPool.Names.SAME, processManager);
            // ThreadPool.Names.SAME, because operations is executed by autodetect worker thread
        }

        @Override
        protected Response readTaskResponse(StreamInput in) throws IOException {
            Response response = new Response();
            response.readFrom(in);
            return response;
        }

        @Override
        protected void taskOperation(Request request, OpenJobAction.JobTask task, ActionListener<Response> listener) {
            TimeRange timeRange = TimeRange.builder().startTime(request.getResetStart()).endTime(request.getResetEnd()).build();
            DataLoadParams params = new DataLoadParams(timeRange, Optional.ofNullable(request.getDataDescription()));
            try {
                processManager.processData(task, request.content.streamInput(), request.getXContentType(), params, (dataCounts, e) -> {
                    if (dataCounts != null) {
                        listener.onResponse(new Response(dataCounts));
                    } else {
                        listener.onFailure(e);
                    }
                });
            } catch (Exception e) {
                listener.onFailure(e);
            }
        }

    }
}
