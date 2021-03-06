/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.watcher.transport.actions.put;

import org.elasticsearch.action.ActionRequestBuilder;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.xpack.watcher.client.WatchSourceBuilder;

public class PutWatchRequestBuilder extends ActionRequestBuilder<PutWatchRequest, PutWatchResponse, PutWatchRequestBuilder> {

    public PutWatchRequestBuilder(ElasticsearchClient client) {
        super(client, PutWatchAction.INSTANCE, new PutWatchRequest());
    }

    public PutWatchRequestBuilder(ElasticsearchClient client, String id) {
        super(client, PutWatchAction.INSTANCE, new PutWatchRequest());
        request.setId(id);
    }

    /**
     * @param id The watch id to be created
     */
    public PutWatchRequestBuilder setId(String id){
        request.setId(id);
        return this;
    }

    /**
     * @param source the source of the watch to be created
     * @param xContentType the content type of the source
     */
    public PutWatchRequestBuilder setSource(BytesReference source, XContentType xContentType) {
        request.setSource(source, xContentType);
        return this;
    }

    /**
     * @param source the source of the watch to be created
     */
    public PutWatchRequestBuilder setSource(WatchSourceBuilder source) {
        request.setSource(source);
        return this;
    }

    /**
     * @param active Sets whether the watcher is in/active by default
     */
    public PutWatchRequestBuilder setActive(boolean active) {
        request.setActive(active);
        return this;
    }
}
