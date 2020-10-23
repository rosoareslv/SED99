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


package org.elasticsearch.search.aggregations.bucket.significant.heuristics;


import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.common.ParseFieldMatcher;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.logging.ESLoggerFactory;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.index.query.QueryShardException;
import org.elasticsearch.script.ExecutableScript;
import org.elasticsearch.script.Script;
import org.elasticsearch.script.Script.ScriptField;
import org.elasticsearch.script.ScriptContext;
import org.elasticsearch.script.ScriptService;
import org.elasticsearch.search.aggregations.InternalAggregation;
import org.elasticsearch.search.internal.SearchContext;

import java.io.IOException;
import java.util.Collections;
import java.util.Objects;

public class ScriptHeuristic extends SignificanceHeuristic {
    public static final String NAME = "script_heuristic";

    private final LongAccessor subsetSizeHolder;
    private final LongAccessor supersetSizeHolder;
    private final LongAccessor subsetDfHolder;
    private final LongAccessor supersetDfHolder;
    private final Script script;
    ExecutableScript searchScript = null;

    public ScriptHeuristic(Script script) {
        subsetSizeHolder = new LongAccessor();
        supersetSizeHolder = new LongAccessor();
        subsetDfHolder = new LongAccessor();
        supersetDfHolder = new LongAccessor();
        this.script = script;
    }

    /**
     * Read from a stream.
     */
    public ScriptHeuristic(StreamInput in) throws IOException {
        this(new Script(in));
    }

    @Override
    public void writeTo(StreamOutput out) throws IOException {
        script.writeTo(out);
    }

    @Override
    public void initialize(InternalAggregation.ReduceContext context) {
        initialize(context.scriptService());
    }

    @Override
    public void initialize(SearchContext context) {
        initialize(context.scriptService());
    }

    public void initialize(ScriptService scriptService) {
        searchScript = scriptService.executable(script, ScriptContext.Standard.AGGS, Collections.emptyMap());
        searchScript.setNextVar("_subset_freq", subsetDfHolder);
        searchScript.setNextVar("_subset_size", subsetSizeHolder);
        searchScript.setNextVar("_superset_freq", supersetDfHolder);
        searchScript.setNextVar("_superset_size", supersetSizeHolder);
    }

    /**
     * Calculates score with a script
     *
     * @param subsetFreq   The frequency of the term in the selected sample
     * @param subsetSize   The size of the selected sample (typically number of docs)
     * @param supersetFreq The frequency of the term in the superset from which the sample was taken
     * @param supersetSize The size of the superset from which the sample was taken  (typically number of docs)
     * @return a "significance" score
     */
    @Override
    public double getScore(long subsetFreq, long subsetSize, long supersetFreq, long supersetSize) {
        if (searchScript == null) {
            //In tests, wehn calling assertSearchResponse(..) the response is streamed one additional time with an arbitrary version, see assertVersionSerializable(..).
            // Now, for version before 1.5.0 the score is computed after streaming the response but for scripts the script does not exists yet.
            // assertSearchResponse() might therefore fail although there is no problem.
            // This should be replaced by an exception in 2.0.
            ESLoggerFactory.getLogger("script heuristic").warn("cannot compute score - script has not been initialized yet.");
            return 0;
        }
        subsetSizeHolder.value = subsetSize;
        supersetSizeHolder.value = supersetSize;
        subsetDfHolder.value = subsetFreq;
        supersetDfHolder.value = supersetFreq;
        return ((Number) searchScript.run()).doubleValue();
    }

    @Override
    public String getWriteableName() {
        return NAME;
    }

    @Override
    public XContentBuilder toXContent(XContentBuilder builder, Params builderParams) throws IOException {
        builder.startObject(NAME);
        builder.field(ScriptField.SCRIPT.getPreferredName());
        script.toXContent(builder, builderParams);
        builder.endObject();
        return builder;
    }

    @Override
    public int hashCode() {
        return Objects.hash(script);
    }

    @Override
    public boolean equals(Object obj) {
        if (obj == null) {
            return false;
        }
        if (getClass() != obj.getClass()) {
            return false;
        }
        ScriptHeuristic other = (ScriptHeuristic) obj;
        return Objects.equals(script, other.script);
    }

    public static SignificanceHeuristic parse(XContentParser parser, ParseFieldMatcher parseFieldMatcher)
            throws IOException, QueryShardException {
        String heuristicName = parser.currentName();
        Script script = null;
        XContentParser.Token token;
        String currentFieldName = null;
        while ((token = parser.nextToken()) != XContentParser.Token.END_OBJECT) {
            if (token.equals(XContentParser.Token.FIELD_NAME)) {
                currentFieldName = parser.currentName();
            } else {
                if (parseFieldMatcher.match(currentFieldName, ScriptField.SCRIPT)) {
                    script = Script.parse(parser, parseFieldMatcher);
                } else {
                    throw new ElasticsearchParseException("failed to parse [{}] significance heuristic. unknown object [{}]", heuristicName, currentFieldName);
                }
            }
        }

        if (script == null) {
            throw new ElasticsearchParseException("failed to parse [{}] significance heuristic. no script found in script_heuristic", heuristicName);
        }
        return new ScriptHeuristic(script);
    }

    public static class ScriptHeuristicBuilder implements SignificanceHeuristicBuilder {

        private Script script = null;

        public ScriptHeuristicBuilder setScript(Script script) {
            this.script = script;
            return this;
        }

        @Override
        public XContentBuilder toXContent(XContentBuilder builder, Params builderParams) throws IOException {
            builder.startObject(NAME);
            builder.field(ScriptField.SCRIPT.getPreferredName());
            script.toXContent(builder, builderParams);
            builder.endObject();
            return builder;
        }

    }

    public final class LongAccessor extends Number {
        public long value;
        @Override
        public int intValue() {
            return (int)value;
        }
        @Override
        public long longValue() {
            return value;
        }

        @Override
        public float floatValue() {
            return value;
        }

        @Override
        public double doubleValue() {
            return value;
        }

        @Override
        public String toString() {
            return Long.toString(value);
        }
    }
}

