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

package org.elasticsearch.index.reindex;

import org.elasticsearch.action.ListenableActionFuture;
import org.elasticsearch.action.admin.cluster.node.tasks.list.ListTasksResponse;
import org.elasticsearch.action.admin.cluster.node.tasks.list.TaskGroup;
import org.elasticsearch.action.index.IndexRequestBuilder;
import org.elasticsearch.tasks.TaskId;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

import static org.hamcrest.Matchers.allOf;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.lessThan;
import static org.hamcrest.Matchers.lessThanOrEqualTo;

/**
 * Tests that you can set requests_per_second over the Java API and that you can rethrottle running requests. There are REST tests for this
 * too but this is the only place that tests running against multiple nodes so it is the only integration tests that checks for
 * serialization.
 */
public class RethrottleTests extends ReindexTestCase {

    public void testReindex() throws Exception {
        testCase(reindex().source("test").destination("dest"), ReindexAction.NAME);
    }

    public void testUpdateByQuery() throws Exception {
        testCase(updateByQuery().source("test"), UpdateByQueryAction.NAME);
    }

    public void testDeleteByQuery() throws Exception {
        testCase(deleteByQuery().source("test"), DeleteByQueryAction.NAME);
    }

    public void testReindexWithWorkers() throws Exception {
        testCase(reindex().source("test").destination("dest").setSlices(between(2, 10)), ReindexAction.NAME);
    }

    public void testUpdateByQueryWithWorkers() throws Exception {
        testCase(updateByQuery().source("test").setSlices(between(2, 10)), UpdateByQueryAction.NAME);
    }

    public void testDeleteByQueryWithWorkers() throws Exception {
        testCase(deleteByQuery().source("test").setSlices(between(2, 10)), DeleteByQueryAction.NAME);
    }

    private void testCase(AbstractBulkByScrollRequestBuilder<?, ?> request, String actionName) throws Exception {
        logger.info("Starting test for [{}] with [{}] slices", actionName, request.request().getSlices());
        /* Add ten documents per slice so most slices will have many documents to process, having to go to multiple batches.
         * we can't rely on all of them doing so, but 
         */
        List<IndexRequestBuilder> docs = new ArrayList<>();
        for (int i = 0; i < request.request().getSlices() * 10; i++) {
            docs.add(client().prepareIndex("test", "test", Integer.toString(i)).setSource("foo", "bar"));
        }
        indexRandom(true, docs);

        // Start a request that will never finish unless we rethrottle it
        request.setRequestsPerSecond(.000001f);  // Throttle "forever"
        request.source().setSize(1);             // Make sure we use multiple batches
        ListenableActionFuture<? extends BulkIndexByScrollResponse> responseListener = request.execute();

        TaskGroup taskGroupToRethrottle = findTaskToRethrottle(actionName, request.request().getSlices());
        TaskId taskToRethrottle = taskGroupToRethrottle.getTaskInfo().getTaskId();

        if (request.request().getSlices() == 1) {
            assertThat(taskGroupToRethrottle.getChildTasks(), empty());
        } else {
            // There should be a sane number of child tasks running
            assertThat(taskGroupToRethrottle.getChildTasks(),
                    hasSize(allOf(greaterThanOrEqualTo(1), lessThanOrEqualTo(request.request().getSlices()))));
            // Wait for all of the sub tasks to start (or finish, some might finish early, all that matters is that not all do)
            assertBusy(() -> {
                BulkByScrollTask.Status parent = (BulkByScrollTask.Status) client().admin().cluster().prepareGetTask(taskToRethrottle).get()
                        .getTask().getTask().getStatus();
                long finishedSubTasks = parent.getSliceStatuses().stream().filter(s -> s != null).count();
                ListTasksResponse list = client().admin().cluster().prepareListTasks().setParentTaskId(taskToRethrottle).get();
                list.rethrowFailures("subtasks");
                assertThat(finishedSubTasks + list.getTasks().size(), greaterThanOrEqualTo((long) request.request().getSlices()));
                assertThat(list.getTasks().size(), greaterThan(0));
            });
        }

        // Now rethrottle it so it'll finish
        float newRequestsPerSecond = randomBoolean() ? Float.POSITIVE_INFINITY : between(1, 1000) * 100000; // No throttle or "very fast"
        ListTasksResponse rethrottleResponse = rethrottle().setTaskId(taskToRethrottle).setRequestsPerSecond(newRequestsPerSecond).get();
        rethrottleResponse.rethrowFailures("Rethrottle");
        assertThat(rethrottleResponse.getTasks(), hasSize(1));
        BulkByScrollTask.Status status = (BulkByScrollTask.Status) rethrottleResponse.getTasks().get(0).getStatus();
        // Now check the resulting requests per second.
        if (request.request().getSlices() == 1) {
            // If there is a single slice it should match perfectly
            assertEquals(newRequestsPerSecond, status.getRequestsPerSecond(), Float.MIN_NORMAL);
        } else {
            /* Check that at least one slice was rethrottled. We won't always rethrottle all of them because they might have completed.
             * With multiple slices these numbers might not add up perfectly, thus the 0.0001f. */
            float expectedSliceRequestsPerSecond = newRequestsPerSecond == Float.POSITIVE_INFINITY ? Float.POSITIVE_INFINITY
                    : newRequestsPerSecond / request.request().getSlices();
            boolean oneSliceRethrottled = false;
            float totalRequestsPerSecond = 0;
            for (BulkByScrollTask.StatusOrException statusOrException : status.getSliceStatuses()) {
                if (statusOrException == null) {
                    /* The slice can be null here because it was completed but hadn't reported its success back to the task when the
                     * rethrottle request came through. */
                    continue;
                }
                assertNull(statusOrException.getException());
                BulkByScrollTask.Status slice = statusOrException.getStatus();
                if (slice.getTotal() > slice.getSuccessfullyProcessed()) {
                    assertEquals(expectedSliceRequestsPerSecond, slice.getRequestsPerSecond(), expectedSliceRequestsPerSecond * 0.0001f);
                }
                if (Math.abs(expectedSliceRequestsPerSecond - slice.getRequestsPerSecond()) <= expectedSliceRequestsPerSecond * 0.0001f
                        || expectedSliceRequestsPerSecond == slice.getRequestsPerSecond()) {
                    oneSliceRethrottled = true;
                }
                totalRequestsPerSecond += slice.getRequestsPerSecond();
            }
            assertTrue("At least one slice must be rethrottled", oneSliceRethrottled);

            /* Now assert that the parent request has the total requests per second. This is a much weaker assertion than that the parent
             * actually has the newRequestsPerSecond. For the most part it will. Sometimes it'll be greater because only unfinished requests
             * are rethrottled, the finished ones just keep whatever requests per second they had while they were running. But it might
             * also be less than newRequestsPerSecond because the newRequestsPerSecond is divided among running sub-requests and then the
             * requests are rethrottled. If one request finishes in between the division and the application of the new throttle then it
             * won't be rethrottled, thus only contributing its lower total. */ 
            assertEquals(totalRequestsPerSecond, status.getRequestsPerSecond(), totalRequestsPerSecond * 0.0001f);
        }

        // Now the response should come back quickly because we've rethrottled the request
        BulkIndexByScrollResponse response = responseListener.get();
        assertThat("Entire request completed in a single batch. This may invalidate the test as throttling is done between batches.",
                response.getBatches(), greaterThanOrEqualTo(request.request().getSlices()));
    }

    private TaskGroup findTaskToRethrottle(String actionName, int sliceCount) {
        long start = System.nanoTime();
        do {
            ListTasksResponse tasks = client().admin().cluster().prepareListTasks().setActions(actionName).setDetailed(true).get();
            tasks.rethrowFailures("Finding tasks to rethrottle");
            assertThat(tasks.getTaskGroups(), hasSize(lessThan(2)));
            if (0 == tasks.getTaskGroups().size()) {
                continue;
            }
            TaskGroup taskGroup = tasks.getTaskGroups().get(0);
            if (sliceCount != 1 && taskGroup.getChildTasks().size() == 0) {
                // If there are child tasks wait for at least one to start
                continue;
            }
            return taskGroup;
        } while (System.nanoTime() - start < TimeUnit.SECONDS.toNanos(10));
        throw new AssertionError("Couldn't find tasks to rethrottle. Here are the running tasks " +
                client().admin().cluster().prepareListTasks().get());
    }
}
