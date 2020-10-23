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

package org.elasticsearch.index.mapper;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.apache.lucene.index.IndexableField;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.compress.CompressedXContent;
import org.elasticsearch.common.lucene.all.AllField;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.index.mapper.ParseContext.Document;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.test.ESSingleNodeTestCase;
import org.elasticsearch.test.InternalSettingsPlugin;

import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.elasticsearch.test.StreamsUtils.copyToBytesFromClasspath;
import static org.elasticsearch.test.StreamsUtils.copyToStringFromClasspath;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;

// TODO: make this a real unit test
public class DocumentParserTests extends ESSingleNodeTestCase {

    @Override
    protected Collection<Class<? extends Plugin>> getPlugins() {
        return pluginList(InternalSettingsPlugin.class);
    }

    public void testTypeDisabled() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .field("enabled", false).endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo")
            .field("field", "1234")
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNull(doc.rootDoc().getField("field"));
        assertNotNull(doc.rootDoc().getField(UidFieldMapper.NAME));
    }

    public void testFieldDisabled() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").startObject("properties")
            .startObject("foo").field("enabled", false).endObject()
            .startObject("bar").field("type", "integer").endObject()
            .endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject()
            .field("foo", "1234")
            .field("bar", 10)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNull(doc.rootDoc().getField("foo"));
        assertNotNull(doc.rootDoc().getField("bar"));
        assertNotNull(doc.rootDoc().getField(UidFieldMapper.NAME));
    }

    public void testDotsWithExistingMapper() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").startObject("properties")
            .startObject("foo").startObject("properties")
            .startObject("bar").startObject("properties")
            .startObject("baz").field("type", "integer")
            .endObject().endObject().endObject().endObject().endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject()
            .field("foo.bar.baz", 123)
            .startObject("foo")
            .field("bar.baz", 456)
            .endObject()
            .startObject("foo.bar")
            .field("baz", 789)
            .endObject()
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNull(doc.dynamicMappingsUpdate()); // no update!
        String[] values = doc.rootDoc().getValues("foo.bar.baz");
        assertEquals(3, values.length);
        assertEquals("123", values[0]);
        assertEquals("456", values[1]);
        assertEquals("789", values[2]);
    }

    public void testPropagateDynamicWithExistingMapper() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .field("dynamic", false)
            .startObject("properties")
                .startObject("foo")
                    .field("type", "object")
                    .field("dynamic", true)
                    .startObject("properties")
            .endObject().endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));
        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo")
            .field("bar", "something")
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNotNull(doc.dynamicMappingsUpdate());
        assertNotNull(doc.rootDoc().getField("foo.bar"));
    }

    public void testPropagateDynamicWithDynamicMapper() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .field("dynamic", false)
            .startObject("properties")
            .startObject("foo")
            .field("type", "object")
            .field("dynamic", true)
            .startObject("properties")
            .endObject().endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));
        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo").startObject("bar")
                .field("baz", "something")
            .endObject().endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNotNull(doc.dynamicMappingsUpdate());
        assertNotNull(doc.rootDoc().getField("foo.bar.baz"));
    }

    public void testDynamicRootFallback() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .field("dynamic", false)
            .startObject("properties")
            .startObject("foo")
            .field("type", "object")
            .startObject("properties")
            .endObject().endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));
        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo")
            .field("bar", "something")
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertNull(doc.dynamicMappingsUpdate());
        assertNull(doc.rootDoc().getField("foo.bar"));
    }

    DocumentMapper createDummyMapping(MapperService mapperService) throws Exception {
        String mapping = jsonBuilder().startObject().startObject("type").startObject("properties")
            .startObject("y").field("type", "object").endObject()
            .startObject("x").startObject("properties")
            .startObject("subx").field("type", "object").startObject("properties")
            .startObject("subsubx").field("type", "object")
            .endObject().endObject().endObject().endObject().endObject().endObject().endObject().endObject().string();

        DocumentMapper defaultMapper = mapperService.documentMapperParser().parse("type", new CompressedXContent(mapping));
        return defaultMapper;
    }

    // creates an object mapper, which is about 100x harder than it should be....
    ObjectMapper createObjectMapper(MapperService mapperService, String name) throws Exception {
        ParseContext context = new ParseContext.InternalParseContext(
            Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT).build(),
            mapperService.documentMapperParser(), mapperService.documentMapper("type"), null, null);
        String[] nameParts = name.split("\\.");
        for (int i = 0; i < nameParts.length - 1; ++i) {
            context.path().add(nameParts[i]);
        }
        Mapper.Builder builder = new ObjectMapper.Builder(nameParts[nameParts.length - 1]).enabled(true);
        Mapper.BuilderContext builderContext = new Mapper.BuilderContext(context.indexSettings(), context.path());
        return (ObjectMapper)builder.build(builderContext);
    }

    public void testEmptyMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        assertNull(DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, Collections.emptyList()));
    }

    public void testSingleMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        List<Mapper> updates = Collections.singletonList(new MockFieldMapper("foo"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        assertNotNull(mapping.root().getMapper("foo"));
    }

    public void testSubfieldMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        List<Mapper> updates = Collections.singletonList(new MockFieldMapper("x.foo"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        Mapper xMapper = mapping.root().getMapper("x");
        assertNotNull(xMapper);
        assertTrue(xMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)xMapper).getMapper("foo"));
        assertNull(((ObjectMapper)xMapper).getMapper("subx"));
    }

    public void testMultipleSubfieldMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        List<Mapper> updates = new ArrayList<>();
        updates.add(new MockFieldMapper("x.foo"));
        updates.add(new MockFieldMapper("x.bar"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        Mapper xMapper = mapping.root().getMapper("x");
        assertNotNull(xMapper);
        assertTrue(xMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)xMapper).getMapper("foo"));
        assertNotNull(((ObjectMapper)xMapper).getMapper("bar"));
        assertNull(((ObjectMapper)xMapper).getMapper("subx"));
    }

    public void testDeepSubfieldMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        List<Mapper> updates = Collections.singletonList(new MockFieldMapper("x.subx.foo"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        Mapper xMapper = mapping.root().getMapper("x");
        assertNotNull(xMapper);
        assertTrue(xMapper instanceof ObjectMapper);
        Mapper subxMapper = ((ObjectMapper)xMapper).getMapper("subx");
        assertTrue(subxMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)subxMapper).getMapper("foo"));
        assertNull(((ObjectMapper)subxMapper).getMapper("subsubx"));
    }

    public void testDeepSubfieldAfterSubfieldMappingUpdate() throws Exception {
        DocumentMapper docMapper = createDummyMapping(createIndex("test").mapperService());
        List<Mapper> updates = new ArrayList<>();
        updates.add(new MockFieldMapper("x.a"));
        updates.add(new MockFieldMapper("x.subx.b"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        Mapper xMapper = mapping.root().getMapper("x");
        assertNotNull(xMapper);
        assertTrue(xMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)xMapper).getMapper("a"));
        Mapper subxMapper = ((ObjectMapper)xMapper).getMapper("subx");
        assertTrue(subxMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)subxMapper).getMapper("b"));
    }

    public void testObjectMappingUpdate() throws Exception {
        MapperService mapperService = createIndex("test").mapperService();
        DocumentMapper docMapper = createDummyMapping(mapperService);
        List<Mapper> updates = new ArrayList<>();
        updates.add(createObjectMapper(mapperService, "foo"));
        updates.add(createObjectMapper(mapperService, "foo.bar"));
        updates.add(new MockFieldMapper("foo.bar.baz"));
        updates.add(new MockFieldMapper("foo.field"));
        Mapping mapping = DocumentParser.createDynamicUpdate(docMapper.mapping(), docMapper, updates);
        Mapper fooMapper = mapping.root().getMapper("foo");
        assertNotNull(fooMapper);
        assertTrue(fooMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)fooMapper).getMapper("field"));
        Mapper barMapper = ((ObjectMapper)fooMapper).getMapper("bar");
        assertTrue(barMapper instanceof ObjectMapper);
        assertNotNull(((ObjectMapper)barMapper).getMapper("baz"));
    }

    public void testDynamicGeoPointArrayWithTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "geo_point").endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .startArray().value(0).value(0).endArray()
                .startArray().value(1).value(1).endArray()
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicLongArrayWithTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "long").endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicFalseLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicStrictLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [foo] within [type] is not allowed", exception.getMessage());
    }

    public void testMappedGeoPointArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
                .startObject("properties").startObject("foo").field("type", "geo_point")
                .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .startArray().value(0).value(0).endArray()
                .startArray().value(1).value(1).endArray()
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo").length);
    }

    public void testMappedLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
                .startObject("properties").startObject("foo").field("type", "long")
                .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicObjectWithTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "object")
                .startObject("properties").startObject("bar").field("type", "keyword").endObject().endObject().endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startObject("foo")
                    .field("bar", "baz")
                .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar").length);
    }

    public void testDynamicFalseObject() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo")
                .field("bar", "baz")
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo.bar").length);
    }

    public void testDynamicStrictObject() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startObject("foo")
                    .field("bar", "baz")
                .endObject().endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [foo] within [type] is not allowed", exception.getMessage());
    }

    public void testDynamicFalseValue() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject()
                .field("bar", "baz")
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("bar").length);
    }

    public void testDynamicStrictValue() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject()
                    .field("bar", "baz")
                .endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [bar] within [type] is not allowed", exception.getMessage());
    }

    public void testDynamicFalseNull() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject()
                .field("bar", (String) null)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("bar").length);
    }

    public void testDynamicStrictNull() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject()
                .field("bar", (String) null)
                .endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [bar] within [type] is not allowed", exception.getMessage());
    }

    public void testMappedNullValue() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
                .startObject("properties").startObject("foo").field("type", "long")
                .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().field("foo", (Long) null)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo").length);
    }

    public void testDynamicDottedFieldNameLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongArrayWithParentTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "object").endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongArrayWithExistingParent() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("properties") .startObject("foo")
            .field("type", "object")
            .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(4, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongArrayWithExistingParentWrongType() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("properties") .startObject("foo")
            .field("type", "long")
            .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        MapperParsingException exception = expectThrows(MapperParsingException.class, () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("Could not dynamically add mapping for field [foo.bar.baz]. "
                + "Existing mapping for [foo] must be of type object but found [long].", exception.getMessage());
    }

    public void testDynamicFalseDottedFieldNameLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo.bar.baz").length);
    }

    public void testDynamicStrictDottedFieldNameLongArray() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startArray("foo.bar.baz")
                .value(0)
                .value(1)
            .endArray().endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [foo] within [type] is not allowed", exception.getMessage());
    }

    public void testDynamicDottedFieldNameLong() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongWithParentTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "object").endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongWithExistingParent() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("properties") .startObject("foo")
            .field("type", "object")
            .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameLongWithExistingParentWrongType() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("properties") .startObject("foo")
            .field("type", "long")
            .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        MapperParsingException exception = expectThrows(MapperParsingException.class, () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("Could not dynamically add mapping for field [foo.bar.baz]. "
                + "Existing mapping for [foo] must be of type object but found [long].", exception.getMessage());
    }

    public void testDynamicFalseDottedFieldNameLong() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo.bar.baz").length);
    }

    public void testDynamicStrictDottedFieldNameLong() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().field("foo.bar.baz", 0)
            .endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [foo] within [type] is not allowed", exception.getMessage());
    }

    public void testDynamicDottedFieldNameObject() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
            .startObject().startObject("foo.bar.baz")
                .field("a", 0)
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz.a").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(ObjectMapper.class));
        Mapper aMapper = ((ObjectMapper) bazMapper).getMapper("a");
        assertNotNull(aMapper);
        assertThat(aMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameObjectWithParentTemplate() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startArray("dynamic_templates").startObject().startObject("georule")
                .field("match", "foo*")
                .startObject("mapping").field("type", "object").endObject()
            .endObject().endObject().endArray().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startObject("foo.bar.baz")
                .field("a", 0)
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz.a").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(ObjectMapper.class));
        Mapper aMapper = ((ObjectMapper) bazMapper).getMapper("a");
        assertNotNull(aMapper);
        assertThat(aMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameObjectWithExistingParent() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").startObject("properties").startObject("foo")
                .field("type", "object").endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder().startObject().startObject("foo.bar.baz").field("a", 0).endObject().endObject()
                .bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(2, doc.rootDoc().getFields("foo.bar.baz.a").length);
        Mapper fooMapper = doc.dynamicMappingsUpdate().root().getMapper("foo");
        assertNotNull(fooMapper);
        assertThat(fooMapper, instanceOf(ObjectMapper.class));
        Mapper barMapper = ((ObjectMapper) fooMapper).getMapper("bar");
        assertNotNull(barMapper);
        assertThat(barMapper, instanceOf(ObjectMapper.class));
        Mapper bazMapper = ((ObjectMapper) barMapper).getMapper("baz");
        assertNotNull(bazMapper);
        assertThat(bazMapper, instanceOf(ObjectMapper.class));
        Mapper aMapper = ((ObjectMapper) bazMapper).getMapper("a");
        assertNotNull(aMapper);
        assertThat(aMapper, instanceOf(NumberFieldMapper.class));
    }

    public void testDynamicDottedFieldNameObjectWithExistingParentWrongType() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type")
            .startObject("properties") .startObject("foo")
            .field("type", "long")
            .endObject().endObject().endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder().startObject().startObject("foo.bar.baz").field("a", 0).endObject().endObject()
                .bytes();
        MapperParsingException exception = expectThrows(MapperParsingException.class, () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("Could not dynamically add mapping for field [foo.bar.baz]. "
                + "Existing mapping for [foo] must be of type object but found [long].", exception.getMessage());
    }

    public void testDynamicFalseDottedFieldNameObject() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "false")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startObject("foo.bar.baz")
                .field("a", 0)
            .endObject().endObject().bytes();
        ParsedDocument doc = mapper.parse("test", "type", "1", bytes);
        assertEquals(0, doc.rootDoc().getFields("foo.bar.baz.a").length);
    }

    public void testDynamicStrictDottedFieldNameObject() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").field("dynamic", "strict")
            .endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder()
                .startObject().startObject("foo.bar.baz")
                .field("a", 0)
            .endObject().endObject().bytes();
        StrictDynamicMappingException exception = expectThrows(StrictDynamicMappingException.class,
                () -> mapper.parse("test", "type", "1", bytes));
        assertEquals("mapping set to strict, dynamic introduction of [foo] within [type] is not allowed", exception.getMessage());
    }

    public void testDocumentContainsMetadataField() throws Exception {
        DocumentMapperParser mapperParser = createIndex("test").mapperService().documentMapperParser();
        String mapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();
        DocumentMapper mapper = mapperParser.parse("type", new CompressedXContent(mapping));

        BytesReference bytes = XContentFactory.jsonBuilder().startObject().field("_ttl", 0).endObject().bytes();
        MapperParsingException e = expectThrows(MapperParsingException.class, () ->
            mapper.parse("test", "type", "1", bytes)
        );
        assertTrue(e.getMessage(), e.getMessage().contains("cannot be added inside a document"));

        BytesReference bytes2 = XContentFactory.jsonBuilder().startObject().field("foo._ttl", 0).endObject().bytes();
        mapper.parse("test", "type", "1", bytes2); // parses without error
    }

    public void testSimpleMapper() throws Exception {
        IndexService indexService = createIndex("test");
        DocumentMapper docMapper = new DocumentMapper.Builder(
                new RootObjectMapper.Builder("person")
                        .add(new ObjectMapper.Builder("name").add(new TextFieldMapper.Builder("first").store(true).index(false))),
            indexService.mapperService()).build(indexService.mapperService());

        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/simple/test1.json"));
        Document doc = docMapper.parse("test", "person", "1", json).rootDoc();

        assertThat(doc.get(docMapper.mappers().getMapper("name.first").fieldType().name()), equalTo("shay"));
        doc = docMapper.parse("test", "person", "1", json).rootDoc();
    }

    public void testParseToJsonAndParse() throws Exception {
        String mapping = copyToStringFromClasspath("/org/elasticsearch/index/mapper/simple/test-mapping.json");
        DocumentMapperParser parser = createIndex("test").mapperService().documentMapperParser();
        DocumentMapper docMapper = parser.parse("person", new CompressedXContent(mapping));
        String builtMapping = docMapper.mappingSource().string();
        // reparse it
        DocumentMapper builtDocMapper = parser.parse("person", new CompressedXContent(builtMapping));
        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/simple/test1.json"));
        Document doc = builtDocMapper.parse("test", "person", "1", json).rootDoc();
        assertThat(doc.get(docMapper.uidMapper().fieldType().name()), equalTo(Uid.createUid("person", "1")));
        assertThat(doc.get(docMapper.mappers().getMapper("name.first").fieldType().name()), equalTo("shay"));
    }

    public void testSimpleParser() throws Exception {
        String mapping = copyToStringFromClasspath("/org/elasticsearch/index/mapper/simple/test-mapping.json");
        DocumentMapper docMapper = createIndex("test").mapperService().documentMapperParser().parse("person", new CompressedXContent(mapping));

        assertThat((String) docMapper.meta().get("param1"), equalTo("value1"));

        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/simple/test1.json"));
        Document doc = docMapper.parse("test", "person", "1", json).rootDoc();
        assertThat(doc.get(docMapper.uidMapper().fieldType().name()), equalTo(Uid.createUid("person", "1")));
        assertThat(doc.get(docMapper.mappers().getMapper("name.first").fieldType().name()), equalTo("shay"));
    }

    public void testSimpleParserNoTypeNoId() throws Exception {
        String mapping = copyToStringFromClasspath("/org/elasticsearch/index/mapper/simple/test-mapping.json");
        DocumentMapper docMapper = createIndex("test").mapperService().documentMapperParser().parse("person", new CompressedXContent(mapping));
        BytesReference json = new BytesArray(copyToBytesFromClasspath("/org/elasticsearch/index/mapper/simple/test1-notype-noid.json"));
        Document doc = docMapper.parse("test", "person", "1", json).rootDoc();
        assertThat(doc.get(docMapper.uidMapper().fieldType().name()), equalTo(Uid.createUid("person", "1")));
        assertThat(doc.get(docMapper.mappers().getMapper("name.first").fieldType().name()), equalTo("shay"));
    }

    public void testAttributes() throws Exception {
        String mapping = copyToStringFromClasspath("/org/elasticsearch/index/mapper/simple/test-mapping.json");
        DocumentMapperParser parser = createIndex("test").mapperService().documentMapperParser();
        DocumentMapper docMapper = parser.parse("person", new CompressedXContent(mapping));

        assertThat((String) docMapper.meta().get("param1"), equalTo("value1"));

        String builtMapping = docMapper.mappingSource().string();
        DocumentMapper builtDocMapper = parser.parse("person", new CompressedXContent(builtMapping));
        assertThat((String) builtDocMapper.meta().get("param1"), equalTo("value1"));
    }

    public void testNoDocumentSent() throws Exception {
        IndexService indexService = createIndex("test");
        DocumentMapper docMapper = new DocumentMapper.Builder(
                new RootObjectMapper.Builder("person")
                        .add(new ObjectMapper.Builder("name").add(new TextFieldMapper.Builder("first").store(true).index(false))),
            indexService.mapperService()).build(indexService.mapperService());

        BytesReference json = new BytesArray("".getBytes(StandardCharsets.UTF_8));
        try {
            docMapper.parse("test", "person", "1", json).rootDoc();
            fail("this point is never reached");
        } catch (MapperParsingException e) {
            assertThat(e.getMessage(), equalTo("failed to parse, document is empty"));
        }
    }

    public void testNoLevel() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject()
                .bytes());

        assertThat(doc.rootDoc().get("test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("inner.inner_field"), equalTo("inner_value"));
    }

    public void testTypeLevel() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testNoLevelWithFieldTypeAsValue() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                .field("type", "value_type")
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type"), equalTo("value_type"));
        assertThat(doc.rootDoc().get("test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("inner.inner_field"), equalTo("inner_value"));
    }

    public void testTypeLevelWithFieldTypeAsValue() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .field("type", "value_type")
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.type"), equalTo("value_type"));
        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testNoLevelWithFieldTypeAsObject() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                .startObject("type").field("type_field", "type_value").endObject()
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject()
                .bytes());

        // in this case, we analyze the type object as the actual document, and ignore the other same level fields
        assertThat(doc.rootDoc().get("type.type_field"), equalTo("type_value"));
        assertThat(doc.rootDoc().get("test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("test2"), equalTo("value2"));
    }

    public void testTypeLevelWithFieldTypeAsObject() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .startObject("type").field("type_field", "type_value").endObject()
                .field("test1", "value1")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.type.type_field"), equalTo("type_value"));
        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testNoLevelWithFieldTypeAsValueNotFirst() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .field("test1", "value1")
                .field("test2", "value2")
                .field("type", "value_type")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.type"), equalTo("value_type"));
        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testTypeLevelWithFieldTypeAsValueNotFirst() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .field("test1", "value1")
                .field("type", "value_type")
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.type"), equalTo("value_type"));
        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testNoLevelWithFieldTypeAsObjectNotFirst() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                .field("test1", "value1")
                .startObject("type").field("type_field", "type_value").endObject()
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject()
                .bytes());

        // when the type is not the first one, we don't confuse it...
        assertThat(doc.rootDoc().get("type.type_field"), equalTo("type_value"));
        assertThat(doc.rootDoc().get("test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("inner.inner_field"), equalTo("inner_value"));
    }

    public void testTypeLevelWithFieldTypeAsObjectNotFirst() throws Exception {
        String defaultMapping = XContentFactory.jsonBuilder().startObject().startObject("type").endObject().endObject().string();

        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));

        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject().startObject("type")
                .field("test1", "value1")
                .startObject("type").field("type_field", "type_value").endObject()
                .field("test2", "value2")
                .startObject("inner").field("inner_field", "inner_value").endObject()
                .endObject().endObject()
                .bytes());

        assertThat(doc.rootDoc().get("type.type.type_field"), equalTo("type_value"));
        assertThat(doc.rootDoc().get("type.test1"), equalTo("value1"));
        assertThat(doc.rootDoc().get("type.test2"), equalTo("value2"));
        assertThat(doc.rootDoc().get("type.inner.inner_field"), equalTo("inner_value"));
    }

    public void testIncludeInAllPropagation() throws IOException {
        String defaultMapping = XContentFactory.jsonBuilder().startObject()
                .startObject("type")
                    .field("dynamic", "strict")
                    .startObject("properties")
                        .startObject("a")
                            .field("type", "keyword")
                        .endObject()
                        .startObject("o")
                            .field("include_in_all", false)
                            .startObject("properties")
                                .startObject("a")
                                    .field("type", "keyword")
                                .endObject()
                                .startObject("o")
                                    .field("include_in_all", true)
                                    .startObject("properties")
                                        .startObject("a")
                                            .field("type", "keyword")
                                        .endObject()
                                    .endObject()
                                .endObject()
                            .endObject()
                        .endObject()
                    .endObject()
                .endObject().endObject().string();
        DocumentMapper defaultMapper = createIndex("test").mapperService().documentMapperParser().parse("type", new CompressedXContent(defaultMapping));
        ParsedDocument doc = defaultMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                    .field("a", "b")
                    .startObject("o")
                        .field("a", "c")
                        .startObject("o")
                            .field("a", "d")
                        .endObject()
                    .endObject()
                .endObject().bytes());
        Set<String> values = new HashSet<>();
        for (IndexableField f : doc.rootDoc().getFields("_all")) {
            values.add(f.stringValue());
        }
        assertEquals(new HashSet<>(Arrays.asList("b", "d")), values);
    }
}
