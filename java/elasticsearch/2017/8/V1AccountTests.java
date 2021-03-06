/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.notification.hipchat;

import org.apache.logging.log4j.Logger;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.settings.SettingsException;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.xpack.common.http.HttpClient;
import org.elasticsearch.xpack.common.http.HttpMethod;
import org.elasticsearch.xpack.common.http.HttpRequest;
import org.elasticsearch.xpack.common.http.HttpResponse;
import org.elasticsearch.xpack.common.http.Scheme;

import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.hamcrest.Matchers.arrayContaining;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class V1AccountTests extends ESTestCase {
    public void testSettings() throws Exception {
        String accountName = "_name";

        Settings.Builder sb = Settings.builder();

        String authToken = randomAlphaOfLength(50);
        sb.put(V1Account.AUTH_TOKEN_SETTING, authToken);

        String host = HipChatServer.DEFAULT.host();
        if (randomBoolean()) {
            host = randomAlphaOfLength(10);
            sb.put(HipChatServer.HOST_SETTING, host);
        }
        int port = HipChatServer.DEFAULT.port();
        if (randomBoolean()) {
            port = randomIntBetween(300, 400);
            sb.put(HipChatServer.PORT_SETTING, port);
        }

        String[] defaultRooms = null;
        if (randomBoolean()) {
            defaultRooms = new String[] { "_r1", "_r2" };
            sb.put(HipChatAccount.DEFAULT_ROOM_SETTING, "_r1,_r2");
        }
        String defaultFrom = null;
        if (randomBoolean()) {
            defaultFrom = randomAlphaOfLength(10);
            sb.put(HipChatAccount.DEFAULT_FROM_SETTING, defaultFrom);
        }
        HipChatMessage.Format defaultFormat = null;
        if (randomBoolean()) {
            defaultFormat = randomFrom(HipChatMessage.Format.values());
            sb.put(HipChatAccount.DEFAULT_FORMAT_SETTING, defaultFormat);
        }
        HipChatMessage.Color defaultColor = null;
        if (randomBoolean()) {
            defaultColor = randomFrom(HipChatMessage.Color.values());
            sb.put(HipChatAccount.DEFAULT_COLOR_SETTING, defaultColor);
        }
        Boolean defaultNotify = null;
        if (randomBoolean()) {
            defaultNotify = randomBoolean();
            sb.put(HipChatAccount.DEFAULT_NOTIFY_SETTING, defaultNotify);
        }
        Settings settings = sb.build();

        V1Account account = new V1Account(accountName, settings, HipChatServer.DEFAULT, mock(HttpClient.class), mock(Logger.class));

        assertThat(account.profile, is(HipChatAccount.Profile.V1));
        assertThat(account.name, equalTo(accountName));
        assertThat(account.server.host(), is(host));
        assertThat(account.server.port(), is(port));
        assertThat(account.authToken, is(authToken));
        if (defaultRooms != null) {
            assertThat(account.defaults.rooms, arrayContaining(defaultRooms));
        } else {
            assertThat(account.defaults.rooms, nullValue());
        }
        assertThat(account.defaults.from, is(defaultFrom));
        assertThat(account.defaults.format, is(defaultFormat));
        assertThat(account.defaults.color, is(defaultColor));
        assertThat(account.defaults.notify, is(defaultNotify));
    }

    public void testSettingsNoAuthToken() throws Exception {
        Settings.Builder sb = Settings.builder();
        try {
            new V1Account("_name", sb.build(), HipChatServer.DEFAULT, mock(HttpClient.class), mock(Logger.class));
            fail("Expected SettingsException");
        } catch (SettingsException e) {
            assertThat(e.getMessage(), is("hipchat account [_name] missing required [auth_token] setting"));
        }
    }

    public void testSend() throws Exception {
        HttpClient httpClient = mock(HttpClient.class);
        V1Account account = new V1Account("_name", Settings.builder()
                .put("host", "_host")
                .put("port", "443")
                .put("auth_token", "_token")
                .build(), HipChatServer.DEFAULT, httpClient, mock(Logger.class));

        HipChatMessage.Format format = randomFrom(HipChatMessage.Format.values());
        HipChatMessage.Color color = randomFrom(HipChatMessage.Color.values());
        Boolean notify = randomBoolean();
        HipChatMessage message = new HipChatMessage("_body", new String[] { "_r1", "_r2" }, null, "_from", format, color, notify);

        HttpRequest req1 = HttpRequest.builder("_host", 443)
                .method(HttpMethod.POST)
                .scheme(Scheme.HTTPS)
                .path("/v1/rooms/message")
                .setHeader("Content-Type", "application/x-www-form-urlencoded")
                .setParam("format", "json")
                .setParam("auth_token", "_token")
                .body(new StringBuilder()
                        .append("room_id=").append("_r1&")
                        .append("from=").append("_from&")
                        .append("message=").append("_body&")
                        .append("message_format=").append(format.value()).append("&")
                        .append("color=").append(color.value()).append("&")
                        .append("notify=").append(notify ? "1" : "0")
                        .toString())
                .build();

        logger.info("expected (r1): {}", jsonBuilder().value(req1).bytes().utf8ToString());

        HttpResponse res1 = mock(HttpResponse.class);
        when(res1.status()).thenReturn(200);
        when(httpClient.execute(req1)).thenReturn(res1);

        HttpRequest req2 = HttpRequest.builder("_host", 443)
                .method(HttpMethod.POST)
                .scheme(Scheme.HTTPS)
                .path("/v1/rooms/message")
                .setHeader("Content-Type", "application/x-www-form-urlencoded")
                .setParam("format", "json")
                .setParam("auth_token", "_token")
                .body(new StringBuilder()
                        .append("room_id=").append("_r2&")
                        .append("from=").append("_from&")
                        .append("message=").append("_body&")
                        .append("message_format=").append(format.value()).append("&")
                        .append("color=").append(color.value()).append("&")
                        .append("notify=").append(notify ? "1" : "0")
                        .toString())
                .build();

        logger.info("expected (r2): {}", jsonBuilder().value(req2).bytes().utf8ToString());

        HttpResponse res2 = mock(HttpResponse.class);
        when(res2.status()).thenReturn(200);
        when(httpClient.execute(req2)).thenReturn(res2);

        account.send(message, null);

        verify(httpClient).execute(req1);
        verify(httpClient).execute(req2);
    }
}
