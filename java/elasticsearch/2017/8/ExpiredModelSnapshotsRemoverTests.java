/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.job.retention;

import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.search.SearchAction;
import org.elasticsearch.action.search.SearchRequest;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.client.Client;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.MetaData;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.json.JsonXContent;
import org.elasticsearch.mock.orig.Mockito;
import org.elasticsearch.search.SearchHit;
import org.elasticsearch.search.SearchHits;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.xpack.ml.MlMetadata;
import org.elasticsearch.xpack.ml.action.DeleteModelSnapshotAction;
import org.elasticsearch.xpack.ml.job.config.Job;
import org.elasticsearch.xpack.ml.job.config.JobTests;
import org.elasticsearch.xpack.ml.job.persistence.AnomalyDetectorsIndex;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSnapshot;
import org.junit.Before;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.hamcrest.Matchers.equalTo;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.same;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class ExpiredModelSnapshotsRemoverTests extends ESTestCase {

    private Client client;
    private ClusterService clusterService;
    private ClusterState clusterState;
    private List<SearchRequest> capturedSearchRequests;
    private List<DeleteModelSnapshotAction.Request> capturedDeleteModelSnapshotRequests;
    private List<SearchResponse> searchResponsesPerCall;
    private Runnable onFinish;

    @Before
    public void setUpTests() {
        capturedSearchRequests = new ArrayList<>();
        capturedDeleteModelSnapshotRequests = new ArrayList<>();
        searchResponsesPerCall = new ArrayList<>();
        clusterService = mock(ClusterService.class);
        clusterState = mock(ClusterState.class);
        when(clusterService.state()).thenReturn(clusterState);
        client = mock(Client.class);
        onFinish = mock(Runnable.class);
    }

    public void testTrigger_GivenJobsWithoutRetentionPolicy() {
        givenClientRequestsSucceed();
        givenJobs(Arrays.asList(
                JobTests.buildJobBuilder("foo").build(),
                JobTests.buildJobBuilder("bar").build()
        ));

        createExpiredModelSnapshotsRemover().trigger(onFinish);

        verify(onFinish).run();
        Mockito.verifyNoMoreInteractions(client);
    }

    public void testTrigger_GivenJobWithoutActiveSnapshot() {
        givenClientRequestsSucceed();
        givenJobs(Arrays.asList(JobTests.buildJobBuilder("foo").setModelSnapshotRetentionDays(7L).build()));

        createExpiredModelSnapshotsRemover().trigger(onFinish);

        verify(onFinish).run();
        Mockito.verifyNoMoreInteractions(client);
    }

    public void testTrigger_GivenJobsWithMixedRetentionPolicies() throws IOException {
        givenClientRequestsSucceed();
        givenJobs(Arrays.asList(
                JobTests.buildJobBuilder("none").build(),
                JobTests.buildJobBuilder("snapshots-1").setModelSnapshotRetentionDays(7L).setModelSnapshotId("active").build(),
                JobTests.buildJobBuilder("snapshots-2").setModelSnapshotRetentionDays(17L).setModelSnapshotId("active").build()
        ));

        List<ModelSnapshot> snapshots1JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-1", "snapshots-1_1"),
                createModelSnapshot("snapshots-1", "snapshots-1_2"));
        List<ModelSnapshot> snapshots2JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-2", "snapshots-2_1"));
        searchResponsesPerCall.add(createSearchResponse(snapshots1JobSnapshots));
        searchResponsesPerCall.add(createSearchResponse(snapshots2JobSnapshots));

        createExpiredModelSnapshotsRemover().trigger(onFinish);

        assertThat(capturedSearchRequests.size(), equalTo(2));
        SearchRequest searchRequest = capturedSearchRequests.get(0);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-1")}));
        searchRequest = capturedSearchRequests.get(1);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-2")}));

        assertThat(capturedDeleteModelSnapshotRequests.size(), equalTo(3));
        DeleteModelSnapshotAction.Request deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(0);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-1"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-1_1"));
        deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(1);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-1"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-1_2"));
        deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(2);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-2"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-2_1"));

        verify(onFinish).run();
    }

    public void testTrigger_GivenClientSearchRequestsFail() throws IOException {
        givenClientSearchRequestsFail();
        givenJobs(Arrays.asList(
                JobTests.buildJobBuilder("none").build(),
                JobTests.buildJobBuilder("snapshots-1").setModelSnapshotRetentionDays(7L).setModelSnapshotId("active").build(),
                JobTests.buildJobBuilder("snapshots-2").setModelSnapshotRetentionDays(17L).setModelSnapshotId("active").build()
        ));

        List<ModelSnapshot> snapshots1JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-1", "snapshots-1_1"),
                createModelSnapshot("snapshots-1", "snapshots-1_2"));
        List<ModelSnapshot> snapshots2JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-2", "snapshots-2_1"));
        searchResponsesPerCall.add(createSearchResponse(snapshots1JobSnapshots));
        searchResponsesPerCall.add(createSearchResponse(snapshots2JobSnapshots));

        createExpiredModelSnapshotsRemover().trigger(onFinish);

        assertThat(capturedSearchRequests.size(), equalTo(2));
        SearchRequest searchRequest = capturedSearchRequests.get(0);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-1")}));
        searchRequest = capturedSearchRequests.get(1);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-2")}));

        assertThat(capturedDeleteModelSnapshotRequests.size(), equalTo(0));

        verify(onFinish).run();
    }

    public void testTrigger_GivenClientDeleteSnapshotRequestsFail() throws IOException {
        givenClientDeleteModelSnapshotRequestsFail();
        givenJobs(Arrays.asList(
                JobTests.buildJobBuilder("none").build(),
                JobTests.buildJobBuilder("snapshots-1").setModelSnapshotRetentionDays(7L).setModelSnapshotId("active").build(),
                JobTests.buildJobBuilder("snapshots-2").setModelSnapshotRetentionDays(17L).setModelSnapshotId("active").build()
        ));

        List<ModelSnapshot> snapshots1JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-1", "snapshots-1_1"),
                createModelSnapshot("snapshots-1", "snapshots-1_2"));
        List<ModelSnapshot> snapshots2JobSnapshots = Arrays.asList(createModelSnapshot("snapshots-2", "snapshots-2_1"));
        searchResponsesPerCall.add(createSearchResponse(snapshots1JobSnapshots));
        searchResponsesPerCall.add(createSearchResponse(snapshots2JobSnapshots));

        createExpiredModelSnapshotsRemover().trigger(onFinish);

        assertThat(capturedSearchRequests.size(), equalTo(2));
        SearchRequest searchRequest = capturedSearchRequests.get(0);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-1")}));
        searchRequest = capturedSearchRequests.get(1);
        assertThat(searchRequest.indices(), equalTo(new String[] {AnomalyDetectorsIndex.jobResultsAliasedName("snapshots-2")}));

        assertThat(capturedDeleteModelSnapshotRequests.size(), equalTo(3));
        DeleteModelSnapshotAction.Request deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(0);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-1"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-1_1"));
        deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(1);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-1"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-1_2"));
        deleteSnapshotRequest = capturedDeleteModelSnapshotRequests.get(2);
        assertThat(deleteSnapshotRequest.getJobId(), equalTo("snapshots-2"));
        assertThat(deleteSnapshotRequest.getSnapshotId(), equalTo("snapshots-2_1"));

        verify(onFinish).run();
    }

    private void givenJobs(List<Job> jobs) {
        Map<String, Job> jobsMap = new HashMap<>();
        jobs.stream().forEach(job -> jobsMap.put(job.getId(), job));
        MlMetadata mlMetadata = mock(MlMetadata.class);
        when(mlMetadata.getJobs()).thenReturn(jobsMap);
        MetaData metadata = mock(MetaData.class);
        when(metadata.custom(MlMetadata.TYPE)).thenReturn(mlMetadata);
        when(clusterState.getMetaData()).thenReturn(metadata);
    }

    private ExpiredModelSnapshotsRemover createExpiredModelSnapshotsRemover() {
        return new ExpiredModelSnapshotsRemover(client, clusterService);
    }

    private static ModelSnapshot createModelSnapshot(String jobId, String snapshotId) {
        return new ModelSnapshot.Builder(jobId).setSnapshotId(snapshotId).build();
    }

    private static SearchResponse createSearchResponse(List<ModelSnapshot> modelSnapshots) throws IOException {
        SearchHit[] hitsArray = new SearchHit[modelSnapshots.size()];
        for (int i = 0; i < modelSnapshots.size(); i++) {
            hitsArray[i] = new SearchHit(randomInt());
            XContentBuilder jsonBuilder = JsonXContent.contentBuilder();
            modelSnapshots.get(i).toXContent(jsonBuilder, ToXContent.EMPTY_PARAMS);
            hitsArray[i].sourceRef(jsonBuilder.bytes());
        }
        SearchHits hits = new SearchHits(hitsArray, hitsArray.length, 1.0f);
        SearchResponse searchResponse = mock(SearchResponse.class);
        when(searchResponse.getHits()).thenReturn(hits);
        return searchResponse;
    }

    private void givenClientRequestsSucceed() {
        givenClientRequests(true, true);
    }

    private void givenClientSearchRequestsFail() {
        givenClientRequests(false, true);
    }

    private void givenClientDeleteModelSnapshotRequestsFail() {
        givenClientRequests(true, false);
    }

    private void givenClientRequests(boolean shouldSearchRequestsSucceed, boolean shouldDeleteSnapshotRequestsSucceed) {
        doAnswer(new Answer<Void>() {
            int callCount = 0;

            @Override
            public Void answer(InvocationOnMock invocationOnMock) throws Throwable {
                SearchRequest searchRequest = (SearchRequest) invocationOnMock.getArguments()[1];
                capturedSearchRequests.add(searchRequest);
                ActionListener<SearchResponse> listener = (ActionListener<SearchResponse>) invocationOnMock.getArguments()[2];
                if (shouldSearchRequestsSucceed) {
                    listener.onResponse(searchResponsesPerCall.get(callCount++));
                } else {
                    listener.onFailure(new RuntimeException("search failed"));
                }
                return null;
            }
        }).when(client).execute(same(SearchAction.INSTANCE), any(), any());
        doAnswer(new Answer<Void>() {
            @Override
            public Void answer(InvocationOnMock invocationOnMock) throws Throwable {
                capturedDeleteModelSnapshotRequests.add((DeleteModelSnapshotAction.Request) invocationOnMock.getArguments()[1]);
                ActionListener<DeleteModelSnapshotAction.Response> listener =
                        (ActionListener<DeleteModelSnapshotAction.Response>) invocationOnMock.getArguments()[2];
                if (shouldDeleteSnapshotRequestsSucceed) {
                    listener.onResponse(null);
                } else {
                    listener.onFailure(new RuntimeException("delete snapshot failed"));
                }
                return null;
            }
        }).when(client).execute(same(DeleteModelSnapshotAction.INSTANCE), any(), any());
    }
}