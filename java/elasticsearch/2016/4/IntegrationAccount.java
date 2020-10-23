/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.notification.hipchat;

import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.ExceptionsHelper;
import org.elasticsearch.common.Nullable;
import org.elasticsearch.common.logging.ESLogger;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.settings.SettingsException;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.watcher.actions.hipchat.HipChatAction;
import org.elasticsearch.xpack.notification.hipchat.HipChatMessage.Color;
import org.elasticsearch.xpack.notification.hipchat.HipChatMessage.Format;
import org.elasticsearch.watcher.support.http.HttpClient;
import org.elasticsearch.watcher.support.http.HttpMethod;
import org.elasticsearch.watcher.support.http.HttpRequest;
import org.elasticsearch.watcher.support.http.HttpResponse;
import org.elasticsearch.watcher.support.http.Scheme;
import org.elasticsearch.watcher.support.text.TextTemplateEngine;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 *
 */
public class IntegrationAccount extends HipChatAccount {

    public static final String TYPE = "integration";

    final String room;
    final Defaults defaults;

    public IntegrationAccount(String name, Settings settings, HipChatServer defaultServer, HttpClient httpClient, ESLogger logger) {
        super(name, Profile.INTEGRATION, settings, defaultServer, httpClient, logger);
        String[] rooms = settings.getAsArray(ROOM_SETTING, null);
        if (rooms == null || rooms.length == 0) {
            throw new SettingsException("invalid hipchat account [" + name + "]. missing required [" + ROOM_SETTING + "] setting for [" +
                    TYPE + "] account profile");
        }
        if (rooms.length > 1) {
            throw new SettingsException("invalid hipchat account [" + name + "]. [" + ROOM_SETTING + "] setting for [" + TYPE + "] " +
                    "account must only be set with a single value");
        }
        this.room = rooms[0];
        defaults = new Defaults(settings);
    }

    @Override
    public String type() {
        return TYPE;
    }

    @Override
    public void validateParsedTemplate(String watchId, String actionId, HipChatMessage.Template template) throws SettingsException {
        if (template.rooms != null) {
            throw new ElasticsearchParseException("invalid [" + HipChatAction.TYPE + "] action for [" + watchId + "/" + actionId + "] " +
                    "action. [" + name + "] hipchat account doesn't support custom rooms");
        }
        if (template.users != null) {
            throw new ElasticsearchParseException("invalid [" + HipChatAction.TYPE + "] action for [" + watchId + "/" + actionId + "] " +
                    "action. [" + name + "] hipchat account doesn't support user private messages");
        }
        if (template.from != null) {
            throw new ElasticsearchParseException("invalid [" + HipChatAction.TYPE + "] action for [" + watchId + "/" + actionId + "] " +
                    "action. [" + name + "] hipchat account doesn't support custom `from` fields");
        }
    }

    @Override
    public HipChatMessage render(String watchId, String actionId, TextTemplateEngine engine, HipChatMessage.Template template,
                                 Map<String, Object> model) {
        String message = engine.render(template.body, model);
        Color color = template.color != null ? Color.resolve(engine.render(template.color, model), defaults.color) : defaults.color;
        Boolean notify = template.notify != null ? template.notify : defaults.notify;
        Format messageFormat = template.format != null ? template.format : defaults.format;
        return new HipChatMessage(message, null, null, null, messageFormat, color, notify);
    }

    @Override
    public SentMessages send(HipChatMessage message) {
        List<SentMessages.SentMessage> sentMessages = new ArrayList<>();
        HttpRequest request = buildRoomRequest(room, message);
        try {
            HttpResponse response = httpClient.execute(request);
            sentMessages.add(SentMessages.SentMessage.responded(room, SentMessages.SentMessage.TargetType.ROOM, message, request,
                    response));
        } catch (Exception e) {
            logger.error("failed to execute hipchat api http request", e);
            sentMessages.add(SentMessages.SentMessage.error(room, SentMessages.SentMessage.TargetType.ROOM, message,
                    ExceptionsHelper.detailedMessage(e)));
        }
        return new SentMessages(name, sentMessages);
    }

    public HttpRequest buildRoomRequest(String room, final HipChatMessage message) {
        return server.httpRequest()
                .method(HttpMethod.POST)
                .scheme(Scheme.HTTPS)
                .path("/v2/room/" + room + "/notification")
                .setHeader("Content-Type", "application/json")
                .setHeader("Authorization", "Bearer " + authToken)
                .body(XContentHelper.toString(new ToXContent() {
                    @Override
                    public XContentBuilder toXContent(XContentBuilder xbuilder, Params params) throws IOException {
                        xbuilder.field("message", message.body);
                        if (message.format != null) {
                            xbuilder.field("message_format", message.format.value());
                        }
                        if (message.notify != null) {
                            xbuilder.field("notify", message.notify);
                        }
                        if (message.color != null) {
                            xbuilder.field("color", String.valueOf(message.color.value()));
                        }
                        return xbuilder;
                    }
                }))
                .build();
    }

    static class Defaults {

        final @Nullable Format format;
        final @Nullable Color color;
        final @Nullable Boolean notify;

        public Defaults(Settings settings) {
            this.format = Format.resolve(settings, DEFAULT_FORMAT_SETTING, null);
            this.color = Color.resolve(settings, DEFAULT_COLOR_SETTING, null);
            this.notify = settings.getAsBoolean(DEFAULT_NOTIFY_SETTING, null);
        }
    }
}
