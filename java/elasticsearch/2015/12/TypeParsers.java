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

package org.elasticsearch.index.mapper.core;

import org.apache.lucene.index.IndexOptions;
import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.Version;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.joda.FormatDateTimeFormatter;
import org.elasticsearch.common.joda.Joda;
import org.elasticsearch.common.logging.ESLoggerFactory;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.settings.loader.SettingsLoader;
import org.elasticsearch.index.analysis.NamedAnalyzer;
import org.elasticsearch.index.mapper.DocumentMapperParser;
import org.elasticsearch.index.mapper.FieldMapper;
import org.elasticsearch.index.mapper.MappedFieldType.Loading;
import org.elasticsearch.index.mapper.Mapper;
import org.elasticsearch.index.mapper.MapperParsingException;
import org.elasticsearch.index.mapper.object.ObjectMapper;
import org.elasticsearch.index.similarity.SimilarityProvider;
import org.elasticsearch.index.similarity.SimilarityService;

import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;

import static org.elasticsearch.common.xcontent.support.XContentMapValues.isArray;
import static org.elasticsearch.common.xcontent.support.XContentMapValues.nodeBooleanValue;
import static org.elasticsearch.common.xcontent.support.XContentMapValues.nodeFloatValue;
import static org.elasticsearch.common.xcontent.support.XContentMapValues.nodeIntegerValue;
import static org.elasticsearch.common.xcontent.support.XContentMapValues.nodeMapValue;
import static org.elasticsearch.common.xcontent.support.XContentMapValues.nodeStringValue;

/**
 *
 */
public class TypeParsers {

    public static final String DOC_VALUES = "doc_values";
    public static final String INDEX_OPTIONS_DOCS = "docs";
    public static final String INDEX_OPTIONS_FREQS = "freqs";
    public static final String INDEX_OPTIONS_POSITIONS = "positions";
    public static final String INDEX_OPTIONS_OFFSETS = "offsets";

    public static void parseNumberField(NumberFieldMapper.Builder builder, String name, Map<String, Object> numberNode, Mapper.TypeParser.ParserContext parserContext) {
        parseField(builder, name, numberNode, parserContext);
        for (Iterator<Map.Entry<String, Object>> iterator = numberNode.entrySet().iterator(); iterator.hasNext();) {
            Map.Entry<String, Object> entry = iterator.next();
            String propName = Strings.toUnderscoreCase(entry.getKey());
            Object propNode = entry.getValue();
            if (propName.equals("precision_step")) {
                builder.precisionStep(nodeIntegerValue(propNode));
                iterator.remove();
            } else if (propName.equals("ignore_malformed")) {
                builder.ignoreMalformed(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("coerce")) {
                builder.coerce(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("omit_norms")) {
                builder.omitNorms(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("similarity")) {
                SimilarityProvider similarityProvider = resolveSimilarity(parserContext, name, propNode.toString());
                builder.similarity(similarityProvider);
                iterator.remove();
            } else if (parseMultiField(builder, name, parserContext, propName, propNode)) {
                iterator.remove();
            }
        }
    }

    private static void parseAnalyzersAndTermVectors(FieldMapper.Builder builder, String name, Map<String, Object> fieldNode, Mapper.TypeParser.ParserContext parserContext) {
        NamedAnalyzer indexAnalyzer = builder.fieldType().indexAnalyzer();
        NamedAnalyzer searchAnalyzer = builder.fieldType().searchAnalyzer();

        for (Iterator<Map.Entry<String, Object>> iterator = fieldNode.entrySet().iterator(); iterator.hasNext();) {
            Map.Entry<String, Object> entry = iterator.next();
            final String propName = Strings.toUnderscoreCase(entry.getKey());
            final Object propNode = entry.getValue();
            if (propName.equals("term_vector")) {
                parseTermVector(name, propNode.toString(), builder);
                iterator.remove();
            } else if (propName.equals("store_term_vectors")) {
                builder.storeTermVectors(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("store_term_vector_offsets")) {
                builder.storeTermVectorOffsets(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("store_term_vector_positions")) {
                builder.storeTermVectorPositions(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("store_term_vector_payloads")) {
                builder.storeTermVectorPayloads(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("analyzer") || // for backcompat, reading old indexes, remove for v3.0
                    propName.equals("index_analyzer") && parserContext.indexVersionCreated().before(Version.V_2_0_0_beta1)) {

                NamedAnalyzer analyzer = parserContext.analysisService().analyzer(propNode.toString());
                if (analyzer == null) {
                    throw new MapperParsingException("analyzer [" + propNode.toString() + "] not found for field [" + name + "]");
                }
                indexAnalyzer = analyzer;
                iterator.remove();
            } else if (propName.equals("search_analyzer")) {
                NamedAnalyzer analyzer = parserContext.analysisService().analyzer(propNode.toString());
                if (analyzer == null) {
                    throw new MapperParsingException("analyzer [" + propNode.toString() + "] not found for field [" + name + "]");
                }
                searchAnalyzer = analyzer;
                iterator.remove();
            }
        }

        if (indexAnalyzer == null) {
            if (searchAnalyzer != null) {
                throw new MapperParsingException("analyzer on field [" + name + "] must be set when search_analyzer is set");
            }
        } else if (searchAnalyzer == null) {
            searchAnalyzer = indexAnalyzer;
        }
        builder.indexAnalyzer(indexAnalyzer);
        builder.searchAnalyzer(searchAnalyzer);
    }

    /**
     * Parse text field attributes. In addition to {@link #parseField common attributes}
     * this will parse analysis and term-vectors related settings.
     */
    public static void parseTextField(FieldMapper.Builder builder, String name, Map<String, Object> fieldNode, Mapper.TypeParser.ParserContext parserContext) {
        parseField(builder, name, fieldNode, parserContext);
        parseAnalyzersAndTermVectors(builder, name, fieldNode, parserContext);
    }

    /**
     * Parse common field attributes such as {@code doc_values} or {@code store}.
     */
    public static void parseField(FieldMapper.Builder builder, String name, Map<String, Object> fieldNode, Mapper.TypeParser.ParserContext parserContext) {
        Version indexVersionCreated = parserContext.indexVersionCreated();
        for (Iterator<Map.Entry<String, Object>> iterator = fieldNode.entrySet().iterator(); iterator.hasNext();) {
            Map.Entry<String, Object> entry = iterator.next();
            final String propName = Strings.toUnderscoreCase(entry.getKey());
            final Object propNode = entry.getValue();
            if (propName.equals("store")) {
                builder.store(parseStore(name, propNode.toString()));
                iterator.remove();
            } else if (propName.equals("index")) {
                parseIndex(name, propNode.toString(), builder);
                iterator.remove();
            } else if (propName.equals(DOC_VALUES)) {
                builder.docValues(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("boost")) {
                builder.boost(nodeFloatValue(propNode));
                iterator.remove();
            } else if (propName.equals("omit_norms")) {
                builder.omitNorms(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("norms")) {
                final Map<String, Object> properties = nodeMapValue(propNode, "norms");
                for (Iterator<Entry<String, Object>> propsIterator = properties.entrySet().iterator(); propsIterator.hasNext();) {
                    Entry<String, Object> entry2 = propsIterator.next();
                    final String propName2 = Strings.toUnderscoreCase(entry2.getKey());
                    final Object propNode2 = entry2.getValue();
                    if (propName2.equals("enabled")) {
                        builder.omitNorms(!nodeBooleanValue(propNode2));
                        propsIterator.remove();
                    } else if (propName2.equals(Loading.KEY)) {
                        builder.normsLoading(Loading.parse(nodeStringValue(propNode2, null), null));
                        propsIterator.remove();
                    }
                }
                DocumentMapperParser.checkNoRemainingFields(propName, properties, parserContext.indexVersionCreated());
                iterator.remove();
            } else if (propName.equals("omit_term_freq_and_positions")) {
                final IndexOptions op = nodeBooleanValue(propNode) ? IndexOptions.DOCS : IndexOptions.DOCS_AND_FREQS_AND_POSITIONS;
                if (indexVersionCreated.onOrAfter(Version.V_1_0_0_RC2)) {
                    throw new ElasticsearchParseException("'omit_term_freq_and_positions' is not supported anymore - use ['index_options' : 'docs']  instead");
                }
                // deprecated option for BW compat
                builder.indexOptions(op);
                iterator.remove();
            } else if (propName.equals("index_options")) {
                builder.indexOptions(nodeIndexOptionValue(propNode));
                iterator.remove();
            } else if (propName.equals("include_in_all")) {
                builder.includeInAll(nodeBooleanValue(propNode));
                iterator.remove();
            } else if (propName.equals("postings_format") && indexVersionCreated.before(Version.V_2_0_0_beta1)) {
                // ignore for old indexes
                iterator.remove();
            } else if (propName.equals("doc_values_format") && indexVersionCreated.before(Version.V_2_0_0_beta1)) {
                // ignore for old indexes
                iterator.remove();
            } else if (propName.equals("similarity")) {
                SimilarityProvider similarityProvider = resolveSimilarity(parserContext, name, propNode.toString());
                builder.similarity(similarityProvider);
                iterator.remove();
            } else if (propName.equals("fielddata")) {
                final Settings settings = Settings.builder().put(SettingsLoader.Helper.loadNestedFromMap(nodeMapValue(propNode, "fielddata"))).build();
                builder.fieldDataSettings(settings);
                iterator.remove();
            } else if (propName.equals("copy_to")) {
                if (parserContext.isWithinMultiField()) {
                    if (indexVersionCreated.after(Version.V_2_1_0) ||
                        (indexVersionCreated.after(Version.V_2_0_1) && indexVersionCreated.before(Version.V_2_1_0))) {
                        throw new MapperParsingException("copy_to in multi fields is not allowed. Found the copy_to in field [" + name + "] which is within a multi field.");
                    } else {
                        ESLoggerFactory.getLogger("mapping [" + parserContext.type() + "]").warn("Found a copy_to in field [" + name + "] which is within a multi field. This feature has been removed and the copy_to will be removed from the mapping.");
                    }
                } else {
                    parseCopyFields(propNode, builder);
                }
                iterator.remove();
            }
        }
        if (indexVersionCreated.before(Version.V_2_2_0)) {
            // analyzer, search_analyzer, term_vectors were accepted on all fields
            // before 2.2, even though it made little sense
            parseAnalyzersAndTermVectors(builder, name, fieldNode, parserContext);
        }
    }

    public static boolean parseMultiField(FieldMapper.Builder builder, String name, Mapper.TypeParser.ParserContext parserContext, String propName, Object propNode) {
        parserContext = parserContext.createMultiFieldContext(parserContext);
        if (propName.equals("fields")) {

            final Map<String, Object> multiFieldsPropNodes;

            if (propNode instanceof List && ((List<?>) propNode).isEmpty()) {
                multiFieldsPropNodes = Collections.emptyMap();
            } else if (propNode instanceof Map) {
                multiFieldsPropNodes = (Map<String, Object>) propNode;
            } else {
                throw new MapperParsingException("expected map for property [fields] on field [" + propNode + "] or " +
                        "[" + propName + "] but got a " + propNode.getClass());
            }

            for (Map.Entry<String, Object> multiFieldEntry : multiFieldsPropNodes.entrySet()) {
                String multiFieldName = multiFieldEntry.getKey();
                if (multiFieldName.contains(".")) {
                    throw new MapperParsingException("Field name [" + multiFieldName + "] which is a multi field of [" + name + "] cannot contain '.'");
                }
                if (!(multiFieldEntry.getValue() instanceof Map)) {
                    throw new MapperParsingException("illegal field [" + multiFieldName + "], only fields can be specified inside fields");
                }
                @SuppressWarnings("unchecked")
                Map<String, Object> multiFieldNodes = (Map<String, Object>) multiFieldEntry.getValue();

                String type;
                Object typeNode = multiFieldNodes.get("type");
                if (typeNode != null) {
                    type = typeNode.toString();
                } else {
                    throw new MapperParsingException("no type specified for property [" + multiFieldName + "]");
                }
                if (type.equals(ObjectMapper.CONTENT_TYPE) || type.equals(ObjectMapper.NESTED_CONTENT_TYPE)) {
                    throw new MapperParsingException("Type [" + type + "] cannot be used in multi field");
                }

                Mapper.TypeParser typeParser = parserContext.typeParser(type);
                if (typeParser == null) {
                    throw new MapperParsingException("no handler for type [" + type + "] declared on field [" + multiFieldName + "]");
                }
                builder.addMultiField(typeParser.parse(multiFieldName, multiFieldNodes, parserContext));
                multiFieldNodes.remove("type");
                DocumentMapperParser.checkNoRemainingFields(propName, multiFieldNodes, parserContext.indexVersionCreated());
            }
            return true;
        }
        return false;
    }

    private static IndexOptions nodeIndexOptionValue(final Object propNode) {
        final String value = propNode.toString();
        if (INDEX_OPTIONS_OFFSETS.equalsIgnoreCase(value)) {
            return IndexOptions.DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
        } else if (INDEX_OPTIONS_POSITIONS.equalsIgnoreCase(value)) {
            return IndexOptions.DOCS_AND_FREQS_AND_POSITIONS;
        } else if (INDEX_OPTIONS_FREQS.equalsIgnoreCase(value)) {
            return IndexOptions.DOCS_AND_FREQS;
        } else if (INDEX_OPTIONS_DOCS.equalsIgnoreCase(value)) {
            return IndexOptions.DOCS;
        } else {
            throw new ElasticsearchParseException("failed to parse index option [{}]", value);
        }
    }

    public static FormatDateTimeFormatter parseDateTimeFormatter(Object node) {
        return Joda.forPattern(node.toString());
    }

    public static void parseTermVector(String fieldName, String termVector, FieldMapper.Builder builder) throws MapperParsingException {
        termVector = Strings.toUnderscoreCase(termVector);
        if ("no".equals(termVector)) {
            builder.storeTermVectors(false);
        } else if ("yes".equals(termVector)) {
            builder.storeTermVectors(true);
        } else if ("with_offsets".equals(termVector)) {
            builder.storeTermVectorOffsets(true);
        } else if ("with_positions".equals(termVector)) {
            builder.storeTermVectorPositions(true);
        } else if ("with_positions_offsets".equals(termVector)) {
            builder.storeTermVectorPositions(true);
            builder.storeTermVectorOffsets(true);
        } else if ("with_positions_payloads".equals(termVector)) {
            builder.storeTermVectorPositions(true);
            builder.storeTermVectorPayloads(true);
        } else if ("with_positions_offsets_payloads".equals(termVector)) {
            builder.storeTermVectorPositions(true);
            builder.storeTermVectorOffsets(true);
            builder.storeTermVectorPayloads(true);
        } else {
            throw new MapperParsingException("wrong value for termVector [" + termVector + "] for field [" + fieldName + "]");
        }
    }

    public static void parseIndex(String fieldName, String index, FieldMapper.Builder builder) throws MapperParsingException {
        index = Strings.toUnderscoreCase(index);
        if ("no".equals(index)) {
            builder.index(false);
        } else if ("not_analyzed".equals(index)) {
            builder.index(true);
            builder.tokenized(false);
        } else if ("analyzed".equals(index)) {
            builder.index(true);
            builder.tokenized(true);
        } else {
            throw new MapperParsingException("wrong value for index [" + index + "] for field [" + fieldName + "]");
        }
    }

    public static boolean parseStore(String fieldName, String store) throws MapperParsingException {
        if ("no".equals(store)) {
            return false;
        } else if ("yes".equals(store)) {
            return true;
        } else {
            return nodeBooleanValue(store);
        }
    }

    @SuppressWarnings("unchecked")
    public static void parseCopyFields(Object propNode, FieldMapper.Builder builder) {
        FieldMapper.CopyTo.Builder copyToBuilder = new FieldMapper.CopyTo.Builder();
        if (isArray(propNode)) {
            for(Object node : (List<Object>) propNode) {
                copyToBuilder.add(nodeStringValue(node, null));
            }
        } else {
            copyToBuilder.add(nodeStringValue(propNode, null));
        }
        builder.copyTo(copyToBuilder.build());
    }

    private static SimilarityProvider resolveSimilarity(Mapper.TypeParser.ParserContext parserContext, String name, String value) {
        if (parserContext.indexVersionCreated().before(Version.V_3_0_0) && "default".equals(value)) {
            // "default" similarity has been renamed into "classic" in 3.x.
            value = SimilarityService.DEFAULT_SIMILARITY;
        }
        SimilarityProvider similarityProvider = parserContext.getSimilarity(value);
        if (similarityProvider == null) {
            throw new MapperParsingException("Unknown Similarity type [" + value + "] for [" + name + "]");
        }
        return similarityProvider;
    }
}
