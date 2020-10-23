/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.watcher.input.http;

import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.collect.MapBuilder;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.watcher.actions.ActionWrapper;
import org.elasticsearch.watcher.actions.ExecutableActions;
import org.elasticsearch.watcher.condition.always.ExecutableAlwaysCondition;
import org.elasticsearch.watcher.execution.TriggeredExecutionContext;
import org.elasticsearch.watcher.execution.WatchExecutionContext;
import org.elasticsearch.watcher.input.InputBuilders;
import org.elasticsearch.watcher.input.simple.ExecutableSimpleInput;
import org.elasticsearch.watcher.input.simple.SimpleInput;
import org.elasticsearch.watcher.support.http.HttpClient;
import org.elasticsearch.watcher.support.http.HttpContentType;
import org.elasticsearch.watcher.support.http.HttpMethod;
import org.elasticsearch.watcher.support.http.HttpRequest;
import org.elasticsearch.watcher.support.http.HttpRequestTemplate;
import org.elasticsearch.watcher.support.http.HttpResponse;
import org.elasticsearch.watcher.support.http.Scheme;
import org.elasticsearch.watcher.support.http.auth.HttpAuth;
import org.elasticsearch.watcher.support.http.auth.HttpAuthRegistry;
import org.elasticsearch.watcher.support.http.auth.basic.BasicAuth;
import org.elasticsearch.watcher.support.http.auth.basic.BasicAuthFactory;
import org.elasticsearch.watcher.support.secret.SecretService;
import org.elasticsearch.watcher.support.text.TextTemplate;
import org.elasticsearch.watcher.support.text.TextTemplateEngine;
import org.elasticsearch.watcher.trigger.schedule.IntervalSchedule;
import org.elasticsearch.watcher.trigger.schedule.ScheduleTrigger;
import org.elasticsearch.watcher.trigger.schedule.ScheduleTriggerEvent;
import org.elasticsearch.watcher.watch.Payload;
import org.elasticsearch.watcher.watch.Watch;
import org.elasticsearch.watcher.watch.WatchStatus;
import org.jboss.netty.handler.codec.http.HttpHeaders;
import org.joda.time.DateTime;
import org.junit.Before;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Map;

import static java.util.Collections.emptyMap;
import static java.util.Collections.singletonMap;
import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasEntry;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.joda.time.DateTimeZone.UTC;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

/**
 */
public class HttpInputTests extends ESTestCase {
    private HttpClient httpClient;
    private HttpInputFactory httpParser;
    private SecretService secretService;
    private TextTemplateEngine templateEngine;

    @Before
    public void init() throws Exception {
        httpClient = mock(HttpClient.class);
        templateEngine = mock(TextTemplateEngine.class);
        secretService = mock(SecretService.class);
        HttpAuthRegistry registry = new HttpAuthRegistry(singletonMap("basic", new BasicAuthFactory(secretService)));
        httpParser = new HttpInputFactory(Settings.EMPTY, httpClient, templateEngine, new HttpRequestTemplate.Parser(registry));
    }

    public void testExecute() throws Exception {
        String host = "_host";
        int port = 123;
        HttpRequestTemplate.Builder request = HttpRequestTemplate.builder(host, port)
                .method(HttpMethod.POST)
                .body("_body");
        HttpInput httpInput;

        HttpResponse response;
        switch (randomIntBetween(1, 6)) {
            case 1:
                response = new HttpResponse(123, "{\"key\" : \"value\"}".getBytes(StandardCharsets.UTF_8));
                httpInput = InputBuilders.httpInput(request.build()).build();
                break;
            case 2:
                response = new HttpResponse(123, "---\nkey : value".getBytes(StandardCharsets.UTF_8));
                httpInput = InputBuilders.httpInput(request.build()).expectedResponseXContentType(HttpContentType.YAML).build();
                break;
            case 3:
                response = new HttpResponse(123, "{\"key\" : \"value\"}".getBytes(StandardCharsets.UTF_8),
                    singletonMap(HttpHeaders.Names.CONTENT_TYPE, new String[] { XContentType.JSON.mediaType() }));
                httpInput = InputBuilders.httpInput(request.build()).build();
                break;
            case 4:
                response = new HttpResponse(123, "key: value".getBytes(StandardCharsets.UTF_8),
                        singletonMap(HttpHeaders.Names.CONTENT_TYPE, new String[] { XContentType.YAML.mediaType() }));
                httpInput = InputBuilders.httpInput(request.build()).build();
                break;
            case 5:
                response = new HttpResponse(123, "---\nkey: value".getBytes(StandardCharsets.UTF_8),
                        singletonMap(HttpHeaders.Names.CONTENT_TYPE, new String[] { "unrecognized_content_type" }));
                httpInput = InputBuilders.httpInput(request.build()).expectedResponseXContentType(HttpContentType.YAML).build();
                break;
            default:
                response = new HttpResponse(123, "{\"key\" : \"value\"}".getBytes(StandardCharsets.UTF_8),
                        singletonMap(HttpHeaders.Names.CONTENT_TYPE, new String[] { "unrecognized_content_type" }));
                httpInput = InputBuilders.httpInput(request.build()).build();
                break;
        }

        ExecutableHttpInput input = new ExecutableHttpInput(httpInput, logger, httpClient, templateEngine);

        when(httpClient.execute(any(HttpRequest.class))).thenReturn(response);

        when(templateEngine.render(eq(TextTemplate.inline("_body").build()), any(Map.class))).thenReturn("_body");

        Watch watch = new Watch("test-watch",
                new ScheduleTrigger(new IntervalSchedule(new IntervalSchedule.Interval(1, IntervalSchedule.Interval.Unit.MINUTES))),
                new ExecutableSimpleInput(new SimpleInput(new Payload.Simple()), logger),
                new ExecutableAlwaysCondition(logger),
                null,
                null,
                new ExecutableActions(new ArrayList<ActionWrapper>()),
                null,
                new WatchStatus(new DateTime(0, UTC), emptyMap()));
        WatchExecutionContext ctx = new TriggeredExecutionContext(watch,
                new DateTime(0, UTC),
                new ScheduleTriggerEvent(watch.id(), new DateTime(0, UTC), new DateTime(0, UTC)),
                TimeValue.timeValueSeconds(5));
        HttpInput.Result result = input.execute(ctx, new Payload.Simple());
        assertThat(result.type(), equalTo(HttpInput.TYPE));
        assertThat(result.payload().data(), equalTo(MapBuilder.<String, Object>newMapBuilder().put("key", "value").map()));
    }

    public void testExecuteNonJson() throws Exception {
        String host = "_host";
        int port = 123;
        HttpRequestTemplate.Builder request = HttpRequestTemplate.builder(host, port)
                .method(HttpMethod.POST)
                .body("_body");
        HttpInput httpInput = InputBuilders.httpInput(request.build()).expectedResponseXContentType(HttpContentType.TEXT).build();
        ExecutableHttpInput input = new ExecutableHttpInput(httpInput, logger, httpClient, templateEngine);
        String notJson = "This is not json";
        HttpResponse response = new HttpResponse(123, notJson.getBytes(StandardCharsets.UTF_8));
        when(httpClient.execute(any(HttpRequest.class))).thenReturn(response);
        when(templateEngine.render(eq(TextTemplate.inline("_body").build()), any(Map.class))).thenReturn("_body");
        Watch watch = new Watch("test-watch",
                new ScheduleTrigger(new IntervalSchedule(new IntervalSchedule.Interval(1, IntervalSchedule.Interval.Unit.MINUTES))),
                new ExecutableSimpleInput(new SimpleInput(new Payload.Simple()), logger),
                new ExecutableAlwaysCondition(logger),
                null,
                null,
                new ExecutableActions(new ArrayList<ActionWrapper>()),
                null,
                new WatchStatus(new DateTime(0, UTC), emptyMap()));
        WatchExecutionContext ctx = new TriggeredExecutionContext(watch,
                new DateTime(0, UTC),
                new ScheduleTriggerEvent(watch.id(), new DateTime(0, UTC), new DateTime(0, UTC)),
                TimeValue.timeValueSeconds(5));
        HttpInput.Result result = input.execute(ctx, new Payload.Simple());
        assertThat(result.type(), equalTo(HttpInput.TYPE));
        assertThat(result.payload().data().get("_value").toString(), equalTo(notJson));
    }

    public void testParser() throws Exception {
        final HttpMethod httpMethod = rarely() ? null : randomFrom(HttpMethod.values());
        Scheme scheme = randomFrom(Scheme.HTTP, Scheme.HTTPS, null);
        String host = randomAsciiOfLength(3);
        int port = randomIntBetween(8000, 9000);
        String path = randomAsciiOfLength(3);
        TextTemplate pathTemplate = TextTemplate.inline(path).build();
        String body = randomBoolean() ? randomAsciiOfLength(3) : null;
        Map<String, TextTemplate> params =
                randomBoolean() ? new MapBuilder<String, TextTemplate>().put("a", TextTemplate.inline("b").build()).map() : null;
        Map<String, TextTemplate> headers =
                randomBoolean() ? new MapBuilder<String, TextTemplate>().put("c", TextTemplate.inline("d").build()).map() : null;
        HttpAuth auth = randomBoolean() ? new BasicAuth("username", "password".toCharArray()) : null;
        HttpRequestTemplate.Builder requestBuilder = HttpRequestTemplate.builder(host, port)
                .scheme(scheme)
                .method(httpMethod)
                .path(pathTemplate)
                .body(body != null ? TextTemplate.inline(body).build() : null)
                .auth(auth);

        if (params != null) {
            requestBuilder.putParams(params);
        }
        if (headers != null) {
            requestBuilder.putHeaders(headers);
        }
        HttpInput.Builder inputBuilder = InputBuilders.httpInput(requestBuilder);
        HttpContentType expectedResponseXContentType = randomFrom(HttpContentType.values());

        String[] extractKeys = randomFrom(new String[]{"foo", "bar"}, new String[]{"baz"}, null);
        if (expectedResponseXContentType != HttpContentType.TEXT) {
            if (extractKeys != null) {
                inputBuilder.extractKeys(extractKeys);
            }
        }

        inputBuilder.expectedResponseXContentType(expectedResponseXContentType);
        BytesReference source = jsonBuilder().value(inputBuilder.build()).bytes();
        XContentParser parser = XContentHelper.createParser(source);
        parser.nextToken();
        HttpInput result = httpParser.parseInput("_id", parser);

        assertThat(result.type(), equalTo(HttpInput.TYPE));
        assertThat(result.getRequest().scheme(), equalTo(scheme != null ? scheme : Scheme.HTTP)); // http is the default
        assertThat(result.getRequest().method(), equalTo(httpMethod != null ? httpMethod : HttpMethod.GET)); // get is the default
        assertThat(result.getRequest().host(), equalTo(host));
        assertThat(result.getRequest().port(), equalTo(port));
        assertThat(result.getRequest().path(), is(TextTemplate.inline(path).build()));
        assertThat(result.getExpectedResponseXContentType(), equalTo(expectedResponseXContentType));
        if (expectedResponseXContentType != HttpContentType.TEXT && extractKeys != null) {
            for (String key : extractKeys) {
                assertThat(result.getExtractKeys().contains(key), is(true));
            }
        }
        if (params != null) {
            assertThat(result.getRequest().params(), hasEntry(is("a"), is(TextTemplate.inline("b").build())));
        }
        if (headers != null) {
            assertThat(result.getRequest().headers(), hasEntry(is("c"), is(TextTemplate.inline("d").build())));
        }
        assertThat(result.getRequest().auth(), equalTo(auth));
        if (body != null) {
            assertThat(result.getRequest().body(), is(TextTemplate.inline(body).build()));
        } else {
            assertThat(result.getRequest().body(), nullValue());
        }
    }

    public void testParser_invalidHttpMethod() throws Exception {
        XContentBuilder builder = jsonBuilder().startObject()
                .startObject("request")
                    .field("method", "_method")
                    .field("body", "_body")
                .endObject()
                .endObject();
        XContentParser parser = XContentHelper.createParser(builder.bytes());
        parser.nextToken();
        try {
            httpParser.parseInput("_id", parser);
            fail("Expected IllegalArgumentException");
        } catch (IllegalArgumentException e) {
            assertThat(e.getMessage(), is("unsupported http method [_METHOD]"));
        }
    }
}
