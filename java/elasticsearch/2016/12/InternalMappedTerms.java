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

package org.elasticsearch.search.aggregations.bucket.terms;

import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.search.DocValueFormat;
import org.elasticsearch.search.aggregations.pipeline.PipelineAggregator;

import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;

/**
 * Common superclass for results of the terms aggregation on mapped fields.
 */
public abstract class InternalMappedTerms<A extends InternalTerms<A, B>, B extends InternalTerms.Bucket<B>> extends InternalTerms<A, B> {
    protected final DocValueFormat format;
    protected final int shardSize;
    protected final boolean showTermDocCountError;
    protected final long otherDocCount;
    protected final List<B> buckets;
    protected Map<String, B> bucketMap;

    protected long docCountError;

    protected InternalMappedTerms(String name, Terms.Order order, int requiredSize, long minDocCount,
            List<PipelineAggregator> pipelineAggregators, Map<String, Object> metaData, DocValueFormat format, int shardSize,
            boolean showTermDocCountError, long otherDocCount, List<B> buckets, long docCountError) {
        super(name, order, requiredSize, minDocCount, pipelineAggregators, metaData);
        this.format = format;
        this.shardSize = shardSize;
        this.showTermDocCountError = showTermDocCountError;
        this.otherDocCount = otherDocCount;
        this.docCountError = docCountError;
        this.buckets = buckets;
    }

    /**
     * Read from a stream.
     */
    protected InternalMappedTerms(StreamInput in, Bucket.Reader<B> bucketReader) throws IOException {
        super(in);
        docCountError = in.readZLong();
        format = in.readNamedWriteable(DocValueFormat.class);
        shardSize = readSize(in);
        showTermDocCountError = in.readBoolean();
        otherDocCount = in.readVLong();
        buckets = in.readList(stream -> bucketReader.read(stream, format, showTermDocCountError));
    }

    @Override
    protected final void writeTermTypeInfoTo(StreamOutput out) throws IOException {
        out.writeZLong(docCountError);
        out.writeNamedWriteable(format);
        writeSize(shardSize, out);
        out.writeBoolean(showTermDocCountError);
        out.writeVLong(otherDocCount);
        out.writeList(buckets);
    }

    @Override
    protected void setDocCountError(long docCountError) {
        this.docCountError = docCountError; 
    }

    @Override
    protected int getShardSize() {
        return shardSize;
    }

    @Override
    public long getDocCountError() {
        return docCountError;
    }

    @Override
    public long getSumOfOtherDocCounts() {
        return otherDocCount;
    }

    @Override
    public List<B> getBucketsInternal() {
        return buckets;
    }

    @Override
    public B getBucketByKey(String term) {
        if (bucketMap == null) {
            bucketMap = buckets.stream().collect(Collectors.toMap(Bucket::getKeyAsString, Function.identity()));
        }
        return bucketMap.get(term);
    }
}
