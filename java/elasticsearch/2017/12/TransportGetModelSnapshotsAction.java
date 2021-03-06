/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.action;

import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.HandledTransportAction;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.ml.action.util.QueryPage;
import org.elasticsearch.xpack.ml.job.JobManager;
import org.elasticsearch.xpack.ml.job.persistence.JobProvider;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSnapshot;

import java.util.stream.Collectors;

public class TransportGetModelSnapshotsAction extends HandledTransportAction<GetModelSnapshotsAction.Request,
        GetModelSnapshotsAction.Response> {

    private final JobProvider jobProvider;
    private final JobManager jobManager;

    @Inject
    public TransportGetModelSnapshotsAction(Settings settings, TransportService transportService, ThreadPool threadPool,
                                            ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                                            JobProvider jobProvider, JobManager jobManager) {
        super(settings, GetModelSnapshotsAction.NAME, threadPool, transportService, actionFilters, indexNameExpressionResolver,
                GetModelSnapshotsAction.Request::new);
        this.jobProvider = jobProvider;
        this.jobManager = jobManager;
    }

    @Override
    protected void doExecute(GetModelSnapshotsAction.Request request, ActionListener<GetModelSnapshotsAction.Response> listener) {
        logger.debug("Get model snapshots for job {} snapshot ID {}. from = {}, size = {}"
                + " start = '{}', end='{}', sort={} descending={}",
                request.getJobId(), request.getSnapshotId(), request.getPageParams().getFrom(), request.getPageParams().getSize(),
                request.getStart(), request.getEnd(), request.getSort(), request.getDescOrder());

        jobManager.getJobOrThrowIfUnknown(request.getJobId());

        jobProvider.modelSnapshots(request.getJobId(), request.getPageParams().getFrom(), request.getPageParams().getSize(),
                request.getStart(), request.getEnd(), request.getSort(), request.getDescOrder(), request.getSnapshotId(),
                page -> {
                    listener.onResponse(new GetModelSnapshotsAction.Response(clearQuantiles(page)));
                }, listener::onFailure);
    }

    public static QueryPage<ModelSnapshot> clearQuantiles(QueryPage<ModelSnapshot> page) {
        if (page.results() == null) {
            return page;
        }
        return new QueryPage<>(page.results().stream().map(snapshot ->
                new ModelSnapshot.Builder(snapshot).setQuantiles(null).build())
                .collect(Collectors.toList()), page.count(), page.getResultsField());
    }
}
