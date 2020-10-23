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

package org.elasticsearch.action.update;

import org.elasticsearch.Version;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.action.support.replication.ReplicationRequest;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.io.stream.Streamable;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.common.xcontent.json.JsonXContent;
import org.elasticsearch.env.Environment;
import org.elasticsearch.index.get.GetResult;
import org.elasticsearch.index.shard.ShardId;
import org.elasticsearch.script.MockScriptEngine;
import org.elasticsearch.script.Script;
import org.elasticsearch.script.ScriptContextRegistry;
import org.elasticsearch.script.ScriptEngineRegistry;
import org.elasticsearch.script.ScriptService;
import org.elasticsearch.script.ScriptSettings;
import org.elasticsearch.script.ScriptType;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.RandomObjects;
import org.elasticsearch.watcher.ResourceWatcherService;
import org.junit.Before;

import java.io.IOException;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Function;

import static java.util.Collections.emptyList;
import static java.util.Collections.emptyMap;
import static java.util.Collections.singletonList;
import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.elasticsearch.common.xcontent.XContentHelper.toXContent;
import static org.elasticsearch.common.xcontent.XContentHelper.update;
import static org.elasticsearch.script.MockScriptEngine.mockInlineScript;
import static org.elasticsearch.test.hamcrest.ElasticsearchAssertions.assertToXContentEquivalent;
import static org.hamcrest.Matchers.arrayContaining;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.notNullValue;

public class UpdateRequestTests extends ESTestCase {

    private UpdateHelper updateHelper;

    @Before
    public void setUp() throws Exception {
        super.setUp();
        final Path genericConfigFolder = createTempDir();
        final Settings baseSettings = Settings.builder()
                .put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString())
                .put(Environment.PATH_CONF_SETTING.getKey(), genericConfigFolder)
                .build();
        final Environment environment = new Environment(baseSettings);
        final Map<String, Function<Map<String, Object>, Object>> scripts =  new HashMap<>();
        scripts.put(
                "ctx._source.update_timestamp = ctx._now",
                vars -> {
                    @SuppressWarnings("unchecked")
                    final Map<String, Object> ctx = (Map<String, Object>) vars.get("ctx");
                    @SuppressWarnings("unchecked")
                    final Map<String, Object> source = (Map<String, Object>) ctx.get("_source");
                    source.put("update_timestamp", ctx.get("_now"));
                    return null;
                });
        scripts.put(
                "ctx._timestamp = ctx._now",
                vars -> {
                    @SuppressWarnings("unchecked")
                    final Map<String, Object> ctx = (Map<String, Object>) vars.get("ctx");
                    ctx.put("_timestamp", ctx.get("_now"));
                    return null;
                });
        scripts.put(
                "ctx.op = delete",
                vars -> {
                    @SuppressWarnings("unchecked")
                    final Map<String, Object> ctx = (Map<String, Object>) vars.get("ctx");
                    ctx.put("op", "delete");
                    return null;
                });
        scripts.put("return", vars -> null);
        final ScriptContextRegistry scriptContextRegistry = new ScriptContextRegistry(emptyList());
        final MockScriptEngine engine = new MockScriptEngine("mock", scripts);
        final ScriptEngineRegistry scriptEngineRegistry =
                new ScriptEngineRegistry(singletonList(engine));

        final ScriptSettings scriptSettings =
                new ScriptSettings(scriptEngineRegistry, scriptContextRegistry);
        final ResourceWatcherService watcherService =
                new ResourceWatcherService(baseSettings, null);
        ScriptService scriptService = new ScriptService(
                baseSettings,
                environment,
                watcherService,
                scriptEngineRegistry,
                scriptContextRegistry,
                scriptSettings);
        final Settings settings = settings(Version.CURRENT).build();

        updateHelper = new UpdateHelper(settings, scriptService);
    }

    public void testFromXContent() throws Exception {
        UpdateRequest request = new UpdateRequest("test", "type", "1");
        // simple script
        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .field("script", "script1")
                .endObject()));
        Script script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        Map<String, Object> params = script.getParams();
        assertThat(params, equalTo(emptyMap()));

        // simple verbose script
        request.fromXContent(createParser(XContentFactory.jsonBuilder().startObject()
                    .startObject("script").field("inline", "script1").endObject()
                .endObject()));
        script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        params = script.getParams();
        assertThat(params, equalTo(emptyMap()));

        // script with params
        request = new UpdateRequest("test", "type", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder().startObject()
            .startObject("script")
                .field("inline", "script1")
                .startObject("params")
                    .field("param1", "value1")
                .endObject()
            .endObject().endObject()));
        script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        params = script.getParams();
        assertThat(params, notNullValue());
        assertThat(params.size(), equalTo(1));
        assertThat(params.get("param1").toString(), equalTo("value1"));

        request = new UpdateRequest("test", "type", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .startObject("script")
                        .startObject("params")
                            .field("param1", "value1")
                        .endObject()
                        .field("inline", "script1")
                    .endObject()
                .endObject()));
        script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        params = script.getParams();
        assertThat(params, notNullValue());
        assertThat(params.size(), equalTo(1));
        assertThat(params.get("param1").toString(), equalTo("value1"));

        // script with params and upsert
        request = new UpdateRequest("test", "type", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder().startObject()
            .startObject("script")
                .startObject("params")
                    .field("param1", "value1")
                .endObject()
                .field("inline", "script1")
            .endObject()
            .startObject("upsert")
                .field("field1", "value1")
                .startObject("compound")
                    .field("field2", "value2")
                .endObject()
            .endObject().endObject()));
        script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        params = script.getParams();
        assertThat(params, notNullValue());
        assertThat(params.size(), equalTo(1));
        assertThat(params.get("param1").toString(), equalTo("value1"));
        Map<String, Object> upsertDoc =
            XContentHelper.convertToMap(request.upsertRequest().source(), true, request.upsertRequest().getContentType()).v2();
        assertThat(upsertDoc.get("field1").toString(), equalTo("value1"));
        assertThat(((Map) upsertDoc.get("compound")).get("field2").toString(), equalTo("value2"));

        request = new UpdateRequest("test", "type", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder().startObject()
            .startObject("upsert")
                .field("field1", "value1")
                .startObject("compound")
                    .field("field2", "value2")
                .endObject()
            .endObject()
            .startObject("script")
                .startObject("params")
                    .field("param1", "value1")
                .endObject()
                .field("inline", "script1")
            .endObject().endObject()));
        script = request.script();
        assertThat(script, notNullValue());
        assertThat(script.getIdOrCode(), equalTo("script1"));
        assertThat(script.getType(), equalTo(ScriptType.INLINE));
        assertThat(script.getLang(), equalTo(Script.DEFAULT_SCRIPT_LANG));
        params = script.getParams();
        assertThat(params, notNullValue());
        assertThat(params.size(), equalTo(1));
        assertThat(params.get("param1").toString(), equalTo("value1"));
        upsertDoc = XContentHelper.convertToMap(request.upsertRequest().source(), true, request.upsertRequest().getContentType()).v2();
        assertThat(upsertDoc.get("field1").toString(), equalTo("value1"));
        assertThat(((Map) upsertDoc.get("compound")).get("field2").toString(), equalTo("value2"));

        // script with doc
        request = new UpdateRequest("test", "type", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .startObject("doc")
                        .field("field1", "value1")
                        .startObject("compound")
                            .field("field2", "value2")
                        .endObject()
                    .endObject()
                .endObject()));
        Map<String, Object> doc = request.doc().sourceAsMap();
        assertThat(doc.get("field1").toString(), equalTo("value1"));
        assertThat(((Map) doc.get("compound")).get("field2").toString(), equalTo("value2"));
    }

    // Related to issue 15338
    public void testFieldsParsing() throws Exception {
        UpdateRequest request = new UpdateRequest("test", "type1", "1").fromXContent(
                createParser(JsonXContent.jsonXContent, new BytesArray("{\"doc\": {\"field1\": \"value1\"}, \"fields\": \"_source\"}")));
        assertThat(request.doc().sourceAsMap().get("field1").toString(), equalTo("value1"));
        assertThat(request.fields(), arrayContaining("_source"));

        request = new UpdateRequest("test", "type2", "2").fromXContent(createParser(JsonXContent.jsonXContent,
                new BytesArray("{\"doc\": {\"field2\": \"value2\"}, \"fields\": [\"field1\", \"field2\"]}")));
        assertThat(request.doc().sourceAsMap().get("field2").toString(), equalTo("value2"));
        assertThat(request.fields(), arrayContaining("field1", "field2"));
    }

    public void testFetchSourceParsing() throws Exception {
        UpdateRequest request = new UpdateRequest("test", "type1", "1");
        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .field("_source", true)
                .endObject()));
        assertThat(request.fetchSource(), notNullValue());
        assertThat(request.fetchSource().includes().length, equalTo(0));
        assertThat(request.fetchSource().excludes().length, equalTo(0));
        assertThat(request.fetchSource().fetchSource(), equalTo(true));

        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .field("_source", false)
                .endObject()));
        assertThat(request.fetchSource(), notNullValue());
        assertThat(request.fetchSource().includes().length, equalTo(0));
        assertThat(request.fetchSource().excludes().length, equalTo(0));
        assertThat(request.fetchSource().fetchSource(), equalTo(false));

        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .field("_source", "path.inner.*")
                .endObject()));
        assertThat(request.fetchSource(), notNullValue());
        assertThat(request.fetchSource().fetchSource(), equalTo(true));
        assertThat(request.fetchSource().includes().length, equalTo(1));
        assertThat(request.fetchSource().excludes().length, equalTo(0));
        assertThat(request.fetchSource().includes()[0], equalTo("path.inner.*"));

        request.fromXContent(createParser(XContentFactory.jsonBuilder()
                .startObject()
                    .startObject("_source")
                        .field("includes", "path.inner.*")
                        .field("excludes", "another.inner.*")
                    .endObject()
                .endObject()));
        assertThat(request.fetchSource(), notNullValue());
        assertThat(request.fetchSource().fetchSource(), equalTo(true));
        assertThat(request.fetchSource().includes().length, equalTo(1));
        assertThat(request.fetchSource().excludes().length, equalTo(1));
        assertThat(request.fetchSource().includes()[0], equalTo("path.inner.*"));
        assertThat(request.fetchSource().excludes()[0], equalTo("another.inner.*"));
    }

    public void testNowInScript() throws IOException {
        // We just upsert one document with now() using a script
        IndexRequest indexRequest = new IndexRequest("test", "type1", "2")
            .source(jsonBuilder().startObject().field("foo", "bar").endObject());

        {
            UpdateRequest updateRequest = new UpdateRequest("test", "type1", "2")
                .upsert(indexRequest)
                .script(mockInlineScript("ctx._source.update_timestamp = ctx._now"))
                .scriptedUpsert(true);
            long nowInMillis = randomNonNegativeLong();
            // We simulate that the document is not existing yet
            GetResult getResult = new GetResult("test", "type1", "2", 0, false, null, null);
            UpdateHelper.Result result = updateHelper.prepare(new ShardId("test", "_na_", 0), updateRequest, getResult, () -> nowInMillis);
            Streamable action = result.action();
            assertThat(action, instanceOf(IndexRequest.class));
            IndexRequest indexAction = (IndexRequest) action;
            assertEquals(nowInMillis, indexAction.sourceAsMap().get("update_timestamp"));
        }
        {
            UpdateRequest updateRequest = new UpdateRequest("test", "type1", "2")
                .upsert(indexRequest)
                .script(mockInlineScript("ctx._timestamp = ctx._now"))
                .scriptedUpsert(true);
            // We simulate that the document is not existing yet
            GetResult getResult = new GetResult("test", "type1", "2", 0, true, new BytesArray("{}"), null);
            UpdateHelper.Result result = updateHelper.prepare(new ShardId("test", "_na_", 0), updateRequest, getResult, () -> 42L);
            Streamable action = result.action();
            assertThat(action, instanceOf(IndexRequest.class));
        }
    }

    public void testIndexTimeout() {
        final GetResult getResult =
                new GetResult("test", "type", "1", 0, true, new BytesArray("{\"f\":\"v\"}"), null);
        final UpdateRequest updateRequest =
                new UpdateRequest("test", "type", "1")
                        .script(mockInlineScript("return"))
                        .timeout(randomTimeValue());
        runTimeoutTest(getResult, updateRequest);
    }

    public void testDeleteTimeout() {
        final GetResult getResult =
                new GetResult("test", "type", "1", 0, true, new BytesArray("{\"f\":\"v\"}"), null);
        final UpdateRequest updateRequest =
                new UpdateRequest("test", "type", "1")
                        .script(mockInlineScript("ctx.op = delete"))
                        .timeout(randomTimeValue());
        runTimeoutTest(getResult, updateRequest);
    }

    public void testUpsertTimeout() throws IOException {
        final boolean exists = randomBoolean();
        final BytesReference source = exists ? new BytesArray("{\"f\":\"v\"}") : null;
        final GetResult getResult = new GetResult("test", "type", "1", 0, exists, source, null);
        final XContentBuilder sourceBuilder = jsonBuilder();
        sourceBuilder.startObject();
        {
            sourceBuilder.field("f", "v");
        }
        sourceBuilder.endObject();
        final IndexRequest upsert = new IndexRequest("test", "type", "1").source(sourceBuilder);
        final UpdateRequest updateRequest =
                new UpdateRequest("test", "type", "1")
                .upsert(upsert)
                .script(mockInlineScript("return"))
                .timeout(randomTimeValue());
        runTimeoutTest(getResult, updateRequest);
    }

    private void runTimeoutTest(final GetResult getResult, final UpdateRequest updateRequest) {
        final UpdateHelper.Result result = updateHelper.prepare(
                new ShardId("test", "", 0),
                updateRequest,
                getResult,
                ESTestCase::randomNonNegativeLong);
        final Streamable action = result.action();
        assertThat(action, instanceOf(ReplicationRequest.class));
        final ReplicationRequest request = (ReplicationRequest) action;
        assertThat(request.timeout(), equalTo(updateRequest.timeout()));
    }

    public void testToAndFromXContent() throws IOException {
        UpdateRequest updateRequest = new UpdateRequest();
        updateRequest.detectNoop(randomBoolean());

        if (randomBoolean()) {
            XContentType xContentType = randomFrom(XContentType.values());
            BytesReference source = RandomObjects.randomSource(random(), xContentType);
            updateRequest.doc(new IndexRequest().source(source, xContentType));
            updateRequest.docAsUpsert(randomBoolean());
        } else {
            ScriptType scriptType = randomFrom(ScriptType.values());
            String scriptLang = (scriptType != ScriptType.STORED) ? randomAlphaOfLength(10) : null;
            String scriptIdOrCode = randomAlphaOfLength(10);
            int nbScriptParams = randomIntBetween(0, 5);
            Map<String, Object> scriptParams = new HashMap<>(nbScriptParams);
            for (int i = 0; i < nbScriptParams; i++) {
                scriptParams.put(randomAlphaOfLength(5), randomAlphaOfLength(5));
            }
            updateRequest.script(new Script(scriptType, scriptLang, scriptIdOrCode, scriptParams));
            updateRequest.scriptedUpsert(randomBoolean());
        }
        if (randomBoolean()) {
            XContentType xContentType = randomFrom(XContentType.values());
            BytesReference source = RandomObjects.randomSource(random(), xContentType);
            updateRequest.upsert(new IndexRequest().source(source, xContentType));
        }
        if (randomBoolean()) {
            String[] fields = new String[randomIntBetween(0, 5)];
            for (int i = 0; i < fields.length; i++) {
                fields[i] = randomAlphaOfLength(5);
            }
            updateRequest.fields(fields);
        }
        if (randomBoolean()) {
            if (randomBoolean()) {
                updateRequest.fetchSource(randomBoolean());
            } else {
                String[] includes = new String[randomIntBetween(0, 5)];
                for (int i = 0; i < includes.length; i++) {
                    includes[i] = randomAlphaOfLength(5);
                }
                String[] excludes = new String[randomIntBetween(0, 5)];
                for (int i = 0; i < excludes.length; i++) {
                    excludes[i] = randomAlphaOfLength(5);
                }
                if (randomBoolean()) {
                    updateRequest.fetchSource(includes, excludes);
                }
            }
        }

        XContentType xContentType = randomFrom(XContentType.values());
        boolean humanReadable = randomBoolean();
        BytesReference originalBytes = toShuffledXContent(updateRequest, xContentType, ToXContent.EMPTY_PARAMS, humanReadable);

        if (randomBoolean()) {
            try (XContentParser parser = createParser(xContentType.xContent(), originalBytes)) {
                originalBytes = shuffleXContent(parser, randomBoolean()).bytes();
            }
        }

        UpdateRequest parsedUpdateRequest = new UpdateRequest();
        try (XContentParser parser = createParser(xContentType.xContent(), originalBytes)) {
            parsedUpdateRequest.fromXContent(parser);
            assertNull(parser.nextToken());
        }

        assertEquals(updateRequest.detectNoop(), parsedUpdateRequest.detectNoop());
        assertEquals(updateRequest.docAsUpsert(), parsedUpdateRequest.docAsUpsert());
        assertEquals(updateRequest.docAsUpsert(), parsedUpdateRequest.docAsUpsert());
        assertEquals(updateRequest.script(), parsedUpdateRequest.script());
        assertEquals(updateRequest.scriptedUpsert(), parsedUpdateRequest.scriptedUpsert());
        assertArrayEquals(updateRequest.fields(), parsedUpdateRequest.fields());
        assertEquals(updateRequest.fetchSource(), parsedUpdateRequest.fetchSource());

        BytesReference finalBytes = toXContent(parsedUpdateRequest, xContentType, humanReadable);
        assertToXContentEquivalent(originalBytes, finalBytes, xContentType);
    }

    public void testToValidateUpsertRequestAndVersion() {
        UpdateRequest updateRequest = new UpdateRequest("index", "type", "id");
        updateRequest.version(1L);
        updateRequest.doc("{}", XContentType.JSON);
        updateRequest.upsert(new IndexRequest("index","type", "id"));
        assertThat(updateRequest.validate().validationErrors(), contains("can't provide both upsert request and a version"));
    }

    public void testToValidateUpsertRequestWithVersion() {
        UpdateRequest updateRequest = new UpdateRequest("index", "type", "id");
        updateRequest.doc("{}", XContentType.JSON);
        updateRequest.upsert(new IndexRequest("index", "type", "1").version(1L));
        assertThat(updateRequest.validate().validationErrors(), contains("can't provide version in upsert request"));
    }
}
