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

package org.elasticsearch.index.query;

import org.apache.lucene.index.LeafReaderContext;
import org.apache.lucene.search.ConstantScoreScorer;
import org.apache.lucene.search.ConstantScoreWeight;
import org.apache.lucene.search.DocIdSetIterator;
import org.apache.lucene.search.IndexSearcher;
import org.apache.lucene.search.Query;
import org.apache.lucene.search.Scorer;
import org.apache.lucene.search.TwoPhaseIterator;
import org.apache.lucene.search.Weight;
import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.ParsingException;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.script.LeafSearchScript;
import org.elasticsearch.script.Script;
import org.elasticsearch.script.ScriptContext;
import org.elasticsearch.script.SearchScript;

import java.io.IOException;
import java.util.Objects;

public class ScriptQueryBuilder extends AbstractQueryBuilder<ScriptQueryBuilder> {
    public static final String NAME = "script";

    private static final ParseField PARAMS_FIELD = new ParseField("params");

    private final Script script;

    public ScriptQueryBuilder(Script script) {
        if (script == null) {
            throw new IllegalArgumentException("script cannot be null");
        }
        this.script = script;
    }

    /**
     * Read from a stream.
     */
    public ScriptQueryBuilder(StreamInput in) throws IOException {
        super(in);
        script = new Script(in);
    }

    @Override
    protected void doWriteTo(StreamOutput out) throws IOException {
        script.writeTo(out);
    }

    public Script script() {
        return this.script;
    }

    @Override
    public String getWriteableName() {
        return NAME;
    }

    @Override
    protected void doXContent(XContentBuilder builder, Params builderParams) throws IOException {
        builder.startObject(NAME);
        builder.field(Script.SCRIPT_PARSE_FIELD.getPreferredName(), script);
        printBoostAndQueryName(builder);
        builder.endObject();
    }

    public static ScriptQueryBuilder fromXContent(QueryParseContext parseContext) throws IOException {
        XContentParser parser = parseContext.parser();
        // also, when caching, since its isCacheable is false, will result in loading all bit set...
        Script script = null;

        float boost = AbstractQueryBuilder.DEFAULT_BOOST;
        String queryName = null;

        XContentParser.Token token;
        String currentFieldName = null;
        while ((token = parser.nextToken()) != XContentParser.Token.END_OBJECT) {
            if (token == XContentParser.Token.FIELD_NAME) {
                currentFieldName = parser.currentName();
            } else if (parseContext.isDeprecatedSetting(currentFieldName)) {
                // skip
            } else if (token == XContentParser.Token.START_OBJECT) {
                if (Script.SCRIPT_PARSE_FIELD.match(currentFieldName)) {
                    script = Script.parse(parser);
                } else {
                    throw new ParsingException(parser.getTokenLocation(), "[script] query does not support [" + currentFieldName + "]");
                }
            } else if (token.isValue()) {
                if (AbstractQueryBuilder.NAME_FIELD.match(currentFieldName)) {
                    queryName = parser.text();
                } else if (AbstractQueryBuilder.BOOST_FIELD.match(currentFieldName)) {
                    boost = parser.floatValue();
                } else if (Script.SCRIPT_PARSE_FIELD.match(currentFieldName)) {
                    script = Script.parse(parser);
                } else {
                    throw new ParsingException(parser.getTokenLocation(), "[script] query does not support [" + currentFieldName + "]");
                }
            }
        }

        if (script == null) {
            throw new ParsingException(parser.getTokenLocation(), "script must be provided with a [script] filter");
        }

        return new ScriptQueryBuilder(script)
                .boost(boost)
                .queryName(queryName);
    }

    @Override
    protected Query doToQuery(QueryShardContext context) throws IOException {
        return new ScriptQuery(script, context.getSearchScript(script, ScriptContext.Standard.SEARCH));
    }

    static class ScriptQuery extends Query {

        final Script script;
        final SearchScript searchScript;

        ScriptQuery(Script script, SearchScript searchScript) {
            this.script = script;
            this.searchScript = searchScript;
        }

        @Override
        public String toString(String field) {
            StringBuilder buffer = new StringBuilder();
            buffer.append("ScriptFilter(");
            buffer.append(script);
            buffer.append(")");
            return buffer.toString();
        }

        @Override
        public boolean equals(Object obj) {
            // TODO: Do this if/when we can assume scripts are pure functions
            // and they have a reliable equals impl
            /*if (this == obj)
                return true;
            if (sameClassAs(obj) == false)
                return false;
            ScriptQuery other = (ScriptQuery) obj;
            return Objects.equals(script, other.script);*/
            return this == obj;
        }

        @Override
        public int hashCode() {
            // TODO: Do this if/when we can assume scripts are pure functions
            // and they have a reliable equals impl
            // return Objects.hash(classHash(), script);
            return System.identityHashCode(this);
        }

        @Override
        public Weight createWeight(IndexSearcher searcher, boolean needsScores, float boost) throws IOException {
            return new ConstantScoreWeight(this, boost) {

                @Override
                public Scorer scorer(LeafReaderContext context) throws IOException {
                    DocIdSetIterator approximation = DocIdSetIterator.all(context.reader().maxDoc());
                    final LeafSearchScript leafScript = searchScript.getLeafSearchScript(context);
                    TwoPhaseIterator twoPhase = new TwoPhaseIterator(approximation) {

                        @Override
                        public boolean matches() throws IOException {
                            leafScript.setDocument(approximation.docID());
                            Object val = leafScript.run();
                            if (val == null) {
                                return false;
                            }
                            if (val instanceof Boolean) {
                                return (Boolean) val;
                            }
                            if (val instanceof Number) {
                                return ((Number) val).longValue() != 0;
                            }
                            throw new IllegalArgumentException("Can't handle type [" + val + "] in script filter");
                        }

                        @Override
                        public float matchCost() {
                            // TODO: how can we compute this?
                            return 1000f;
                        }
                    };
                    return new ConstantScoreScorer(this, score(), twoPhase);
                }
            };
        }
    }

    @Override
    protected int doHashCode() {
        return Objects.hash(script);
    }

    @Override
    protected boolean doEquals(ScriptQueryBuilder other) {
        return Objects.equals(script, other.script);
    }


}
