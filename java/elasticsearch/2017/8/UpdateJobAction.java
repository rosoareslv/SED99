/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.action.Action;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.ActionRequestValidationException;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.master.AcknowledgedRequest;
import org.elasticsearch.action.support.master.MasterNodeOperationRequestBuilder;
import org.elasticsearch.action.support.master.TransportMasterNodeAction;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.block.ClusterBlockException;
import org.elasticsearch.cluster.block.ClusterBlockLevel;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.ToXContent.Params;
import org.elasticsearch.common.xcontent.ToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.job.JobManager;
import org.elasticsearch.xpack.ml.job.config.JobUpdate;

import java.io.IOException;
import java.util.Objects;

public class UpdateJobAction extends Action<UpdateJobAction.Request, PutJobAction.Response, UpdateJobAction.RequestBuilder> {
    public static final UpdateJobAction INSTANCE = new UpdateJobAction();
    public static final String NAME = "cluster:admin/xpack/ml/job/update";

    private UpdateJobAction() {
        super(NAME);
    }

    @Override
    public UpdateJobAction.RequestBuilder newRequestBuilder(ElasticsearchClient client) {
        return new UpdateJobAction.RequestBuilder(client, this);
    }

    @Override
    public PutJobAction.Response newResponse() {
        return new PutJobAction.Response();
    }

    public static class Request extends AcknowledgedRequest<UpdateJobAction.Request> implements ToXContentObject {

        public static UpdateJobAction.Request parseRequest(String jobId, XContentParser parser) {
            JobUpdate update = JobUpdate.PARSER.apply(parser, null).setJobId(jobId).build();
            return new UpdateJobAction.Request(jobId, update);
        }

        private String jobId;
        private JobUpdate update;

        public Request(String jobId, JobUpdate update) {
            this.jobId = jobId;
            this.update = update;
        }

        Request() {
        }

        public String getJobId() {
            return jobId;
        }

        public JobUpdate getJobUpdate() {
            return update;
        }

        @Override
        public ActionRequestValidationException validate() {
            return null;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            jobId = in.readString();
            update = new JobUpdate(in);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeString(jobId);
            update.writeTo(out);
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            // only serialize the update, as the job id is specified as part of the url
            update.toXContent(builder, params);
            return builder;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;
            UpdateJobAction.Request request = (UpdateJobAction.Request) o;
            return Objects.equals(jobId, request.jobId) &&
                    Objects.equals(update, request.update);
        }

        @Override
        public int hashCode() {
            return Objects.hash(jobId, update);
        }

        @Override
        public final String toString() {
            return Strings.toString(this);
        }
    }

    public static class RequestBuilder extends MasterNodeOperationRequestBuilder<Request, PutJobAction.Response, RequestBuilder> {

        public RequestBuilder(ElasticsearchClient client, UpdateJobAction action) {
            super(client, action, new Request());
        }
    }

    public static class TransportAction extends TransportMasterNodeAction<UpdateJobAction.Request, PutJobAction.Response> {

        private final JobManager jobManager;

        @Inject
        public TransportAction(Settings settings, TransportService transportService, ClusterService clusterService,
                               ThreadPool threadPool, ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                               JobManager jobManager) {
            super(settings, UpdateJobAction.NAME, transportService, clusterService, threadPool, actionFilters,
                    indexNameExpressionResolver, UpdateJobAction.Request::new);
            this.jobManager = jobManager;
        }

        @Override
        protected String executor() {
            return ThreadPool.Names.SAME;
        }

        @Override
        protected PutJobAction.Response newResponse() {
            return new PutJobAction.Response();
        }

        @Override
        protected void masterOperation(Request request, ClusterState state,
                                       ActionListener<PutJobAction.Response> listener) throws Exception {
            if (request.getJobId().equals(MetaData.ALL)) {
                throw new IllegalArgumentException("Job Id " + MetaData.ALL + " cannot be for update");
            }

            jobManager.updateJob(request.getJobId(), request.getJobUpdate(), request, listener);
        }

        @Override
        protected ClusterBlockException checkBlock(UpdateJobAction.Request request, ClusterState state) {
            return state.blocks().globalBlockedException(ClusterBlockLevel.METADATA_WRITE);
        }
    }
}
