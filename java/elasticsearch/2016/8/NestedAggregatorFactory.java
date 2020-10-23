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

package org.elasticsearch.search.aggregations.bucket.nested;

import org.elasticsearch.index.mapper.ObjectMapper;
import org.elasticsearch.search.aggregations.AggregationExecutionException;
import org.elasticsearch.search.aggregations.Aggregator;
import org.elasticsearch.search.aggregations.AggregatorFactories;
import org.elasticsearch.search.aggregations.AggregatorFactory;
import org.elasticsearch.search.aggregations.InternalAggregation;
import org.elasticsearch.search.aggregations.NonCollectingAggregator;
import org.elasticsearch.search.aggregations.InternalAggregation.Type;
import org.elasticsearch.search.aggregations.pipeline.PipelineAggregator;
import org.elasticsearch.search.aggregations.support.AggregationContext;

import java.io.IOException;
import java.util.List;
import java.util.Map;

public class NestedAggregatorFactory extends AggregatorFactory<NestedAggregatorFactory> {

    private final ObjectMapper parentObjectMapper;
    private final ObjectMapper childObjectMapper;

    public NestedAggregatorFactory(String name, Type type, ObjectMapper parentObjectMapper, ObjectMapper childObjectMapper,
                                   AggregationContext context, AggregatorFactory<?> parent, AggregatorFactories.Builder subFactories,
                                   Map<String, Object> metaData) throws IOException {
        super(name, type, context, parent, subFactories, metaData);
        this.parentObjectMapper = parentObjectMapper;
        this.childObjectMapper = childObjectMapper;
    }

    @Override
    public Aggregator createInternal(Aggregator parent, boolean collectsFromSingleBucket, List<PipelineAggregator> pipelineAggregators,
            Map<String, Object> metaData) throws IOException {
        if (collectsFromSingleBucket == false) {
            return asMultiBucketAggregator(this, context, parent);
        }
        if (childObjectMapper == null) {
            return new Unmapped(name, context, parent, pipelineAggregators, metaData);
        }
        return new NestedAggregator(name, factories, parentObjectMapper, childObjectMapper, context, parent, pipelineAggregators, metaData);
    }

    private static final class Unmapped extends NonCollectingAggregator {

        public Unmapped(String name, AggregationContext context, Aggregator parent, List<PipelineAggregator> pipelineAggregators,
                Map<String, Object> metaData) throws IOException {
            super(name, context, parent, pipelineAggregators, metaData);
        }

        @Override
        public InternalAggregation buildEmptyAggregation() {
            return new InternalNested(name, 0, buildEmptySubAggregations(), pipelineAggregators(), metaData());
        }
    }

}
