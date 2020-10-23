/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.Version;
import org.elasticsearch.action.Action;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.ActionRequestBuilder;
import org.elasticsearch.action.ActionRequestValidationException;
import org.elasticsearch.action.FailedNodeException;
import org.elasticsearch.action.TaskOperationFailure;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.tasks.BaseTasksRequest;
import org.elasticsearch.action.support.tasks.BaseTasksResponse;
import org.elasticsearch.action.support.tasks.TransportTasksAction;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.Nullable;
import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.collect.Tuple;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.common.util.concurrent.AtomicArray;
import org.elasticsearch.common.xcontent.ToXContentObject;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.tasks.Task;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.MlMetadata;
import org.elasticsearch.xpack.ml.action.util.QueryPage;
import org.elasticsearch.xpack.ml.job.config.Job;
import org.elasticsearch.xpack.ml.job.config.JobState;
import org.elasticsearch.xpack.ml.job.persistence.JobProvider;
import org.elasticsearch.xpack.ml.job.process.autodetect.AutodetectProcessManager;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.DataCounts;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSizeStats;
import org.elasticsearch.xpack.ml.utils.ExceptionsHelper;
import org.elasticsearch.xpack.persistent.PersistentTasksCustomMetaData;
import org.elasticsearch.xpack.persistent.PersistentTasksCustomMetaData.PersistentTask;

import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.stream.Collectors;

public class GetJobsStatsAction extends Action<GetJobsStatsAction.Request, GetJobsStatsAction.Response, GetJobsStatsAction.RequestBuilder> {

    public static final GetJobsStatsAction INSTANCE = new GetJobsStatsAction();
    public static final String NAME = "cluster:monitor/xpack/ml/job/stats/get";

    private static final String DATA_COUNTS = "data_counts";
    private static final String MODEL_SIZE_STATS = "model_size_stats";
    private static final String STATE = "state";
    private static final String NODE = "node";

    private GetJobsStatsAction() {
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

    public static class Request extends BaseTasksRequest<Request> {

        public static final ParseField ALLOW_NO_JOBS = new ParseField("allow_no_jobs");

        private String jobId;
        private boolean allowNoJobs = true;

        // used internally to expand _all jobid to encapsulate all jobs in cluster:
        private List<String> expandedJobsIds;

        public Request(String jobId) {
            this.jobId = ExceptionsHelper.requireNonNull(jobId, Job.ID.getPreferredName());
            this.expandedJobsIds = Collections.singletonList(jobId);
        }

        Request() {}

        public void setAllowNoJobs(boolean allowNoJobs) {
            this.allowNoJobs = allowNoJobs;
        }

        public String getJobId() {
            return jobId;
        }

        public boolean allowNoJobs() {
            return allowNoJobs;
        }

        @Override
        public boolean match(Task task) {
            return jobId.equals(MetaData.ALL) || OpenJobAction.JobTask.match(task, jobId);
        }

        @Override
        public ActionRequestValidationException validate() {
            return null;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            jobId = in.readString();
            expandedJobsIds = in.readList(StreamInput::readString);
            if (in.getVersion().onOrAfter(Version.V_6_1_0)) {
                allowNoJobs = in.readBoolean();
            }
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            out.writeString(jobId);
            out.writeStringList(expandedJobsIds);
            if (out.getVersion().onOrAfter(Version.V_6_1_0)) {
                out.writeBoolean(allowNoJobs);
            }
        }

        @Override
        public int hashCode() {
            return Objects.hash(jobId, allowNoJobs);
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
            return Objects.equals(jobId, other.jobId) && Objects.equals(allowNoJobs, other.allowNoJobs);
        }
    }

    public static class RequestBuilder extends ActionRequestBuilder<Request, Response, RequestBuilder> {

        public RequestBuilder(ElasticsearchClient client, GetJobsStatsAction action) {
            super(client, action, new Request());
        }
    }

    public static class Response extends BaseTasksResponse implements ToXContentObject {

        public static class JobStats implements ToXContentObject, Writeable {
            private final String jobId;
            private DataCounts dataCounts;
            @Nullable
            private ModelSizeStats modelSizeStats;
            @Nullable
            private TimeValue openTime;
            private JobState state;
            @Nullable
            private DiscoveryNode node;
            @Nullable
            private String assignmentExplanation;

            public JobStats(String jobId, DataCounts dataCounts, @Nullable ModelSizeStats modelSizeStats, JobState state,
                     @Nullable  DiscoveryNode node, @Nullable String assignmentExplanation, @Nullable TimeValue opentime) {
                this.jobId = Objects.requireNonNull(jobId);
                this.dataCounts = Objects.requireNonNull(dataCounts);
                this.modelSizeStats = modelSizeStats;
                this.state = Objects.requireNonNull(state);
                this.node = node;
                this.assignmentExplanation = assignmentExplanation;
                this.openTime = opentime;
            }

            JobStats(StreamInput in) throws IOException {
                jobId = in.readString();
                dataCounts = new DataCounts(in);
                modelSizeStats = in.readOptionalWriteable(ModelSizeStats::new);
                state = JobState.fromStream(in);
                node = in.readOptionalWriteable(DiscoveryNode::new);
                assignmentExplanation = in.readOptionalString();
                openTime = in.readOptionalWriteable(TimeValue::new);
            }

            public String getJobId() {
                return jobId;
            }

            public DataCounts getDataCounts() {
                return dataCounts;
            }

            public ModelSizeStats getModelSizeStats() {
                return modelSizeStats;
            }

            public JobState getState() {
                return state;
            }

            public DiscoveryNode getNode() {
                return node;
            }

            public String getAssignmentExplanation() {
                return assignmentExplanation;
            }

            public TimeValue getOpenTime() {
                return openTime;
            }

            @Override
            public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
                // TODO: Have callers wrap the content with an object as they choose rather than forcing it upon them
                builder.startObject();
                {
                    toUnwrappedXContent(builder);
                }
                return builder.endObject();
            }

            public XContentBuilder toUnwrappedXContent(XContentBuilder builder) throws IOException {
                builder.field(Job.ID.getPreferredName(), jobId);
                builder.field(DATA_COUNTS, dataCounts);
                if (modelSizeStats != null) {
                    builder.field(MODEL_SIZE_STATS, modelSizeStats);
                }
                builder.field(STATE, state.toString());
                if (node != null) {
                    builder.startObject(NODE);
                    builder.field("id", node.getId());
                    builder.field("name", node.getName());
                    builder.field("ephemeral_id", node.getEphemeralId());
                    builder.field("transport_address", node.getAddress().toString());

                    builder.startObject("attributes");
                    for (Map.Entry<String, String> entry : node.getAttributes().entrySet()) {
                        builder.field(entry.getKey(), entry.getValue());
                    }
                    builder.endObject();
                    builder.endObject();
                }
                if (assignmentExplanation != null) {
                    builder.field("assignment_explanation", assignmentExplanation);
                }
                if (openTime != null) {
                    builder.field("open_time", openTime.getStringRep());
                }
                return builder;
            }

            @Override
            public void writeTo(StreamOutput out) throws IOException {
                out.writeString(jobId);
                dataCounts.writeTo(out);
                out.writeOptionalWriteable(modelSizeStats);
                state.writeTo(out);
                out.writeOptionalWriteable(node);
                out.writeOptionalString(assignmentExplanation);
                out.writeOptionalWriteable(openTime);
            }

            @Override
            public int hashCode() {
                return Objects.hash(jobId, dataCounts, modelSizeStats, state, node, assignmentExplanation, openTime);
            }

            @Override
            public boolean equals(Object obj) {
                if (obj == null) {
                    return false;
                }
                if (getClass() != obj.getClass()) {
                    return false;
                }
                JobStats other = (JobStats) obj;
                return Objects.equals(jobId, other.jobId)
                        && Objects.equals(this.dataCounts, other.dataCounts)
                        && Objects.equals(this.modelSizeStats, other.modelSizeStats)
                        && Objects.equals(this.state, other.state)
                        && Objects.equals(this.node, other.node)
                        && Objects.equals(this.assignmentExplanation, other.assignmentExplanation)
                        && Objects.equals(this.openTime, other.openTime);
            }
        }

        private QueryPage<JobStats> jobsStats;

        public Response(QueryPage<JobStats> jobsStats) {
            super(Collections.emptyList(), Collections.emptyList());
            this.jobsStats = jobsStats;
        }

        Response(List<TaskOperationFailure> taskFailures, List<? extends FailedNodeException> nodeFailures,
                 QueryPage<JobStats> jobsStats) {
            super(taskFailures, nodeFailures);
            this.jobsStats = jobsStats;
        }

        public Response() {
            super(Collections.emptyList(), Collections.emptyList());
        }

        public QueryPage<JobStats> getResponse() {
            return jobsStats;
        }

        @Override
        public void readFrom(StreamInput in) throws IOException {
            super.readFrom(in);
            jobsStats = new QueryPage<>(in, JobStats::new);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            super.writeTo(out);
            jobsStats.writeTo(out);
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
            builder.startObject();;
            jobsStats.doXContentBody(builder, params);
            builder.endObject();
            return builder;
        }

        @Override
        public int hashCode() {
            return Objects.hash(jobsStats);
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
            return Objects.equals(jobsStats, other.jobsStats);
        }

        @Override
        public final String toString() {
            return Strings.toString(this);
        }
    }

    public static class TransportAction extends TransportTasksAction<OpenJobAction.JobTask, Request, Response,
            QueryPage<Response.JobStats>> {

        private final ClusterService clusterService;
        private final AutodetectProcessManager processManager;
        private final JobProvider jobProvider;

        @Inject
        public TransportAction(Settings settings, TransportService transportService, ThreadPool threadPool, ActionFilters actionFilters,
                               ClusterService clusterService, IndexNameExpressionResolver indexNameExpressionResolver,
                               AutodetectProcessManager processManager, JobProvider jobProvider) {
            super(settings, GetJobsStatsAction.NAME, threadPool, clusterService, transportService, actionFilters,
                    indexNameExpressionResolver, Request::new, Response::new, ThreadPool.Names.MANAGEMENT);
            this.clusterService = clusterService;
            this.processManager = processManager;
            this.jobProvider = jobProvider;
        }

        @Override
        protected void doExecute(Task task, Request request, ActionListener<Response> listener) {
            MlMetadata clusterMlMetadata = clusterService.state().metaData().custom(MlMetadata.TYPE);
            MlMetadata mlMetadata = (clusterMlMetadata == null) ? MlMetadata.EMPTY_METADATA : clusterMlMetadata;
            request.expandedJobsIds = new ArrayList<>(mlMetadata.expandJobIds(request.getJobId(), request.allowNoJobs()));
            ActionListener<Response> finalListener = listener;
            listener = ActionListener.wrap(response -> gatherStatsForClosedJobs(mlMetadata,
                    request, response, finalListener), listener::onFailure);
            super.doExecute(task, request, listener);
        }

        @Override
        protected Response newResponse(Request request, List<QueryPage<Response.JobStats>> tasks,
                                       List<TaskOperationFailure> taskOperationFailures,
                                       List<FailedNodeException> failedNodeExceptions) {
            List<Response.JobStats> stats = new ArrayList<>();
            for (QueryPage<Response.JobStats> task : tasks) {
                stats.addAll(task.results());
            }
            return new Response(taskOperationFailures, failedNodeExceptions, new QueryPage<>(stats, stats.size(), Job.RESULTS_FIELD));
        }

        @Override
        protected QueryPage<Response.JobStats> readTaskResponse(StreamInput in) throws IOException {
            return new QueryPage<>(in, Response.JobStats::new);
        }

        @Override
        protected void taskOperation(Request request, OpenJobAction.JobTask task,
                                     ActionListener<QueryPage<Response.JobStats>> listener) {
            String jobId = task.getJobId();
            logger.debug("Get stats for job [{}]", jobId);
            ClusterState state = clusterService.state();
            PersistentTasksCustomMetaData tasks = state.getMetaData().custom(PersistentTasksCustomMetaData.TYPE);
            Optional<Tuple<DataCounts, ModelSizeStats>> stats = processManager.getStatistics(task);
            if (stats.isPresent()) {
                PersistentTask<?> pTask = MlMetadata.getJobTask(jobId, tasks);
                DiscoveryNode node = state.nodes().get(pTask.getExecutorNode());
                JobState jobState = MlMetadata.getJobState(jobId, tasks);
                String assignmentExplanation = pTask.getAssignment().getExplanation();
                TimeValue openTime = durationToTimeValue(processManager.jobOpenTime(task));
                Response.JobStats jobStats = new Response.JobStats(jobId, stats.get().v1(), stats.get().v2(), jobState,
                        node, assignmentExplanation, openTime);
                listener.onResponse(new QueryPage<>(Collections.singletonList(jobStats), 1, Job.RESULTS_FIELD));
            } else {
                listener.onResponse(new QueryPage<>(Collections.emptyList(), 0, Job.RESULTS_FIELD));
            }
        }

        // Up until now we gathered the stats for jobs that were open,
        // This method will fetch the stats for missing jobs, that was stored in the jobs index
        void gatherStatsForClosedJobs(MlMetadata mlMetadata, Request request, Response response,
                                      ActionListener<Response> listener) {
            List<String> jobIds = determineNonDeletedJobIdsWithoutLiveStats(mlMetadata,
                    request.expandedJobsIds, response.jobsStats.results());
            if (jobIds.isEmpty()) {
                listener.onResponse(response);
                return;
            }

            AtomicInteger counter = new AtomicInteger(jobIds.size());
            AtomicArray<Response.JobStats> jobStats = new AtomicArray<>(jobIds.size());
            PersistentTasksCustomMetaData tasks = clusterService.state().getMetaData().custom(PersistentTasksCustomMetaData.TYPE);
            for (int i = 0; i < jobIds.size(); i++) {
                int slot = i;
                String jobId = jobIds.get(i);
                gatherDataCountsAndModelSizeStats(jobId, (dataCounts, modelSizeStats) -> {
                    JobState jobState = MlMetadata.getJobState(jobId, tasks);
                    PersistentTasksCustomMetaData.PersistentTask<?> pTask = MlMetadata.getJobTask(jobId, tasks);
                    String assignmentExplanation = null;
                    if (pTask != null) {
                        assignmentExplanation = pTask.getAssignment().getExplanation();
                    }
                    jobStats.set(slot, new Response.JobStats(jobId, dataCounts, modelSizeStats, jobState, null,
                            assignmentExplanation, null));
                    if (counter.decrementAndGet() == 0) {
                        List<Response.JobStats> results = response.getResponse().results();
                        results.addAll(jobStats.asList());
                        listener.onResponse(new Response(response.getTaskFailures(), response.getNodeFailures(),
                                new QueryPage<>(results, results.size(), Job.RESULTS_FIELD)));
                    }
                }, listener::onFailure);
            }
        }

        void gatherDataCountsAndModelSizeStats(String jobId, BiConsumer<DataCounts, ModelSizeStats> handler,
                                                       Consumer<Exception> errorHandler) {
            jobProvider.dataCounts(jobId, dataCounts -> {
                jobProvider.modelSizeStats(jobId, modelSizeStats -> {
                    handler.accept(dataCounts, modelSizeStats);
                }, errorHandler);
            }, errorHandler);
        }

        static TimeValue durationToTimeValue(Optional<Duration> duration) {
            if (duration.isPresent()) {
                return TimeValue.timeValueSeconds(duration.get().getSeconds());
            } else {
                return null;
            }
        }

        static List<String> determineNonDeletedJobIdsWithoutLiveStats(MlMetadata mlMetadata,
                                                                      List<String> requestedJobIds,
                                                                      List<Response.JobStats> stats) {
            Set<String> excludeJobIds = stats.stream().map(Response.JobStats::getJobId).collect(Collectors.toSet());
            return requestedJobIds.stream().filter(jobId -> !excludeJobIds.contains(jobId) &&
                    !mlMetadata.isJobDeleted(jobId)).collect(Collectors.toList());
        }
    }
}
