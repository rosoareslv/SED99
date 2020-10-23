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

package org.elasticsearch.search.profile;

import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * This class is the internal representation of a profiled Query, corresponding
 * to a single node in the query tree.  It is built after the query has finished executing
 * and is merely a structured representation, rather than the entity that collects the timing
 * profile (see InternalProfiler for that)
 *
 * Each InternalProfileResult has a List of InternalProfileResults, which will contain
 * "children" queries if applicable
 */
final class ProfileResult implements Writeable<ProfileResult>, ToXContent {

    private static final ParseField QUERY_TYPE = new ParseField("query_type");
    private static final ParseField LUCENE_DESCRIPTION = new ParseField("lucene");
    private static final ParseField NODE_TIME = new ParseField("time");
    private static final ParseField CHILDREN = new ParseField("children");
    private static final ParseField BREAKDOWN = new ParseField("breakdown");

    private final String queryType;
    private final String luceneDescription;
    private final Map<String, Long> timings;
    private final long nodeTime;
    private final List<ProfileResult> children;

    public ProfileResult(String queryType, String luceneDescription, Map<String, Long> timings, List<ProfileResult> children, long nodeTime) {
        this.queryType = queryType;
        this.luceneDescription = luceneDescription;
        this.timings = timings;
        this.children = children;
        this.nodeTime = nodeTime;
    }

    public ProfileResult(StreamInput in) throws IOException{
        this.queryType = in.readString();
        this.luceneDescription = in.readString();
        this.nodeTime = in.readLong();

        int timingsSize = in.readVInt();
        this.timings = new HashMap<>(timingsSize);
        for (int i = 0; i < timingsSize; ++i) {
            timings.put(in.readString(), in.readLong());
        }

        int size = in.readVInt();
        this.children = new ArrayList<>(size);

        for (int i = 0; i < size; i++) {
            children.add(new ProfileResult(in));
        }
    }

    /**
     * Retrieve the lucene description of this query (e.g. the "explain" text)
     */
    public String getLuceneDescription() {
        return luceneDescription;
    }

    /**
     * Retrieve the name of the query (e.g. "TermQuery")
     */
    public String getQueryName() {
        return queryType;
    }

    /**
     * Returns the timing breakdown for this particular query node
     */
    public Map<String, Long> getTimeBreakdown() {
        return Collections.unmodifiableMap(timings);
    }

    /**
     * Returns the total time (inclusive of children) for this query node.
     *
     * @return  elapsed time in nanoseconds
     */
    public long getTime() {
        return nodeTime;
    }

    /**
     * Returns a list of all profiled children queries
     */
    public List<ProfileResult> getProfiledChildren() {
        return Collections.unmodifiableList(children);
    }

    @Override
    public ProfileResult readFrom(StreamInput in) throws IOException {
        return new ProfileResult(in);
    }

    @Override
    public void writeTo(StreamOutput out) throws IOException {
        out.writeString(queryType);
        out.writeString(luceneDescription);
        out.writeLong(nodeTime);            // not Vlong because can be negative
        out.writeVInt(timings.size());
        for (Map.Entry<String, Long> entry : timings.entrySet()) {
            out.writeString(entry.getKey());
            out.writeLong(entry.getValue());
        }
        out.writeVInt(children.size());
        for (ProfileResult child : children) {
            child.writeTo(out);
        }
    }

    @Override
    public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
        builder = builder.startObject()
                .field(QUERY_TYPE.getPreferredName(), queryType)
                .field(LUCENE_DESCRIPTION.getPreferredName(), luceneDescription)
                .field(NODE_TIME.getPreferredName(), String.format(Locale.US, "%.10gms", (double)(getTime() / 1000000.0)))
                .field(BREAKDOWN.getPreferredName(), timings);

        if (!children.isEmpty()) {
            builder = builder.startArray(CHILDREN.getPreferredName());
            for (ProfileResult child : children) {
                builder = child.toXContent(builder, params);
            }
            builder = builder.endArray();
        }

        builder = builder.endObject();
        return builder;
    }

}
