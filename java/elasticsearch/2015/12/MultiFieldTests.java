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

package org.elasticsearch.index.mapper.multifield;

import org.apache.lucene.index.IndexOptions;
import org.apache.lucene.index.IndexableField;
import org.apache.lucene.util.GeoUtils;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.compress.CompressedXContent;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.common.xcontent.support.XContentMapValues;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.mapper.DocumentMapper;
import org.elasticsearch.index.mapper.DocumentMapperParser;
import org.elasticsearch.index.mapper.MappedFieldType;
import org.elasticsearch.index.mapper.MapperParsingException;
import org.elasticsearch.index.mapper.MapperService;
import org.elasticsearch.index.mapper.ParseContext.Document;
import org.elasticsearch.index.mapper.core.CompletionFieldMapper;
import org.elasticsearch.index.mapper.core.DateFieldMapper;
import org.elasticsearch.index.mapper.core.LongFieldMapper;
import org.elasticsearch.index.mapper.core.StringFieldMapper;
import org.elasticsearch.index.mapper.core.TokenCountFieldMapper;
import org.elasticsearch.index.mapper.geo.BaseGeoPointFieldMapper;
import org.elasticsearch.test.ESSingleNodeTestCase;
import org.elasticsearch.test.VersionUtils;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.Map;
import java.util.TreeMap;

import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.elasticsearch.index.mapper.MapperBuilders.doc;
import static org.elasticsearch.index.mapper.MapperBuilders.rootObject;
import static org.elasticsearch.index.mapper.MapperBuilders.stringField;
import static org.elasticsearch.test.StreamsUtils.copyToBytesFromClasspath;
import static org.elasticsearch.test.StreamsUtils.copyToStringFromClasspath;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.notNullValue;

/**
 *
 */
public class MultiFieldTests extends ESSingleNodeTestCase {

    public void testMultiFieldMultiFields() throws Exception {
        String mapping = copyToStringFromClasspath("/org/elasticsearch/index/mapper/multifield/test-multi-fields.json");
        testMultiField(mapping);
    }

    private void testMultiField(String mapping) throws Exception {
        DocumentMapper docMapper = createIndex("test").mapperService().documentMapperParser().parse("person", new CompressedXContent(mapping));
        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/multifield/test-data.json"));
        Document doc = docMapper.parse("test", "person", "1", json).rootDoc();

        IndexableField f = doc.getField("name");
        assertThat(f.name(), equalTo("name"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().stored(), equalTo(true));
        assertNotSame(IndexOptions.NONE, f.fieldType().indexOptions());

        f = doc.getField("name.indexed");
        assertThat(f.name(), equalTo("name.indexed"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().stored(), equalTo(false));
        assertNotSame(IndexOptions.NONE, f.fieldType().indexOptions());

        f = doc.getField("name.not_indexed");
        assertThat(f.name(), equalTo("name.not_indexed"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().stored(), equalTo(true));
        assertEquals(IndexOptions.NONE, f.fieldType().indexOptions());

        f = doc.getField("object1.multi1");
        assertThat(f.name(), equalTo("object1.multi1"));

        f = doc.getField("object1.multi1.string");
        assertThat(f.name(), equalTo("object1.multi1.string"));
        assertThat(f.stringValue(), equalTo("2010-01-01"));

        assertThat(docMapper.mappers().getMapper("name"), notNullValue());
        assertThat(docMapper.mappers().getMapper("name"), instanceOf(StringFieldMapper.class));
        assertNotSame(IndexOptions.NONE, docMapper.mappers().getMapper("name").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("name").fieldType().stored(), equalTo(true));
        assertThat(docMapper.mappers().getMapper("name").fieldType().tokenized(), equalTo(true));

        assertThat(docMapper.mappers().getMapper("name.indexed"), notNullValue());
        assertThat(docMapper.mappers().getMapper("name.indexed"), instanceOf(StringFieldMapper.class));
        assertNotSame(IndexOptions.NONE, docMapper.mappers().getMapper("name.indexed").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("name.indexed").fieldType().stored(), equalTo(false));
        assertThat(docMapper.mappers().getMapper("name.indexed").fieldType().tokenized(), equalTo(true));

        assertThat(docMapper.mappers().getMapper("name.not_indexed"), notNullValue());
        assertThat(docMapper.mappers().getMapper("name.not_indexed"), instanceOf(StringFieldMapper.class));
        assertEquals(IndexOptions.NONE, docMapper.mappers().getMapper("name.not_indexed").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("name.not_indexed").fieldType().stored(), equalTo(true));
        assertThat(docMapper.mappers().getMapper("name.not_indexed").fieldType().tokenized(), equalTo(true));

        assertThat(docMapper.mappers().getMapper("name.test1"), notNullValue());
        assertThat(docMapper.mappers().getMapper("name.test1"), instanceOf(StringFieldMapper.class));
        assertNotSame(IndexOptions.NONE, docMapper.mappers().getMapper("name.test1").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("name.test1").fieldType().stored(), equalTo(true));
        assertThat(docMapper.mappers().getMapper("name.test1").fieldType().tokenized(), equalTo(true));
        assertThat(docMapper.mappers().getMapper("name.test1").fieldType().fieldDataType().getLoading(), equalTo(MappedFieldType.Loading.EAGER));

        assertThat(docMapper.mappers().getMapper("name.test2"), notNullValue());
        assertThat(docMapper.mappers().getMapper("name.test2"), instanceOf(TokenCountFieldMapper.class));
        assertNotSame(IndexOptions.NONE, docMapper.mappers().getMapper("name.test2").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("name.test2").fieldType().stored(), equalTo(true));
        assertThat(docMapper.mappers().getMapper("name.test2").fieldType().tokenized(), equalTo(false));
        assertThat(((TokenCountFieldMapper) docMapper.mappers().getMapper("name.test2")).analyzer(), equalTo("simple"));
        assertThat(((TokenCountFieldMapper) docMapper.mappers().getMapper("name.test2")).analyzer(), equalTo("simple"));

        assertThat(docMapper.mappers().getMapper("object1.multi1"), notNullValue());
        assertThat(docMapper.mappers().getMapper("object1.multi1"), instanceOf(DateFieldMapper.class));
        assertThat(docMapper.mappers().getMapper("object1.multi1.string"), notNullValue());
        assertThat(docMapper.mappers().getMapper("object1.multi1.string"), instanceOf(StringFieldMapper.class));
        assertNotSame(IndexOptions.NONE, docMapper.mappers().getMapper("object1.multi1.string").fieldType().indexOptions());
        assertThat(docMapper.mappers().getMapper("object1.multi1.string").fieldType().tokenized(), equalTo(false));
    }

    public void testBuildThenParse() throws Exception {
        IndexService indexService = createIndex("test");

        DocumentMapper builderDocMapper = doc(rootObject("person").add(
                stringField("name").store(true)
                        .addMultiField(stringField("indexed").index(true).tokenized(true))
                        .addMultiField(stringField("not_indexed").index(false).store(true))
        ), indexService.mapperService()).build(indexService.mapperService());

        String builtMapping = builderDocMapper.mappingSource().string();
//        System.out.println(builtMapping);
        // reparse it
        DocumentMapper docMapper = indexService.mapperService().documentMapperParser().parse("person", new CompressedXContent(builtMapping));


        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/multifield/test-data.json"));
        Document doc = docMapper.parse("test", "person", "1", json).rootDoc();

        IndexableField f = doc.getField("name");
        assertThat(f.name(), equalTo("name"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().stored(), equalTo(true));
        assertNotSame(IndexOptions.NONE, f.fieldType().indexOptions());

        f = doc.getField("name.indexed");
        assertThat(f.name(), equalTo("name.indexed"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().tokenized(), equalTo(true));
        assertThat(f.fieldType().stored(), equalTo(false));
        assertNotSame(IndexOptions.NONE, f.fieldType().indexOptions());

        f = doc.getField("name.not_indexed");
        assertThat(f.name(), equalTo("name.not_indexed"));
        assertThat(f.stringValue(), equalTo("some name"));
        assertThat(f.fieldType().stored(), equalTo(true));
        assertEquals(IndexOptions.NONE, f.fieldType().indexOptions());
    }

    // The underlying order of the fields in multi fields in the mapping source should always be consistent, if not this
    // can to unnecessary re-syncing of the mappings between the local instance and cluster state
    public void testMultiFieldsInConsistentOrder() throws Exception {
        String[] multiFieldNames = new String[randomIntBetween(2, 10)];
        for (int i = 0; i < multiFieldNames.length; i++) {
            multiFieldNames[i] = randomAsciiOfLength(4);
        }

        XContentBuilder builder = jsonBuilder().startObject().startObject("type").startObject("properties")
                .startObject("my_field").field("type", "string").startObject("fields");
        for (String multiFieldName : multiFieldNames) {
            builder = builder.startObject(multiFieldName).field("type", "string").endObject();
        }
        builder = builder.endObject().endObject().endObject().endObject().endObject();
        String mapping = builder.string();
        DocumentMapper docMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(mapping));
        Arrays.sort(multiFieldNames);

        Map<String, Object> sourceAsMap = XContentHelper.convertToMap(docMapper.mappingSource().compressedReference(), true).v2();
        @SuppressWarnings("unchecked")
        Map<String, Object> multiFields = (Map<String, Object>) XContentMapValues.extractValue("type.properties.my_field.fields", sourceAsMap);
        assertThat(multiFields.size(), equalTo(multiFieldNames.length));

        int i = 0;
        // underlying map is LinkedHashMap, so this ok:
        for (String field : multiFields.keySet()) {
            assertThat(field, equalTo(multiFieldNames[i++]));
        }
    }

    // The fielddata settings need to be the same after deserializing/re-serialsing, else unneccesary mapping sync's can be triggered
    public void testMultiFieldsFieldDataSettingsInConsistentOrder() throws Exception {
        final String MY_MULTI_FIELD = "multi_field";

        // Possible fielddata settings
        Map<String, Object> possibleSettings = new TreeMap<String, Object>();
        possibleSettings.put("filter.frequency.min", 1);
        possibleSettings.put("filter.frequency.max", 2);
        possibleSettings.put("filter.regex.pattern", ".*");
        possibleSettings.put("loading", "eager");
        possibleSettings.put("foo", "bar");
        possibleSettings.put("zetting", "zValue");
        possibleSettings.put("aSetting", "aValue");

        // Generate a mapping with the a random subset of possible fielddata settings
        XContentBuilder builder = jsonBuilder().startObject().startObject("type").startObject("properties")
            .startObject("my_field").field("type", "string").startObject("fields").startObject(MY_MULTI_FIELD)
            .field("type", "string").startObject("fielddata");
        String[] keys = possibleSettings.keySet().toArray(new String[]{});
        Collections.shuffle(Arrays.asList(keys), random());
        for(int i = randomIntBetween(0, possibleSettings.size()-1); i >= 0; --i)
            builder.field(keys[i], possibleSettings.get(keys[i]));
        builder.endObject().endObject().endObject().endObject().endObject().endObject().endObject();

        // Check the mapping remains identical when deserialed/re-serialsed
        final DocumentMapperParser parser = createIndex("test").mapperService().documentMapperParser();
        DocumentMapper docMapper = parser.parse("type", new CompressedXContent(builder.string()));
        DocumentMapper docMapper2 = parser.parse("type", docMapper.mappingSource());
        assertThat(docMapper.mappingSource(), equalTo(docMapper2.mappingSource()));
    }

    public void testObjectFieldNotAllowed() throws Exception {
        String mapping = jsonBuilder().startObject().startObject("type").startObject("properties").startObject("my_field")
            .field("type", "string").startObject("fields").startObject("multi").field("type", "object").endObject().endObject()
            .endObject().endObject().endObject().endObject().string();
        final DocumentMapperParser parser = createIndex("test").mapperService().documentMapperParser();
        try {
            parser.parse("type", new CompressedXContent(mapping));
            fail("expected mapping parse failure");
        } catch (MapperParsingException e) {
            assertTrue(e.getMessage().contains("cannot be used in multi field"));
        }
    }

    public void testNestedFieldNotAllowed() throws Exception {
        String mapping = jsonBuilder().startObject().startObject("type").startObject("properties").startObject("my_field")
            .field("type", "string").startObject("fields").startObject("multi").field("type", "nested").endObject().endObject()
            .endObject().endObject().endObject().endObject().string();
        final DocumentMapperParser parser = createIndex("test").mapperService().documentMapperParser();
        try {
            parser.parse("type", new CompressedXContent(mapping));
            fail("expected mapping parse failure");
        } catch (MapperParsingException e) {
            assertTrue(e.getMessage().contains("cannot be used in multi field"));
        }
    }

    public void testMultiFieldWithDot() throws IOException {
        XContentBuilder mapping = jsonBuilder();
        mapping.startObject()
                .startObject("my_type")
                .startObject("properties")
                .startObject("city")
                .field("type", "string")
                .startObject("fields")
                .startObject("raw.foo")
                .field("type", "string")
                .field("index", "not_analyzed")
                .endObject()
                .endObject()
                .endObject()
                .endObject()
                .endObject();

        MapperService mapperService = createIndex("test").mapperService();
        try {
            mapperService.documentMapperParser().parse("my_type", new CompressedXContent(mapping.string()));
            fail("this should throw an exception because one field contains a dot");
        } catch (MapperParsingException e) {
            assertThat(e.getMessage(), equalTo("Field name [raw.foo] which is a multi field of [city] cannot contain '.'"));
        }
    }
}
