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

package org.elasticsearch.percolator;

import org.apache.lucene.analysis.core.WhitespaceAnalyzer;
import org.apache.lucene.document.LongPoint;
import org.apache.lucene.index.IndexReader;
import org.apache.lucene.index.IndexableField;
import org.apache.lucene.index.PrefixCodedTerms;
import org.apache.lucene.index.Term;
import org.apache.lucene.index.memory.MemoryIndex;
import org.apache.lucene.queries.TermsQuery;
import org.apache.lucene.search.BooleanClause;
import org.apache.lucene.search.BooleanQuery;
import org.apache.lucene.search.PhraseQuery;
import org.apache.lucene.search.TermQuery;
import org.apache.lucene.search.TermRangeQuery;
import org.apache.lucene.search.join.ScoreMode;
import org.apache.lucene.util.BytesRef;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.compress.CompressedXContent;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.mapper.DocumentMapper;
import org.elasticsearch.index.mapper.DocumentMapperParser;
import org.elasticsearch.index.mapper.MapperParsingException;
import org.elasticsearch.index.mapper.MapperService;
import org.elasticsearch.index.mapper.ParseContext;
import org.elasticsearch.index.mapper.ParsedDocument;
import org.elasticsearch.index.query.BoolQueryBuilder;
import org.elasticsearch.index.query.BoostingQueryBuilder;
import org.elasticsearch.index.query.ConstantScoreQueryBuilder;
import org.elasticsearch.index.query.HasChildQueryBuilder;
import org.elasticsearch.index.query.HasParentQueryBuilder;
import org.elasticsearch.index.query.MatchAllQueryBuilder;
import org.elasticsearch.index.query.QueryBuilder;
import org.elasticsearch.index.query.QueryParseContext;
import org.elasticsearch.index.query.QueryShardException;
import org.elasticsearch.index.query.RangeQueryBuilder;
import org.elasticsearch.index.query.functionscore.FunctionScoreQueryBuilder;
import org.elasticsearch.index.query.functionscore.RandomScoreFunctionBuilder;
import org.elasticsearch.indices.TermsLookup;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.test.ESSingleNodeTestCase;
import org.elasticsearch.test.InternalSettingsPlugin;
import org.elasticsearch.test.VersionUtils;
import org.junit.Before;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import static com.carrotsearch.randomizedtesting.RandomizedTest.getRandom;
import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.elasticsearch.index.query.QueryBuilders.matchAllQuery;
import static org.elasticsearch.index.query.QueryBuilders.matchPhraseQuery;
import static org.elasticsearch.index.query.QueryBuilders.matchQuery;
import static org.elasticsearch.index.query.QueryBuilders.prefixQuery;
import static org.elasticsearch.index.query.QueryBuilders.rangeQuery;
import static org.elasticsearch.index.query.QueryBuilders.termQuery;
import static org.elasticsearch.index.query.QueryBuilders.termsLookupQuery;
import static org.elasticsearch.index.query.QueryBuilders.wildcardQuery;
import static org.elasticsearch.percolator.PercolatorFieldMapper.EXTRACTION_COMPLETE;
import static org.elasticsearch.percolator.PercolatorFieldMapper.EXTRACTION_FAILED;
import static org.elasticsearch.percolator.PercolatorFieldMapper.EXTRACTION_PARTIAL;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;

public class PercolatorFieldMapperTests extends ESSingleNodeTestCase {

    private String typeName;
    private String fieldName;
    private IndexService indexService;
    private MapperService mapperService;
    private PercolatorFieldMapper.FieldType fieldType;

    @Override
    protected Collection<Class<? extends Plugin>> getPlugins() {
        return pluginList(InternalSettingsPlugin.class, PercolatorPlugin.class);
    }

    @Before
    public void init() throws Exception {
        indexService = createIndex("test", Settings.EMPTY);
        mapperService = indexService.mapperService();

        String mapper = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("_field_names").field("enabled", false).endObject() // makes testing easier
            .startObject("properties")
                .startObject("field").field("type", "text").endObject()
                .startObject("field1").field("type", "text").endObject()
                .startObject("field2").field("type", "text").endObject()
                .startObject("_field3").field("type", "text").endObject()
                .startObject("field4").field("type", "text").endObject()
                .startObject("number_field").field("type", "long").endObject()
                .startObject("date_field").field("type", "date").endObject()
            .endObject().endObject().endObject().string();
        mapperService.merge("type", new CompressedXContent(mapper), MapperService.MergeReason.MAPPING_UPDATE, true);
    }

    private void addQueryMapping() throws Exception {
        typeName = randomAsciiOfLength(4);
        fieldName = randomAsciiOfLength(4);
        String percolatorMapper = XContentFactory.jsonBuilder().startObject().startObject(typeName)
                .startObject("properties").startObject(fieldName).field("type", "percolator").endObject().endObject()
                .endObject().endObject().string();
        mapperService.merge(typeName, new CompressedXContent(percolatorMapper), MapperService.MergeReason.MAPPING_UPDATE, true);
        fieldType = (PercolatorFieldMapper.FieldType) mapperService.fullName(fieldName);
    }

    public void testExtractTerms() throws Exception {
        addQueryMapping();
        BooleanQuery.Builder bq = new BooleanQuery.Builder();
        TermQuery termQuery1 = new TermQuery(new Term("field", "term1"));
        bq.add(termQuery1, BooleanClause.Occur.SHOULD);
        TermQuery termQuery2 = new TermQuery(new Term("field", "term2"));
        bq.add(termQuery2, BooleanClause.Occur.SHOULD);

        DocumentMapper documentMapper = mapperService.documentMapper(typeName);
        PercolatorFieldMapper fieldMapper = (PercolatorFieldMapper) documentMapper.mappers().getMapper(fieldName);
        ParseContext.InternalParseContext parseContext = new ParseContext.InternalParseContext(Settings.EMPTY,
                mapperService.documentMapperParser(), documentMapper, null, null);
        fieldMapper.processQuery(bq.build(), parseContext);
        ParseContext.Document document = parseContext.doc();

        PercolatorFieldMapper.FieldType fieldType = (PercolatorFieldMapper.FieldType) fieldMapper.fieldType();
        assertThat(document.getField(fieldType.extractionResultField.name()).stringValue(), equalTo(EXTRACTION_COMPLETE));
        List<IndexableField> fields = new ArrayList<>(Arrays.asList(document.getFields(fieldType.queryTermsField.name())));
        Collections.sort(fields, (field1, field2) -> field1.binaryValue().compareTo(field2.binaryValue()));
        assertThat(fields.size(), equalTo(2));
        assertThat(fields.get(0).binaryValue().utf8ToString(), equalTo("field\u0000term1"));
        assertThat(fields.get(1).binaryValue().utf8ToString(), equalTo("field\u0000term2"));
    }

    public void testExtractTermsAndRanges_failed() throws Exception {
        addQueryMapping();
        TermRangeQuery query = new TermRangeQuery("field1", new BytesRef("a"), new BytesRef("z"), true, true);
        DocumentMapper documentMapper = mapperService.documentMapper(typeName);
        PercolatorFieldMapper fieldMapper = (PercolatorFieldMapper) documentMapper.mappers().getMapper(fieldName);
        ParseContext.InternalParseContext parseContext = new ParseContext.InternalParseContext(Settings.EMPTY,
                mapperService.documentMapperParser(), documentMapper, null, null);
        fieldMapper.processQuery(query, parseContext);
        ParseContext.Document document = parseContext.doc();

        PercolatorFieldMapper.FieldType fieldType = (PercolatorFieldMapper.FieldType) fieldMapper.fieldType();
        assertThat(document.getFields().size(), equalTo(1));
        assertThat(document.getField(fieldType.extractionResultField.name()).stringValue(), equalTo(EXTRACTION_FAILED));
    }

    public void testExtractTermsAndRanges_partial() throws Exception {
        addQueryMapping();
        PhraseQuery phraseQuery = new PhraseQuery("field", "term");
        DocumentMapper documentMapper = mapperService.documentMapper(typeName);
        PercolatorFieldMapper fieldMapper = (PercolatorFieldMapper) documentMapper.mappers().getMapper(fieldName);
        ParseContext.InternalParseContext parseContext = new ParseContext.InternalParseContext(Settings.EMPTY,
                mapperService.documentMapperParser(), documentMapper, null, null);
        fieldMapper.processQuery(phraseQuery, parseContext);
        ParseContext.Document document = parseContext.doc();

        PercolatorFieldMapper.FieldType fieldType = (PercolatorFieldMapper.FieldType) fieldMapper.fieldType();
        assertThat(document.getFields().size(), equalTo(2));
        assertThat(document.getFields().get(0).binaryValue().utf8ToString(), equalTo("field\u0000term"));
        assertThat(document.getField(fieldType.extractionResultField.name()).stringValue(), equalTo(EXTRACTION_PARTIAL));
    }

    public void testCreateCandidateQuery() throws Exception {
        addQueryMapping();

        MemoryIndex memoryIndex = new MemoryIndex(false);
        memoryIndex.addField("field1", "the quick brown fox jumps over the lazy dog", new WhitespaceAnalyzer());
        memoryIndex.addField("field2", "some more text", new WhitespaceAnalyzer());
        memoryIndex.addField("_field3", "unhide me", new WhitespaceAnalyzer());
        memoryIndex.addField("field4", "123", new WhitespaceAnalyzer());
        memoryIndex.addField(new LongPoint("number_field", 10L), new WhitespaceAnalyzer());

        IndexReader indexReader = memoryIndex.createSearcher().getIndexReader();

        TermsQuery termsQuery = (TermsQuery) fieldType.createCandidateQuery(indexReader);

        PrefixCodedTerms terms = termsQuery.getTermData();
        assertThat(terms.size(), equalTo(15L));
        PrefixCodedTerms.TermIterator termIterator = terms.iterator();
        assertTermIterator(termIterator, "_field3\u0000me", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "_field3\u0000unhide", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000brown", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000dog", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000fox", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000jumps", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000lazy", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000over", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000quick", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field1\u0000the", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field2\u0000more", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field2\u0000some", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field2\u0000text", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, "field4\u0000123", fieldType.queryTermsField.name());
        assertTermIterator(termIterator, EXTRACTION_FAILED, fieldType.extractionResultField.name());
    }

    private void assertTermIterator(PrefixCodedTerms.TermIterator termIterator, String expectedValue, String expectedField) {
        assertThat(termIterator.next().utf8ToString(), equalTo(expectedValue));
        assertThat(termIterator.field(), equalTo(expectedField));
    }

    public void testPercolatorFieldMapper() throws Exception {
        addQueryMapping();
        QueryBuilder queryBuilder = termQuery("field", "value");
        ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
            .field(fieldName, queryBuilder)
            .endObject().bytes());

        assertThat(doc.rootDoc().getFields(fieldType.queryTermsField.name()).length, equalTo(1));
        assertThat(doc.rootDoc().getFields(fieldType.queryTermsField.name())[0].binaryValue().utf8ToString(), equalTo("field\0value"));
        assertThat(doc.rootDoc().getFields(fieldType.queryBuilderField.name()).length, equalTo(1));
        assertThat(doc.rootDoc().getFields(fieldType.extractionResultField.name()).length, equalTo(1));
        assertThat(doc.rootDoc().getFields(fieldType.extractionResultField.name())[0].stringValue(),
                equalTo(EXTRACTION_COMPLETE));
        BytesRef qbSource = doc.rootDoc().getFields(fieldType.queryBuilderField.name())[0].binaryValue();
        assertQueryBuilder(qbSource, queryBuilder);

        // add an query for which we don't extract terms from
        queryBuilder = rangeQuery("field").from("a").to("z");
        doc = mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
                .field(fieldName, queryBuilder)
                .endObject().bytes());
        assertThat(doc.rootDoc().getFields(fieldType.extractionResultField.name()).length, equalTo(1));
        assertThat(doc.rootDoc().getFields(fieldType.extractionResultField.name())[0].stringValue(),
                equalTo(EXTRACTION_FAILED));
        assertThat(doc.rootDoc().getFields(fieldType.queryTermsField.name()).length, equalTo(0));
        assertThat(doc.rootDoc().getFields(fieldType.queryBuilderField.name()).length, equalTo(1));
        qbSource = doc.rootDoc().getFields(fieldType.queryBuilderField.name())[0].binaryValue();
        assertQueryBuilder(qbSource, queryBuilder);
    }

    public void testStoringQueries() throws Exception {
        addQueryMapping();
        QueryBuilder[] queries = new QueryBuilder[]{
                termQuery("field", "value"), matchAllQuery(), matchQuery("field", "value"), matchPhraseQuery("field", "value"),
                prefixQuery("field", "v"), wildcardQuery("field", "v*"), rangeQuery("number_field").gte(0).lte(9),
                rangeQuery("date_field").from("2015-01-01T00:00").to("2015-01-01T00:00")
        };
        // note: it important that range queries never rewrite, otherwise it will cause results to be wrong.
        // (it can't use shard data for rewriting purposes, because percolator queries run on MemoryIndex)

        for (QueryBuilder query : queries) {
            ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1",
                    XContentFactory.jsonBuilder().startObject()
                    .field(fieldName, query)
                    .endObject().bytes());
            BytesRef qbSource = doc.rootDoc().getFields(fieldType.queryBuilderField.name())[0].binaryValue();
            assertQueryBuilder(qbSource, query);
        }
    }

    public void testQueryWithRewrite() throws Exception {
        addQueryMapping();
        client().prepareIndex("remote", "type", "1").setSource("field", "value").get();
        QueryBuilder queryBuilder = termsLookupQuery("field", new TermsLookup("remote", "type", "1", "field"));
        ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
                .field(fieldName, queryBuilder)
                .endObject().bytes());
        BytesRef qbSource = doc.rootDoc().getFields(fieldType.queryBuilderField.name())[0].binaryValue();
        assertQueryBuilder(qbSource, queryBuilder.rewrite(indexService.newQueryShardContext()));
    }


    public void testPercolatorFieldMapperUnMappedField() throws Exception {
        addQueryMapping();
        MapperParsingException exception = expectThrows(MapperParsingException.class, () -> {
            mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
                    .field(fieldName, termQuery("unmapped_field", "value"))
                    .endObject().bytes());
        });
        assertThat(exception.getCause(), instanceOf(QueryShardException.class));
        assertThat(exception.getCause().getMessage(), equalTo("No field mapping can be found for the field with name [unmapped_field]"));
    }


    public void testPercolatorFieldMapper_noQuery() throws Exception {
        addQueryMapping();
        ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
            .endObject().bytes());
        assertThat(doc.rootDoc().getFields(fieldType.queryBuilderField.name()).length, equalTo(0));

        try {
            mapperService.documentMapper(typeName).parse("test", typeName, "1", XContentFactory.jsonBuilder().startObject()
                .nullField(fieldName)
                .endObject().bytes());
        } catch (MapperParsingException e) {
            assertThat(e.getDetailedMessage(), containsString("query malformed, must start with start_object"));
        }
    }

    public void testAllowNoAdditionalSettings() throws Exception {
        addQueryMapping();
        IndexService indexService = createIndex("test1", Settings.EMPTY);
        MapperService mapperService = indexService.mapperService();

        String percolatorMapper = XContentFactory.jsonBuilder().startObject().startObject(typeName)
            .startObject("properties").startObject(fieldName).field("type", "percolator").field("index", "no").endObject().endObject()
            .endObject().endObject().string();
        try {
            mapperService.merge(typeName, new CompressedXContent(percolatorMapper), MapperService.MergeReason.MAPPING_UPDATE, true);
            fail("MapperParsingException expected");
        } catch (MapperParsingException e) {
            assertThat(e.getMessage(), equalTo("Mapping definition for [" + fieldName + "] has unsupported parameters:  [index : no]"));
        }
    }

    // multiple percolator fields are allowed in the mapping, but only one field can be used at index time.
    public void testMultiplePercolatorFields() throws Exception {
        String typeName = "another_type";
        String percolatorMapper = XContentFactory.jsonBuilder().startObject().startObject(typeName)
                .startObject("_field_names").field("enabled", false).endObject() // makes testing easier
                .startObject("properties")
                    .startObject("query_field1").field("type", "percolator").endObject()
                    .startObject("query_field2").field("type", "percolator").endObject()
                .endObject()
                .endObject().endObject().string();
        mapperService.merge(typeName, new CompressedXContent(percolatorMapper), MapperService.MergeReason.MAPPING_UPDATE, true);

        QueryBuilder queryBuilder = matchQuery("field", "value");
        ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1",
                jsonBuilder().startObject()
                        .field("query_field1", queryBuilder)
                        .field("query_field2", queryBuilder)
                        .endObject().bytes()
        );
        assertThat(doc.rootDoc().getFields().size(), equalTo(11)); // also includes _uid (1), type (2), source (1)
        BytesRef queryBuilderAsBytes = doc.rootDoc().getField("query_field1.query_builder_field").binaryValue();
        assertQueryBuilder(queryBuilderAsBytes, queryBuilder);

        queryBuilderAsBytes = doc.rootDoc().getField("query_field2.query_builder_field").binaryValue();
        assertQueryBuilder(queryBuilderAsBytes, queryBuilder);
    }

    // percolator field can be nested under an object field, but only one query can be specified per document
    public void testNestedPercolatorField() throws Exception {
        String typeName = "another_type";
        String percolatorMapper = XContentFactory.jsonBuilder().startObject().startObject(typeName)
                .startObject("_field_names").field("enabled", false).endObject() // makes testing easier
                .startObject("properties")
                .startObject("object_field")
                    .field("type", "object")
                    .startObject("properties")
                        .startObject("query_field").field("type", "percolator").endObject()
                    .endObject()
                .endObject()
                .endObject()
                .endObject().endObject().string();
        mapperService.merge(typeName, new CompressedXContent(percolatorMapper), MapperService.MergeReason.MAPPING_UPDATE, true);

        QueryBuilder queryBuilder = matchQuery("field", "value");
        ParsedDocument doc = mapperService.documentMapper(typeName).parse("test", typeName, "1",
                jsonBuilder().startObject().startObject("object_field")
                            .field("query_field", queryBuilder)
                        .endObject().endObject().bytes()
        );
        assertThat(doc.rootDoc().getFields().size(), equalTo(8)); // also includes _uid (1), type (2), source (1)
        BytesRef queryBuilderAsBytes = doc.rootDoc().getField("object_field.query_field.query_builder_field").binaryValue();
        assertQueryBuilder(queryBuilderAsBytes, queryBuilder);

        doc = mapperService.documentMapper(typeName).parse("test", typeName, "1",
                jsonBuilder().startObject()
                            .startArray("object_field")
                                .startObject().field("query_field", queryBuilder).endObject()
                            .endArray()
                        .endObject().bytes()
        );
        assertThat(doc.rootDoc().getFields().size(), equalTo(8)); // also includes _uid (1), type (2), source (1)
        queryBuilderAsBytes = doc.rootDoc().getField("object_field.query_field.query_builder_field").binaryValue();
        assertQueryBuilder(queryBuilderAsBytes, queryBuilder);

        MapperParsingException e = expectThrows(MapperParsingException.class, () -> {
                    mapperService.documentMapper(typeName).parse("test", typeName, "1",
                            jsonBuilder().startObject()
                                    .startArray("object_field")
                                        .startObject().field("query_field", queryBuilder).endObject()
                                        .startObject().field("query_field", queryBuilder).endObject()
                                    .endArray()
                                .endObject().bytes()
                    );
                }
        );
        assertThat(e.getCause(), instanceOf(IllegalArgumentException.class));
        assertThat(e.getCause().getMessage(), equalTo("a document can only contain one percolator query"));
    }

    public void testRangeQueryWithNowRangeIsForbidden() throws Exception {
        addQueryMapping();
        MapperParsingException e = expectThrows(MapperParsingException.class, () -> {
            mapperService.documentMapper(typeName).parse("test", typeName, "1",
                        jsonBuilder().startObject()
                                .field(fieldName, rangeQuery("date_field").from("2016-01-01||/D").to("now"))
                                .endObject().bytes());
            }
        );
        assertThat(e.getCause(), instanceOf(IllegalArgumentException.class));
        e = expectThrows(MapperParsingException.class, () -> {
            mapperService.documentMapper(typeName).parse("test", typeName, "1",
                        jsonBuilder().startObject()
                                .field(fieldName, rangeQuery("date_field").from("2016-01-01||/D").to("now/D"))
                                .endObject().bytes());
                }
        );
        assertThat(e.getCause(), instanceOf(IllegalArgumentException.class));
        e = expectThrows(MapperParsingException.class, () -> {
            mapperService.documentMapper(typeName).parse("test", typeName, "1",
                        jsonBuilder().startObject()
                                .field(fieldName, rangeQuery("date_field").from("now-1d").to("now"))
                                .endObject().bytes());
                }
        );
        assertThat(e.getCause(), instanceOf(IllegalArgumentException.class));
    }

    public void testUnsupportedQueries() {
        RangeQueryBuilder rangeQuery1 = new RangeQueryBuilder("field").from("2016-01-01||/D").to("2017-01-01||/D");
        RangeQueryBuilder rangeQuery2 = new RangeQueryBuilder("field").from("2016-01-01||/D").to("now");
        PercolatorFieldMapper.verifyQuery(rangeQuery1);
        expectThrows(IllegalArgumentException.class, () -> PercolatorFieldMapper.verifyQuery(rangeQuery2));
        PercolatorFieldMapper.verifyQuery(new BoolQueryBuilder().must(rangeQuery1));
        expectThrows(IllegalArgumentException.class, () ->
                PercolatorFieldMapper.verifyQuery(new BoolQueryBuilder().must(rangeQuery2)));
        PercolatorFieldMapper.verifyQuery(new ConstantScoreQueryBuilder((rangeQuery1)));
        expectThrows(IllegalArgumentException.class, () ->
                PercolatorFieldMapper.verifyQuery(new ConstantScoreQueryBuilder(rangeQuery2)));
        PercolatorFieldMapper.verifyQuery(new BoostingQueryBuilder(rangeQuery1, new MatchAllQueryBuilder()));
        expectThrows(IllegalArgumentException.class, () ->
                PercolatorFieldMapper.verifyQuery(new BoostingQueryBuilder(rangeQuery2, new MatchAllQueryBuilder())));
        PercolatorFieldMapper.verifyQuery(new FunctionScoreQueryBuilder(rangeQuery1, new RandomScoreFunctionBuilder()));
        expectThrows(IllegalArgumentException.class, () ->
                PercolatorFieldMapper.verifyQuery(new FunctionScoreQueryBuilder(rangeQuery2, new RandomScoreFunctionBuilder())));

        HasChildQueryBuilder hasChildQuery = new HasChildQueryBuilder("_type", new MatchAllQueryBuilder(), ScoreMode.None);
        expectThrows(IllegalArgumentException.class, () -> PercolatorFieldMapper.verifyQuery(hasChildQuery));
        expectThrows(IllegalArgumentException.class, () -> PercolatorFieldMapper.verifyQuery(new BoolQueryBuilder().must(hasChildQuery)));

        HasParentQueryBuilder hasParentQuery = new HasParentQueryBuilder("_type", new MatchAllQueryBuilder(), false);
        expectThrows(IllegalArgumentException.class, () -> PercolatorFieldMapper.verifyQuery(hasParentQuery));
        expectThrows(IllegalArgumentException.class, () -> PercolatorFieldMapper.verifyQuery(new BoolQueryBuilder().must(hasParentQuery)));
    }

    private void assertQueryBuilder(BytesRef actual, QueryBuilder expected) throws IOException {
        XContentParser sourceParser = PercolatorFieldMapper.QUERY_BUILDER_CONTENT_TYPE.xContent()
                .createParser(actual.bytes, actual.offset, actual.length);
        QueryParseContext qsc = indexService.newQueryShardContext().newParseContext(sourceParser);
        assertThat(qsc.parseInnerQueryBuilder().get(), equalTo(expected));
    }


    public void testEmptyName() throws Exception {
        // after 5.x
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type1")
            .startObject("properties").startObject("").field("type", "percolator").endObject().endObject()
            .endObject().endObject().string();
        DocumentMapperParser parser = mapperService.documentMapperParser();

        IllegalArgumentException e = expectThrows(IllegalArgumentException.class,
            () -> parser.parse("type1", new CompressedXContent(mapping))
        );
        assertThat(e.getMessage(), containsString("name cannot be empty string"));

        // before 5.x
        Version oldVersion = VersionUtils.randomVersionBetween(getRandom(), Version.V_2_0_0, Version.V_2_3_5);
        Settings oldIndexSettings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, oldVersion).build();
        DocumentMapperParser parser2x = createIndex("test_old", oldIndexSettings).mapperService().documentMapperParser();

        DocumentMapper defaultMapper = parser2x.parse("type1", new CompressedXContent(mapping));
        assertEquals(mapping, defaultMapper.mappingSource().string());
    }
}
