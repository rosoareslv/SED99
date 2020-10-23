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

import org.apache.lucene.index.IndexableField;
import org.apache.lucene.spatial.geopoint.document.GeoPointField;
import org.apache.lucene.util.BytesRef;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.compress.CompressedXContent;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.index.IndexService;
import org.elasticsearch.indices.mapper.MapperRegistry;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.test.ESSingleNodeTestCase;
import org.elasticsearch.test.InternalSettingsPlugin;
import org.elasticsearch.test.VersionUtils;

import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;

/**
 */
public class ExternalFieldMapperTests extends ESSingleNodeTestCase {

    @Override
    protected Collection<Class<? extends Plugin>> getPlugins() {
        return pluginList(InternalSettingsPlugin.class);
    }

    public void testExternalValues() throws Exception {
        Version version = VersionUtils.randomVersionBetween(random(), Version.V_2_0_0, Version.CURRENT);
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        IndexService indexService = createIndex("test", settings);
        MapperRegistry mapperRegistry = new MapperRegistry(
                Collections.singletonMap(ExternalMapperPlugin.EXTERNAL, new ExternalMapper.TypeParser(ExternalMapperPlugin.EXTERNAL, "foo")),
                Collections.singletonMap(ExternalMetadataMapper.CONTENT_TYPE, new ExternalMetadataMapper.TypeParser()));

        DocumentMapperParser parser = new DocumentMapperParser(indexService.getIndexSettings(), indexService.mapperService(),
                indexService.analysisService(), indexService.similarityService(), mapperRegistry, indexService::newQueryShardContext);
        DocumentMapper documentMapper = parser.parse("type", new CompressedXContent(
                XContentFactory.jsonBuilder().startObject().startObject("type")
                .startObject(ExternalMetadataMapper.CONTENT_TYPE)
                .endObject()
                .startObject("properties")
                    .startObject("field").field("type", "external").endObject()
                .endObject()
            .endObject().endObject().string()
        ));

        ParsedDocument doc = documentMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                    .field("field", "1234")
                .endObject()
                .bytes());

        assertThat(doc.rootDoc().getField("field.bool"), notNullValue());
        assertThat(doc.rootDoc().getField("field.bool").stringValue(), is("T"));

        assertThat(doc.rootDoc().getField("field.point"), notNullValue());
        if (version.before(Version.V_2_2_0)) {
            assertThat(doc.rootDoc().getField("field.point").stringValue(), is("42.0,51.0"));
        } else {
            assertThat(Long.parseLong(doc.rootDoc().getField("field.point").stringValue()), is(GeoPointField.encodeLatLon(42.0, 51.0)));
        }

        assertThat(doc.rootDoc().getField("field.shape"), notNullValue());

        assertThat(doc.rootDoc().getField("field.field"), notNullValue());
        assertThat(doc.rootDoc().getField("field.field").stringValue(), is("foo"));

        assertThat(doc.rootDoc().getField(ExternalMetadataMapper.FIELD_NAME).stringValue(), is(ExternalMetadataMapper.FIELD_VALUE));

    }

    public void testExternalValuesWithMultifield() throws Exception {
        Version version = VersionUtils.randomVersionBetween(random(), Version.V_2_0_0, Version.CURRENT);
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        IndexService indexService = createIndex("test", settings);
        Map<String, Mapper.TypeParser> mapperParsers = new HashMap<>();
        mapperParsers.put(ExternalMapperPlugin.EXTERNAL, new ExternalMapper.TypeParser(ExternalMapperPlugin.EXTERNAL, "foo"));
        mapperParsers.put(TextFieldMapper.CONTENT_TYPE, new TextFieldMapper.TypeParser());
        mapperParsers.put(KeywordFieldMapper.CONTENT_TYPE, new KeywordFieldMapper.TypeParser());
        MapperRegistry mapperRegistry = new MapperRegistry(mapperParsers, Collections.emptyMap());

        DocumentMapperParser parser = new DocumentMapperParser(indexService.getIndexSettings(), indexService.mapperService(),
                indexService.analysisService(), indexService.similarityService(), mapperRegistry, indexService::newQueryShardContext);

        DocumentMapper documentMapper = parser.parse("type", new CompressedXContent(
                XContentFactory.jsonBuilder().startObject().startObject("type").startObject("properties")
                .startObject("field")
                    .field("type", ExternalMapperPlugin.EXTERNAL)
                    .startObject("fields")
                        .startObject("field")
                            .field("type", "text")
                            .field("store", true)
                            .startObject("fields")
                                .startObject("raw")
                                    .field("type", "keyword")
                                    .field("store", true)
                                .endObject()
                            .endObject()
                        .endObject()
                    .endObject()
                .endObject()
                .endObject().endObject().endObject()
                .string()));

        ParsedDocument doc = documentMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                    .field("field", "1234")
                .endObject()
                .bytes());

        assertThat(doc.rootDoc().getField("field.bool"), notNullValue());
        assertThat(doc.rootDoc().getField("field.bool").stringValue(), is("T"));

        assertThat(doc.rootDoc().getField("field.point"), notNullValue());
        if (version.before(Version.V_2_2_0)) {
            assertThat(doc.rootDoc().getField("field.point").stringValue(), is("42.0,51.0"));
        } else {
            assertThat(Long.parseLong(doc.rootDoc().getField("field.point").stringValue()), is(GeoPointField.encodeLatLon(42.0, 51.0)));
        }

        IndexableField shape = doc.rootDoc().getField("field.shape");
        assertThat(shape, notNullValue());

        IndexableField field = doc.rootDoc().getField("field.field");
        assertThat(field, notNullValue());
        assertThat(field.stringValue(), is("foo"));

        IndexableField raw = doc.rootDoc().getField("field.field.raw");

        assertThat(raw, notNullValue());
        if (version.before(Version.V_5_0_0_alpha1)) {
            assertThat(raw.stringValue(), is("foo"));
        } else {
            assertThat(raw.binaryValue(), is(new BytesRef("foo")));
        }
    }

    public void testExternalValuesWithMultifieldTwoLevels() throws Exception {
        Version version = VersionUtils.randomVersionBetween(random(), Version.V_2_0_0, Version.CURRENT);
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        IndexService indexService = createIndex("test", settings);
        Map<String, Mapper.TypeParser> mapperParsers = new HashMap<>();
        mapperParsers.put(ExternalMapperPlugin.EXTERNAL, new ExternalMapper.TypeParser(ExternalMapperPlugin.EXTERNAL, "foo"));
        mapperParsers.put(ExternalMapperPlugin.EXTERNAL_BIS, new ExternalMapper.TypeParser(ExternalMapperPlugin.EXTERNAL, "bar"));
        mapperParsers.put(TextFieldMapper.CONTENT_TYPE, new TextFieldMapper.TypeParser());
        MapperRegistry mapperRegistry = new MapperRegistry(mapperParsers, Collections.emptyMap());

        DocumentMapperParser parser = new DocumentMapperParser(indexService.getIndexSettings(), indexService.mapperService(),
                indexService.analysisService(), indexService.similarityService(), mapperRegistry, indexService::newQueryShardContext);

        DocumentMapper documentMapper = parser.parse("type", new CompressedXContent(
                XContentFactory.jsonBuilder().startObject().startObject("type").startObject("properties")
                .startObject("field")
                    .field("type", ExternalMapperPlugin.EXTERNAL)
                    .startObject("fields")
                        .startObject("field")
                            .field("type", "text")
                            .startObject("fields")
                                .startObject("generated")
                                    .field("type", ExternalMapperPlugin.EXTERNAL_BIS)
                                .endObject()
                                .startObject("raw")
                                    .field("type", "text")
                                .endObject()
                            .endObject()
                        .endObject()
                        .startObject("raw")
                            .field("type", "text")
                        .endObject()
                    .endObject()
                .endObject()
                .endObject().endObject().endObject()
                .string()));

        ParsedDocument doc = documentMapper.parse("test", "type", "1", XContentFactory.jsonBuilder()
                .startObject()
                .field("field", "1234")
                .endObject()
                .bytes());

        assertThat(doc.rootDoc().getField("field.bool"), notNullValue());
        assertThat(doc.rootDoc().getField("field.bool").stringValue(), is("T"));

        assertThat(doc.rootDoc().getField("field.point"), notNullValue());
        if (version.before(Version.V_2_2_0)) {
            assertThat(doc.rootDoc().getField("field.point").stringValue(), is("42.0,51.0"));
        } else {
            assertThat(Long.parseLong(doc.rootDoc().getField("field.point").stringValue()), is(GeoPointField.encodeLatLon(42.0, 51.0)));
        }

        assertThat(doc.rootDoc().getField("field.shape"), notNullValue());

        assertThat(doc.rootDoc().getField("field.field"), notNullValue());
        assertThat(doc.rootDoc().getField("field.field").stringValue(), is("foo"));

        assertThat(doc.rootDoc().getField("field.field.generated.generated"), notNullValue());
        assertThat(doc.rootDoc().getField("field.field.generated.generated").stringValue(), is("bar"));

        assertThat(doc.rootDoc().getField("field.field.raw"), notNullValue());
        assertThat(doc.rootDoc().getField("field.field.raw").stringValue(), is("foo"));

        assertThat(doc.rootDoc().getField("field.raw"), notNullValue());
        assertThat(doc.rootDoc().getField("field.raw").stringValue(), is("foo"));
    }
}
