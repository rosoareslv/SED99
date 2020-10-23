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

import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * A container class to hold the profile results for a single shard in the request.
 * Contains a list of query profiles, a collector tree and a total rewrite tree.
 */
public final class ProfileShardResult implements Writeable<ProfileShardResult>, ToXContent {

    private final List<ProfileResult> profileResults;

    private final CollectorResult profileCollector;

    private final long rewriteTime;

    public ProfileShardResult(List<ProfileResult> profileResults, long rewriteTime,
                              CollectorResult profileCollector) {
        assert(profileCollector != null);
        this.profileResults = profileResults;
        this.profileCollector = profileCollector;
        this.rewriteTime = rewriteTime;
    }

    public ProfileShardResult(StreamInput in) throws IOException {
        int profileSize = in.readVInt();
        profileResults = new ArrayList<>(profileSize);
        for (int j = 0; j < profileSize; j++) {
            profileResults.add(new ProfileResult(in));
        }

        profileCollector = new CollectorResult(in);
        rewriteTime = in.readLong();
    }

    public List<ProfileResult> getQueryResults() {
        return Collections.unmodifiableList(profileResults);
    }

    public long getRewriteTime() {
        return rewriteTime;
    }

    public CollectorResult getCollectorResult() {
        return profileCollector;
    }

    @Override
    public XContentBuilder toXContent(XContentBuilder builder, Params params) throws IOException {
        builder.startArray("query");
        for (ProfileResult p : profileResults) {
            p.toXContent(builder, params);
        }
        builder.endArray();
        builder.field("rewrite_time", rewriteTime);
        builder.startArray("collector");
        profileCollector.toXContent(builder, params);
        builder.endArray();
        return builder;
    }

    @Override
    public ProfileShardResult readFrom(StreamInput in) throws IOException {
        return new ProfileShardResult(in);
    }

    @Override
    public void writeTo(StreamOutput out) throws IOException {
        out.writeVInt(profileResults.size());
        for (ProfileResult p : profileResults) {
            p.writeTo(out);
        }
        profileCollector.writeTo(out);
        out.writeLong(rewriteTime);
    }

}
