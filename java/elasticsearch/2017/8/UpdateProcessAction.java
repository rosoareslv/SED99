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
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.StatusToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.rest.RestStatus;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.job.config.JobUpdate;
import org.elasticsearch.xpack.ml.job.config.ModelPlotConfig;
import org.elasticsearch.xpack.ml.job.process.autodetect.AutodetectProcessManager;

import java.io.IOException;
import java.util.List;
import java.util.Objects;

public class UpdateProcessAction extends
        Action<UpdateProcessAction.Request, UpdateProcessAction.Response, UpdateProcessAction.RequestBuilder> {

    public static final UpdateProcessAction INSTANCE = new UpdateProcessAction();
    public static final String NAME = "cluster:internal/xpack/ml/job/update/process";

    private UpdateProcessAction() {
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

        RequestBuilder(ElasticsearchClient client, UpdateProcessAction action) {
            super(client, action, new Request());
        }
    }

    public static class Response extends BaseTasksResponse implements StatusToXContentObject, Writeable {

        private boolean isUpdated;

        private Response() {
            super(null, null);
            this.isUpdated = true;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            isUpdated = in.readBoolean();
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeBoolean(isUpdated);
        }

        public boolean isUpdated() {
            return isUpdated;
        }

        @Override
        public RestStatus status() {
            return RestStatus.ACCEPTED;
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            builder.startObject();
            builder.field("updated", isUpdated);
            builder.endObject();
            return builder;
        }

        @Override
        public int hashCode() {
            return Objects.hashCode(isUpdated);
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

            return this.isUpdated == other.isUpdated;
        }
    }

    public static class Request extends TransportJobTaskAction.JobTaskRequest<Request> {

        private ModelPlotConfig modelPlotConfig;
        private List<JobUpdate.DetectorUpdate> detectorUpdates;

        Request() {
        }

        public Request(String jobId, ModelPlotConfig modelPlotConfig, List<JobUpdate.DetectorUpdate> detectorUpdates) {
            super(jobId);
            this.modelPlotConfig = modelPlotConfig;
            this.detectorUpdates = detectorUpdates;
        }

        public ModelPlotConfig getModelPlotConfig() {
            return modelPlotConfig;
        }

        public List<JobUpdate.DetectorUpdate> getDetectorUpdates() {
            return detectorUpdates;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            modelPlotConfig = in.readOptionalWriteable(ModelPlotConfig::new);
            if (in.readBoolean()) {
                in.readList(JobUpdate.DetectorUpdate::new);
            }
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeOptionalWriteable(modelPlotConfig);
            boolean hasDetectorUpdates = detectorUpdates != null;
            out.writeBoolean(hasDetectorUpdates);
            if (hasDetectorUpdates) {
                out.writeList(detectorUpdates);
            }
        }

        @Override
        public int hashCode() {
            return Objects.hash(getJobId(), modelPlotConfig, detectorUpdates);
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

            return Objects.equals(getJobId(), other.getJobId()) &&
                    Objects.equals(modelPlotConfig, other.modelPlotConfig) &&
                    Objects.equals(detectorUpdates, other.detectorUpdates);
        }
    }

    public static class TransportAction extends TransportJobTaskAction<Request, Response> {

        @Inject
        public TransportAction(Settings settings, TransportService transportService, ThreadPool threadPool, ClusterService clusterService,
                               ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                               AutodetectProcessManager processManager) {
            super(settings, NAME, threadPool, clusterService, transportService, actionFilters, indexNameExpressionResolver,
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
            try {
                processManager.writeUpdateProcessMessage(task, request.getDetectorUpdates(),
                        request.getModelPlotConfig(), e -> {
                            if (e == null) {
                                listener.onResponse(new Response());
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
