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

package org.elasticsearch.rest.action.admin.indices;

import org.elasticsearch.action.admin.indices.shrink.ShrinkRequest;
import org.elasticsearch.action.admin.indices.shrink.ShrinkResponse;
import org.elasticsearch.action.support.ActiveShardCount;
import org.elasticsearch.client.node.NodeClient;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.rest.BaseRestHandler;
import org.elasticsearch.rest.RestController;
import org.elasticsearch.rest.RestRequest;
import org.elasticsearch.rest.action.AcknowledgedRestListener;

import java.io.IOException;

public class RestShrinkIndexAction extends BaseRestHandler {
    public RestShrinkIndexAction(Settings settings, RestController controller) {
        super(settings);
        controller.registerHandler(RestRequest.Method.PUT, "/{index}/_shrink/{target}", this);
        controller.registerHandler(RestRequest.Method.POST, "/{index}/_shrink/{target}", this);
    }

    @Override
    public RestChannelConsumer prepareRequest(final RestRequest request, final NodeClient client) throws IOException {
        if (request.param("target") == null) {
            throw new IllegalArgumentException("no target index");
        }
        if (request.param("index") == null) {
            throw new IllegalArgumentException("no source index");
        }
        ShrinkRequest shrinkIndexRequest = new ShrinkRequest(request.param("target"), request.param("index"));
        request.applyContentParser(parser -> ShrinkRequest.PARSER.parse(parser, shrinkIndexRequest, null));
        shrinkIndexRequest.timeout(request.paramAsTime("timeout", shrinkIndexRequest.timeout()));
        shrinkIndexRequest.masterNodeTimeout(request.paramAsTime("master_timeout", shrinkIndexRequest.masterNodeTimeout()));
        shrinkIndexRequest.setWaitForActiveShards(ActiveShardCount.parseString(request.param("wait_for_active_shards")));
        return channel -> client.admin().indices().shrinkIndex(shrinkIndexRequest, new AcknowledgedRestListener<ShrinkResponse>(channel) {
            @Override
            public void addCustomFields(XContentBuilder builder, ShrinkResponse response) throws IOException {
                response.addCustomFields(builder);
            }
        });
    }
}
