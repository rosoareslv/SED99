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
package org.elasticsearch.snapshots;

import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.Version;
import org.elasticsearch.action.ShardOperationFailedException;
import org.elasticsearch.common.ParseFieldMatcher;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.joda.FormatDateTimeFormatter;
import org.elasticsearch.common.joda.Joda;
import org.elasticsearch.common.xcontent.FromXContentBuilder;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.rest.RestStatus;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * Information about a snapshot
 */
public final class SnapshotInfo implements Comparable<SnapshotInfo>, ToXContent, FromXContentBuilder<SnapshotInfo>, Writeable {

    public static final SnapshotInfo PROTO = new SnapshotInfo(new SnapshotId("", ""), Collections.emptyList(), 0);
    public static final String CONTEXT_MODE_PARAM = "context_mode";
    public static final String CONTEXT_MODE_SNAPSHOT = "SNAPSHOT";
    private static final FormatDateTimeFormatter DATE_TIME_FORMATTER = Joda.forPattern("strictDateOptionalTime");
    private static final String SNAPSHOT = "snapshot";
    private static final String UUID = "uuid";
    private static final String INDICES = "indices";
    private static final String STATE = "state";
    private static final String REASON = "reason";
    private static final String START_TIME = "start_time";
    private static final String START_TIME_IN_MILLIS = "start_time_in_millis";
    private static final String END_TIME = "end_time";
    private static final String END_TIME_IN_MILLIS = "end_time_in_millis";
    private static final String DURATION = "duration";
    private static final String DURATION_IN_MILLIS = "duration_in_millis";
    private static final String FAILURES = "failures";
    private static final String SHARDS = "shards";
    private static final String TOTAL = "total";
    private static final String FAILED = "failed";
    private static final String SUCCESSFUL = "successful";
    private static final String VERSION_ID = "version_id";
    private static final String VERSION = "version";
    private static final String NAME = "name";
    private static final String TOTAL_SHARDS = "total_shards";
    private static final String SUCCESSFUL_SHARDS = "successful_shards";

    private final SnapshotId snapshotId;

    private final SnapshotState state;

    private final String reason;

    private final List<String> indices;

    private final long startTime;

    private final long endTime;

    private final int totalShards;

    private final int successfulShards;

    private final Version version;

    private final List<SnapshotShardFailure> shardFailures;

    public SnapshotInfo(SnapshotId snapshotId, List<String> indices, long startTime) {
        this(snapshotId, indices, SnapshotState.IN_PROGRESS, null, Version.CURRENT, startTime, 0L, 0, 0, Collections.emptyList());
    }

    public SnapshotInfo(SnapshotId snapshotId, List<String> indices, long startTime, String reason, long endTime,
                        int totalShards, List<SnapshotShardFailure> shardFailures) {
        this(snapshotId, indices, snapshotState(reason, shardFailures), reason, Version.CURRENT,
             startTime, endTime, totalShards, totalShards - shardFailures.size(), shardFailures);
    }

    private SnapshotInfo(SnapshotId snapshotId, List<String> indices, SnapshotState state, String reason, Version version,
                         long startTime, long endTime, int totalShards, int successfulShards, List<SnapshotShardFailure> shardFailures) {
        this.snapshotId = Objects.requireNonNull(snapshotId);
        this.indices = Objects.requireNonNull(indices);
        this.state = Objects.requireNonNull(state);
        this.reason = reason;
        this.version = version;
        this.startTime = startTime;
        this.endTime = endTime;
        this.totalShards = totalShards;
        this.successfulShards = successfulShards;
        this.shardFailures = Objects.requireNonNull(shardFailures);
    }

    /**
     * Constructs snapshot information from stream input
     */
    public SnapshotInfo(final StreamInput in) throws IOException {
        snapshotId = new SnapshotId(in);
        int size = in.readVInt();
        List<String> indicesListBuilder = new ArrayList<>();
        for (int i = 0; i < size; i++) {
            indicesListBuilder.add(in.readString());
        }
        indices = Collections.unmodifiableList(indicesListBuilder);
        state = SnapshotState.fromValue(in.readByte());
        reason = in.readOptionalString();
        startTime = in.readVLong();
        endTime = in.readVLong();
        totalShards = in.readVInt();
        successfulShards = in.readVInt();
        size = in.readVInt();
        if (size > 0) {
            List<SnapshotShardFailure> failureBuilder = new ArrayList<>();
            for (int i = 0; i < size; i++) {
                failureBuilder.add(SnapshotShardFailure.readSnapshotShardFailure(in));
            }
            shardFailures = Collections.unmodifiableList(failureBuilder);
        } else {
            shardFailures = Collections.emptyList();
        }
        version = Version.readVersion(in);
    }

    /**
     * Returns snapshot id
     *
     * @return snapshot id
     */
    public SnapshotId snapshotId() {
        return snapshotId;
    }

    /**
     * Returns snapshot state
     *
     * @return snapshot state
     */
    public SnapshotState state() {
        return state;
    }

    /**
     * Returns snapshot failure reason
     *
     * @return snapshot failure reason
     */
    public String reason() {
        return reason;
    }

    /**
     * Returns indices that were included into this snapshot
     *
     * @return list of indices
     */
    public List<String> indices() {
        return indices;
    }

    /**
     * Returns time when snapshot started
     *
     * @return snapshot start time
     */
    public long startTime() {
        return startTime;
    }

    /**
     * Returns time when snapshot ended
     * <p>
     * Can be 0L if snapshot is still running
     *
     * @return snapshot end time
     */
    public long endTime() {
        return endTime;
    }

    /**
     * Returns total number of shards that were snapshotted
     *
     * @return number of shards
     */
    public int totalShards() {
        return totalShards;
    }

    /**
     * Number of failed shards
     *
     * @return number of failed shards
     */
    public int failedShards() {
        return totalShards - successfulShards;
    }

    /**
     * Returns total number of shards that were successfully snapshotted
     *
     * @return number of successful shards
     */
    public int successfulShards() {
        return successfulShards;
    }

    /**
     * Returns shard failures
     *
     * @return shard failures
     */
    public List<SnapshotShardFailure> shardFailures() {
        return shardFailures;
    }

    /**
     * Returns the version of elasticsearch that the snapshot was created with
     *
     * @return version of elasticsearch that the snapshot was created with
     */
    public Version version() {
        return version;
    }

    /**
     * Compares two snapshots by their start time
     *
     * @param o other snapshot
     * @return the value {@code 0} if snapshots were created at the same time;
     * a value less than {@code 0} if this snapshot was created before snapshot {@code o}; and
     * a value greater than {@code 0} if this snapshot was created after snapshot {@code o};
     */
    @Override
    public int compareTo(final SnapshotInfo o) {
        return Long.compare(startTime, o.startTime);
    }

    @Override
    public boolean equals(final Object o) {
        if (this == o) {
            return true;
        }
        if (o == null || getClass() != o.getClass()) {
            return false;
        }

        final SnapshotInfo that = (SnapshotInfo) o;
        return startTime == that.startTime && snapshotId.equals(that.snapshotId);
    }

    @Override
    public int hashCode() {
        int result = snapshotId.hashCode();
        result = 31 * result + Long.hashCode(startTime);
        return result;
    }

    @Override
    public String toString() {
        return "SnapshotInfo[snapshotId=" + snapshotId + ", state=" + state + ", indices=" + indices + "]";
    }

    /**
     * Returns snapshot REST status
     */
    public RestStatus status() {
        if (state == SnapshotState.FAILED) {
            return RestStatus.INTERNAL_SERVER_ERROR;
        }
        if (shardFailures.size() == 0) {
            return RestStatus.OK;
        }
        return RestStatus.status(successfulShards, totalShards,
                                 shardFailures.toArray(new ShardOperationFailedException[shardFailures.size()]));
    }

    @Override
    public XContentBuilder toXContent(final XContentBuilder builder, final Params params) throws IOException {
        // write snapshot info to repository snapshot blob format
        if (CONTEXT_MODE_SNAPSHOT.equals(params.param(CONTEXT_MODE_PARAM))) {
            return toXContentSnapshot(builder, params);
        }

        // write snapshot info for the API and any other situations
        builder.startObject();
        builder.field(SNAPSHOT, snapshotId.getName());
        builder.field(UUID, snapshotId.getUUID());
        builder.field(VERSION_ID, version.id);
        builder.field(VERSION, version.toString());
        builder.startArray(INDICES);
        for (String index : indices) {
            builder.value(index);
        }
        builder.endArray();
        builder.field(STATE, state);
        if (reason != null) {
            builder.field(REASON, reason);
        }
        if (startTime != 0) {
            builder.field(START_TIME, DATE_TIME_FORMATTER.printer().print(startTime));
            builder.field(START_TIME_IN_MILLIS, startTime);
        }
        if (endTime != 0) {
            builder.field(END_TIME, DATE_TIME_FORMATTER.printer().print(endTime));
            builder.field(END_TIME_IN_MILLIS, endTime);
            builder.timeValueField(DURATION_IN_MILLIS, DURATION, endTime - startTime);
        }
        builder.startArray(FAILURES);
        for (SnapshotShardFailure shardFailure : shardFailures) {
            builder.startObject();
            shardFailure.toXContent(builder, params);
            builder.endObject();
        }
        builder.endArray();
        builder.startObject(SHARDS);
        builder.field(TOTAL, totalShards);
        builder.field(FAILED, failedShards());
        builder.field(SUCCESSFUL, successfulShards);
        builder.endObject();
        builder.endObject();
        return builder;
    }

    private XContentBuilder toXContentSnapshot(final XContentBuilder builder, final ToXContent.Params params) throws IOException {
        builder.startObject(SNAPSHOT);
        builder.field(NAME, snapshotId.getName());
        builder.field(UUID, snapshotId.getUUID());
        builder.field(VERSION_ID, version.id);
        builder.startArray(INDICES);
        for (String index : indices) {
            builder.value(index);
        }
        builder.endArray();
        builder.field(STATE, state);
        if (reason != null) {
            builder.field(REASON, reason);
        }
        builder.field(START_TIME, startTime);
        builder.field(END_TIME, endTime);
        builder.field(TOTAL_SHARDS, totalShards);
        builder.field(SUCCESSFUL_SHARDS, successfulShards);
        builder.startArray(FAILURES);
        for (SnapshotShardFailure shardFailure : shardFailures) {
            builder.startObject();
            shardFailure.toXContent(builder, params);
            builder.endObject();
        }
        builder.endArray();
        builder.endObject();
        return builder;
    }

    @Override
    public SnapshotInfo fromXContent(final XContentParser parser, final ParseFieldMatcher matcher) throws IOException {
        return fromXContent(parser);
    }

    /**
     * This method creates a SnapshotInfo from internal x-content.  It does not
     * handle x-content written with the external version as external x-content
     * is only for display purposes and does not need to be parsed.
     */
    public static SnapshotInfo fromXContent(final XContentParser parser) throws IOException {
        String name = null;
        String uuid = null;
        Version version = Version.CURRENT;
        SnapshotState state = SnapshotState.IN_PROGRESS;
        String reason = null;
        List<String> indices = Collections.emptyList();
        long startTime = 0;
        long endTime = 0;
        int totalShards = 0;
        int successfulShards = 0;
        List<SnapshotShardFailure> shardFailures = Collections.emptyList();
        if (parser.currentToken() == null) { // fresh parser? move to the first token
            parser.nextToken();
        }
        if (parser.currentToken() == XContentParser.Token.START_OBJECT) {  // on a start object move to next token
            parser.nextToken();
        }
        XContentParser.Token token;
        if ((token = parser.nextToken()) == XContentParser.Token.START_OBJECT) {
            String currentFieldName = parser.currentName();
            if (SNAPSHOT.equals(currentFieldName)) {
                while ((token = parser.nextToken()) != XContentParser.Token.END_OBJECT) {
                    if (token == XContentParser.Token.FIELD_NAME) {
                        currentFieldName = parser.currentName();
                        token = parser.nextToken();
                        if (token.isValue()) {
                            if (NAME.equals(currentFieldName)) {
                                name = parser.text();
                            } else if (UUID.equals(currentFieldName)) {
                                uuid = parser.text();
                            } else if (STATE.equals(currentFieldName)) {
                                state = SnapshotState.valueOf(parser.text());
                            } else if (REASON.equals(currentFieldName)) {
                                reason = parser.text();
                            } else if (START_TIME.equals(currentFieldName)) {
                                startTime = parser.longValue();
                            } else if (END_TIME.equals(currentFieldName)) {
                                endTime = parser.longValue();
                            } else if (TOTAL_SHARDS.equals(currentFieldName)) {
                                totalShards = parser.intValue();
                            } else if (SUCCESSFUL_SHARDS.equals(currentFieldName)) {
                                successfulShards = parser.intValue();
                            } else if (VERSION_ID.equals(currentFieldName)) {
                                version = Version.fromId(parser.intValue());
                            }
                        } else if (token == XContentParser.Token.START_ARRAY) {
                            if (INDICES.equals(currentFieldName)) {
                                ArrayList<String> indicesArray = new ArrayList<>();
                                while (parser.nextToken() != XContentParser.Token.END_ARRAY) {
                                    indicesArray.add(parser.text());
                                }
                                indices = Collections.unmodifiableList(indicesArray);
                            } else if (FAILURES.equals(currentFieldName)) {
                                ArrayList<SnapshotShardFailure> shardFailureArrayList = new ArrayList<>();
                                while (parser.nextToken() != XContentParser.Token.END_ARRAY) {
                                    shardFailureArrayList.add(SnapshotShardFailure.fromXContent(parser));
                                }
                                shardFailures = Collections.unmodifiableList(shardFailureArrayList);
                            } else {
                                // It was probably created by newer version - ignoring
                                parser.skipChildren();
                            }
                        } else if (token == XContentParser.Token.START_OBJECT) {
                            // It was probably created by newer version - ignoring
                            parser.skipChildren();
                        }
                    }
                }
            }
        } else {
            throw new ElasticsearchParseException("unexpected token  [" + token + "]");
        }
        if (uuid == null) {
            // the old format where there wasn't a UUID
            uuid = name;
        }
        return new SnapshotInfo(new SnapshotId(name, uuid),
                                indices,
                                state,
                                reason,
                                version,
                                startTime,
                                endTime,
                                totalShards,
                                successfulShards,
                                shardFailures);
    }

    @Override
    public void writeTo(final StreamOutput out) throws IOException {
        snapshotId.writeTo(out);
        out.writeVInt(indices.size());
        for (String index : indices) {
            out.writeString(index);
        }
        out.writeByte(state.value());
        out.writeOptionalString(reason);
        out.writeVLong(startTime);
        out.writeVLong(endTime);
        out.writeVInt(totalShards);
        out.writeVInt(successfulShards);
        out.writeVInt(shardFailures.size());
        for (SnapshotShardFailure failure : shardFailures) {
            failure.writeTo(out);
        }
        Version.writeVersion(version, out);
    }

    private static SnapshotState snapshotState(final String reason, final List<SnapshotShardFailure> shardFailures) {
        if (reason == null) {
            if (shardFailures.isEmpty()) {
                return SnapshotState.SUCCESS;
            } else {
                return SnapshotState.PARTIAL;
            }
        } else {
            return SnapshotState.FAILED;
        }
    }

}
