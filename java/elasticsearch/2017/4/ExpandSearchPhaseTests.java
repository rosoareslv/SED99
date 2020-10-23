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

package org.elasticsearch.action.search;

import org.elasticsearch.action.ActionListener;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.text.Text;
import org.elasticsearch.index.query.BoolQueryBuilder;
import org.elasticsearch.index.query.InnerHitBuilder;
import org.elasticsearch.index.query.QueryBuilder;
import org.elasticsearch.index.query.QueryBuilders;
import org.elasticsearch.search.SearchHit;
import org.elasticsearch.search.SearchHitField;
import org.elasticsearch.search.SearchHits;
import org.elasticsearch.search.builder.SearchSourceBuilder;
import org.elasticsearch.search.collapse.CollapseBuilder;
import org.elasticsearch.search.internal.InternalSearchResponse;
import org.elasticsearch.test.ESTestCase;
import org.hamcrest.Matchers;

import java.io.IOException;
import java.util.Collections;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public class ExpandSearchPhaseTests extends ESTestCase {

    public void testCollapseSingleHit() throws IOException {
        final int iters = randomIntBetween(5, 10);
        for (int i = 0; i < iters; i++) {
            SearchHits collapsedHits = new SearchHits(new SearchHit[]{new SearchHit(2, "ID", new Text("type"),
                Collections.emptyMap()), new SearchHit(3, "ID", new Text("type"),
                Collections.emptyMap())}, 1, 1.0F);
            AtomicBoolean executedMultiSearch = new AtomicBoolean(false);
            QueryBuilder originalQuery = randomBoolean() ? null : QueryBuilders.termQuery("foo", "bar");

            MockSearchPhaseContext mockSearchPhaseContext = new MockSearchPhaseContext(1);
            String collapseValue = randomBoolean() ? null : "boom";
            mockSearchPhaseContext.getRequest().source(new SearchSourceBuilder()
                .collapse(new CollapseBuilder("someField").setInnerHits(new InnerHitBuilder().setName("foobarbaz"))));
            mockSearchPhaseContext.getRequest().source().query(originalQuery);
            mockSearchPhaseContext.searchTransport = new SearchTransportService(
                Settings.builder().put("search.remote.connect", false).build(), null, null) {

                @Override
                void sendExecuteMultiSearch(MultiSearchRequest request, SearchTask task, ActionListener<MultiSearchResponse> listener) {
                    assertTrue(executedMultiSearch.compareAndSet(false, true));
                    assertEquals(1, request.requests().size());
                    SearchRequest searchRequest = request.requests().get(0);
                    assertTrue(searchRequest.source().query() instanceof BoolQueryBuilder);
                    BoolQueryBuilder groupBuilder = (BoolQueryBuilder) searchRequest.source().query();
                    if (collapseValue == null) {
                        assertThat(groupBuilder.mustNot(), Matchers.contains(QueryBuilders.existsQuery("someField")));
                    } else {
                        assertThat(groupBuilder.filter(), Matchers.contains(QueryBuilders.matchQuery("someField", "boom")));
                    }
                    if (originalQuery != null) {
                        assertThat(groupBuilder.must(), Matchers.contains(QueryBuilders.termQuery("foo", "bar")));
                    }
                    assertArrayEquals(mockSearchPhaseContext.getRequest().indices(), searchRequest.indices());
                    assertArrayEquals(mockSearchPhaseContext.getRequest().types(), searchRequest.types());


                    InternalSearchResponse internalSearchResponse = new InternalSearchResponse(collapsedHits,
                        null, null, null, false, null, 1);
                    SearchResponse response = mockSearchPhaseContext.buildSearchResponse(internalSearchResponse, null);
                    listener.onResponse(new MultiSearchResponse(new MultiSearchResponse.Item[]{
                        new MultiSearchResponse.Item(response, null)
                    }));

                }
            };

            SearchHits hits = new SearchHits(new SearchHit[]{new SearchHit(1, "ID", new Text("type"),
                Collections.singletonMap("someField", new SearchHitField("someField", Collections.singletonList(collapseValue))))},
                1, 1.0F);
            InternalSearchResponse internalSearchResponse = new InternalSearchResponse(hits, null, null, null, false, null, 1);
            SearchResponse response = mockSearchPhaseContext.buildSearchResponse(internalSearchResponse, null);
            AtomicReference<SearchResponse> reference = new AtomicReference<>();
            ExpandSearchPhase phase = new ExpandSearchPhase(mockSearchPhaseContext, response, r ->
                new SearchPhase("test") {
                    @Override
                    public void run() throws IOException {
                        reference.set(r);
                    }
                }
            );

            phase.run();
            mockSearchPhaseContext.assertNoFailure();
            assertNotNull(reference.get());
            SearchResponse theResponse = reference.get();
            assertSame(theResponse, response);
            assertEquals(1, theResponse.getHits().getHits()[0].getInnerHits().size());
            assertSame(theResponse.getHits().getHits()[0].getInnerHits().get("foobarbaz"), collapsedHits);
            assertTrue(executedMultiSearch.get());
            assertEquals(1, mockSearchPhaseContext.phasesExecuted.get());
        }
    }

    public void testFailOneItemFailsEntirePhase() throws IOException {
        AtomicBoolean executedMultiSearch = new AtomicBoolean(false);

        SearchHits collapsedHits = new SearchHits(new SearchHit[]{new SearchHit(2, "ID", new Text("type"),
            Collections.emptyMap()), new SearchHit(3, "ID", new Text("type"),
            Collections.emptyMap())}, 1, 1.0F);
        MockSearchPhaseContext mockSearchPhaseContext = new MockSearchPhaseContext(1);
        String collapseValue = randomBoolean() ? null : "boom";
        mockSearchPhaseContext.getRequest().source(new SearchSourceBuilder()
            .collapse(new CollapseBuilder("someField").setInnerHits(new InnerHitBuilder().setName("foobarbaz"))));
        mockSearchPhaseContext.searchTransport = new SearchTransportService(
            Settings.builder().put("search.remote.connect", false).build(), null, null) {

            @Override
            void sendExecuteMultiSearch(MultiSearchRequest request, SearchTask task, ActionListener<MultiSearchResponse> listener) {
                assertTrue(executedMultiSearch.compareAndSet(false, true));
                InternalSearchResponse internalSearchResponse = new InternalSearchResponse(collapsedHits,
                    null, null, null, false, null, 1);
                SearchResponse response = mockSearchPhaseContext.buildSearchResponse(internalSearchResponse, null);
                listener.onResponse(new MultiSearchResponse(new MultiSearchResponse.Item[]{
                    new MultiSearchResponse.Item(null, new RuntimeException("boom")),
                    new MultiSearchResponse.Item(response, null)
                }));
            }
        };

        SearchHits hits = new SearchHits(new SearchHit[]{new SearchHit(1, "ID", new Text("type"),
            Collections.singletonMap("someField", new SearchHitField("someField", Collections.singletonList(collapseValue)))),
            new SearchHit(2, "ID2", new Text("type"),
                Collections.singletonMap("someField", new SearchHitField("someField", Collections.singletonList(collapseValue))))}, 1,
            1.0F);
        InternalSearchResponse internalSearchResponse = new InternalSearchResponse(hits, null, null, null, false, null, 1);
        SearchResponse response = mockSearchPhaseContext.buildSearchResponse(internalSearchResponse, null);
        AtomicReference<SearchResponse> reference = new AtomicReference<>();
        ExpandSearchPhase phase = new ExpandSearchPhase(mockSearchPhaseContext, response, r ->
            new SearchPhase("test") {
                @Override
                public void run() throws IOException {
                    reference.set(r);
                }
            }
        );
        phase.run();
        assertThat(mockSearchPhaseContext.phaseFailure.get(), Matchers.instanceOf(RuntimeException.class));
        assertEquals("boom", mockSearchPhaseContext.phaseFailure.get().getMessage());
        assertNotNull(mockSearchPhaseContext.phaseFailure.get());
        assertNull(reference.get());
        assertEquals(0, mockSearchPhaseContext.phasesExecuted.get());
    }

    public void testSkipPhase() throws IOException {
        MockSearchPhaseContext mockSearchPhaseContext = new MockSearchPhaseContext(1);
        mockSearchPhaseContext.searchTransport = new SearchTransportService(
            Settings.builder().put("search.remote.connect", false).build(), null, null) {

            @Override
            void sendExecuteMultiSearch(MultiSearchRequest request, SearchTask task, ActionListener<MultiSearchResponse> listener) {
              fail("no collapsing here");
            }
        };

        SearchHits hits = new SearchHits(new SearchHit[]{new SearchHit(1, "ID", new Text("type"),
            Collections.singletonMap("someField", new SearchHitField("someField", Collections.singletonList(null)))),
            new SearchHit(2, "ID2", new Text("type"),
                Collections.singletonMap("someField", new SearchHitField("someField", Collections.singletonList(null))))}, 1, 1.0F);
        InternalSearchResponse internalSearchResponse = new InternalSearchResponse(hits, null, null, null, false, null, 1);
        SearchResponse response = mockSearchPhaseContext.buildSearchResponse(internalSearchResponse, null);
        AtomicReference<SearchResponse> reference = new AtomicReference<>();
        ExpandSearchPhase phase = new ExpandSearchPhase(mockSearchPhaseContext, response, r ->
            new SearchPhase("test") {
                @Override
                public void run() throws IOException {
                    reference.set(r);
                }
            }
        );
        phase.run();
        mockSearchPhaseContext.assertNoFailure();
        assertNotNull(reference.get());
        assertEquals(1, mockSearchPhaseContext.phasesExecuted.get());
    }
}
