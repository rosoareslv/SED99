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

package org.elasticsearch.client;

import org.apache.http.HttpEntity;
import org.apache.http.client.methods.HttpDelete;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.client.methods.HttpHead;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.client.methods.HttpPut;
import org.apache.http.entity.ByteArrayEntity;
import org.apache.http.entity.ContentType;
import org.apache.lucene.util.BytesRef;
import org.elasticsearch.action.DocWriteRequest;
import org.elasticsearch.action.bulk.BulkRequest;
import org.elasticsearch.action.delete.DeleteRequest;
import org.elasticsearch.action.get.GetRequest;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.action.support.ActiveShardCount;
import org.elasticsearch.action.support.WriteRequest;
import org.elasticsearch.action.update.UpdateRequest;
import org.elasticsearch.common.Nullable;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.lucene.uid.Versions;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.common.xcontent.NamedXContentRegistry;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.index.VersionType;
import org.elasticsearch.search.fetch.subphase.FetchSourceContext;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.StringJoiner;

final class Request {

    private static final String DELIMITER = "/";

    final String method;
    final String endpoint;
    final Map<String, String> params;
    final HttpEntity entity;

    Request(String method, String endpoint, Map<String, String> params, HttpEntity entity) {
        this.method = method;
        this.endpoint = endpoint;
        this.params = params;
        this.entity = entity;
    }

    @Override
    public String toString() {
        return "Request{" +
                "method='" + method + '\'' +
                ", endpoint='" + endpoint + '\'' +
                ", params=" + params +
                '}';
    }

    static Request delete(DeleteRequest deleteRequest) {
        String endpoint = endpoint(deleteRequest.index(), deleteRequest.type(), deleteRequest.id());

        Params parameters = Params.builder();
        parameters.withRouting(deleteRequest.routing());
        parameters.withParent(deleteRequest.parent());
        parameters.withTimeout(deleteRequest.timeout());
        parameters.withVersion(deleteRequest.version());
        parameters.withVersionType(deleteRequest.versionType());
        parameters.withRefreshPolicy(deleteRequest.getRefreshPolicy());
        parameters.withWaitForActiveShards(deleteRequest.waitForActiveShards());

        return new Request(HttpDelete.METHOD_NAME, endpoint, parameters.getParams(), null);
    }

    static Request info() {
        return new Request(HttpGet.METHOD_NAME, "/", Collections.emptyMap(), null);
    }

    static Request bulk(BulkRequest bulkRequest) throws IOException {
        Params parameters = Params.builder();
        parameters.withTimeout(bulkRequest.timeout());
        parameters.withRefreshPolicy(bulkRequest.getRefreshPolicy());

        // Bulk API only supports newline delimited JSON or Smile. Before executing
        // the bulk, we need to check that all requests have the same content-type
        // and this content-type is supported by the Bulk API.
        XContentType bulkContentType = null;
        for (int i = 0; i < bulkRequest.numberOfActions(); i++) {
            DocWriteRequest<?> request = bulkRequest.requests().get(i);

            DocWriteRequest.OpType opType = request.opType();
            if (opType == DocWriteRequest.OpType.INDEX || opType == DocWriteRequest.OpType.CREATE) {
                bulkContentType = enforceSameContentType((IndexRequest) request, bulkContentType);

            } else if (opType == DocWriteRequest.OpType.UPDATE) {
                UpdateRequest updateRequest = (UpdateRequest) request;
                if (updateRequest.doc() != null) {
                    bulkContentType = enforceSameContentType(updateRequest.doc(), bulkContentType);
                }
                if (updateRequest.upsertRequest() != null) {
                    bulkContentType = enforceSameContentType(updateRequest.upsertRequest(), bulkContentType);
                }
            }
        }

        if (bulkContentType == null) {
            bulkContentType = XContentType.JSON;
        }

        byte separator = bulkContentType.xContent().streamSeparator();
        ContentType requestContentType = ContentType.create(bulkContentType.mediaType());

        ByteArrayOutputStream content = new ByteArrayOutputStream();
        for (DocWriteRequest<?> request : bulkRequest.requests()) {
            DocWriteRequest.OpType opType = request.opType();

            try (XContentBuilder metadata = XContentBuilder.builder(bulkContentType.xContent())) {
                metadata.startObject();
                {
                    metadata.startObject(opType.getLowercase());
                    if (Strings.hasLength(request.index())) {
                        metadata.field("_index", request.index());
                    }
                    if (Strings.hasLength(request.type())) {
                        metadata.field("_type", request.type());
                    }
                    if (Strings.hasLength(request.id())) {
                        metadata.field("_id", request.id());
                    }
                    if (Strings.hasLength(request.routing())) {
                        metadata.field("_routing", request.routing());
                    }
                    if (Strings.hasLength(request.parent())) {
                        metadata.field("_parent", request.parent());
                    }
                    if (request.version() != Versions.MATCH_ANY) {
                        metadata.field("_version", request.version());
                    }

                    VersionType versionType = request.versionType();
                    if (versionType != VersionType.INTERNAL) {
                        if (versionType == VersionType.EXTERNAL) {
                            metadata.field("_version_type", "external");
                        } else if (versionType == VersionType.EXTERNAL_GTE) {
                            metadata.field("_version_type", "external_gte");
                        } else if (versionType == VersionType.FORCE) {
                            metadata.field("_version_type", "force");
                        }
                    }

                    if (opType == DocWriteRequest.OpType.INDEX || opType == DocWriteRequest.OpType.CREATE) {
                        IndexRequest indexRequest = (IndexRequest) request;
                        if (Strings.hasLength(indexRequest.getPipeline())) {
                            metadata.field("pipeline", indexRequest.getPipeline());
                        }
                    } else if (opType == DocWriteRequest.OpType.UPDATE) {
                        UpdateRequest updateRequest = (UpdateRequest) request;
                        if (updateRequest.retryOnConflict() > 0) {
                            metadata.field("_retry_on_conflict", updateRequest.retryOnConflict());
                        }
                        if (updateRequest.fetchSource() != null) {
                            metadata.field("_source", updateRequest.fetchSource());
                        }
                    }
                    metadata.endObject();
                }
                metadata.endObject();

                BytesRef metadataSource = metadata.bytes().toBytesRef();
                content.write(metadataSource.bytes, metadataSource.offset, metadataSource.length);
                content.write(separator);
            }

            BytesRef source = null;
            if (opType == DocWriteRequest.OpType.INDEX || opType == DocWriteRequest.OpType.CREATE) {
                IndexRequest indexRequest = (IndexRequest) request;
                BytesReference indexSource = indexRequest.source();
                XContentType indexXContentType = indexRequest.getContentType();

                try (XContentParser parser = XContentHelper.createParser(NamedXContentRegistry.EMPTY, indexSource, indexXContentType)) {
                    try (XContentBuilder builder = XContentBuilder.builder(bulkContentType.xContent())) {
                        builder.copyCurrentStructure(parser);
                        source = builder.bytes().toBytesRef();
                    }
                }
            } else if (opType == DocWriteRequest.OpType.UPDATE) {
                source = XContentHelper.toXContent((UpdateRequest) request, bulkContentType, false).toBytesRef();
            }

            if (source != null) {
                content.write(source.bytes, source.offset, source.length);
                content.write(separator);
            }
        }

        HttpEntity entity = new ByteArrayEntity(content.toByteArray(), 0, content.size(), requestContentType);
        return new Request(HttpPost.METHOD_NAME, "/_bulk", parameters.getParams(), entity);
    }

    static Request exists(GetRequest getRequest) {
        Request request = get(getRequest);
        return new Request(HttpHead.METHOD_NAME, request.endpoint, request.params, null);
    }

    static Request get(GetRequest getRequest) {
        String endpoint = endpoint(getRequest.index(), getRequest.type(), getRequest.id());

        Params parameters = Params.builder();
        parameters.withPreference(getRequest.preference());
        parameters.withRouting(getRequest.routing());
        parameters.withParent(getRequest.parent());
        parameters.withRefresh(getRequest.refresh());
        parameters.withRealtime(getRequest.realtime());
        parameters.withStoredFields(getRequest.storedFields());
        parameters.withVersion(getRequest.version());
        parameters.withVersionType(getRequest.versionType());
        parameters.withFetchSourceContext(getRequest.fetchSourceContext());

        return new Request(HttpGet.METHOD_NAME, endpoint, parameters.getParams(), null);
    }

    static Request index(IndexRequest indexRequest) {
        String method = Strings.hasLength(indexRequest.id()) ? HttpPut.METHOD_NAME : HttpPost.METHOD_NAME;

        boolean isCreate = (indexRequest.opType() == DocWriteRequest.OpType.CREATE);
        String endpoint = endpoint(indexRequest.index(), indexRequest.type(), indexRequest.id(), isCreate ? "_create" : null);

        Params parameters = Params.builder();
        parameters.withRouting(indexRequest.routing());
        parameters.withParent(indexRequest.parent());
        parameters.withTimeout(indexRequest.timeout());
        parameters.withVersion(indexRequest.version());
        parameters.withVersionType(indexRequest.versionType());
        parameters.withPipeline(indexRequest.getPipeline());
        parameters.withRefreshPolicy(indexRequest.getRefreshPolicy());
        parameters.withWaitForActiveShards(indexRequest.waitForActiveShards());

        BytesRef source = indexRequest.source().toBytesRef();
        ContentType contentType = ContentType.create(indexRequest.getContentType().mediaType());
        HttpEntity entity = new ByteArrayEntity(source.bytes, source.offset, source.length, contentType);

        return new Request(method, endpoint, parameters.getParams(), entity);
    }

    static Request ping() {
        return new Request(HttpHead.METHOD_NAME, "/", Collections.emptyMap(), null);
    }

    static Request update(UpdateRequest updateRequest) throws IOException {
        String endpoint = endpoint(updateRequest.index(), updateRequest.type(), updateRequest.id(), "_update");

        Params parameters = Params.builder();
        parameters.withRouting(updateRequest.routing());
        parameters.withParent(updateRequest.parent());
        parameters.withTimeout(updateRequest.timeout());
        parameters.withRefreshPolicy(updateRequest.getRefreshPolicy());
        parameters.withWaitForActiveShards(updateRequest.waitForActiveShards());
        parameters.withDocAsUpsert(updateRequest.docAsUpsert());
        parameters.withFetchSourceContext(updateRequest.fetchSource());
        parameters.withRetryOnConflict(updateRequest.retryOnConflict());
        parameters.withVersion(updateRequest.version());
        parameters.withVersionType(updateRequest.versionType());

        // The Java API allows update requests with different content types
        // set for the partial document and the upsert document. This client
        // only accepts update requests that have the same content types set
        // for both doc and upsert.
        XContentType xContentType = null;
        if (updateRequest.doc() != null) {
            xContentType = updateRequest.doc().getContentType();
        }
        if (updateRequest.upsertRequest() != null) {
            XContentType upsertContentType = updateRequest.upsertRequest().getContentType();
            if ((xContentType != null) && (xContentType != upsertContentType)) {
                throw new IllegalStateException("Update request cannot have different content types for doc [" + xContentType + "]" +
                        " and upsert [" + upsertContentType + "] documents");
            } else {
                xContentType = upsertContentType;
            }
        }
        if (xContentType == null) {
            xContentType = Requests.INDEX_CONTENT_TYPE;
        }

        BytesRef source = XContentHelper.toXContent(updateRequest, xContentType, false).toBytesRef();
        HttpEntity entity = new ByteArrayEntity(source.bytes, source.offset, source.length, ContentType.create(xContentType.mediaType()));

        return new Request(HttpPost.METHOD_NAME, endpoint, parameters.getParams(), entity);
    }

    /**
     * Utility method to build request's endpoint.
     */
    static String endpoint(String... parts) {
        if (parts == null || parts.length == 0) {
            return DELIMITER;
        }

        StringJoiner joiner = new StringJoiner(DELIMITER, DELIMITER, "");
        for (String part : parts) {
            if (part != null) {
                joiner.add(part);
            }
        }
        return joiner.toString();
    }

    /**
     * Utility class to build request's parameters map and centralize all parameter names.
     */
    static class Params {
        private final Map<String, String> params = new HashMap<>();

        private Params() {
        }

        Params putParam(String key, String value) {
            if (Strings.hasLength(value)) {
                if (params.putIfAbsent(key, value) != null) {
                    throw new IllegalArgumentException("Request parameter [" + key + "] is already registered");
                }
            }
            return this;
        }

        Params putParam(String key, TimeValue value) {
            if (value != null) {
                return putParam(key, value.getStringRep());
            }
            return this;
        }

        Params withDocAsUpsert(boolean docAsUpsert) {
            if (docAsUpsert) {
                return putParam("doc_as_upsert", Boolean.TRUE.toString());
            }
            return this;
        }

        Params withFetchSourceContext(FetchSourceContext fetchSourceContext) {
            if (fetchSourceContext != null) {
                if (fetchSourceContext.fetchSource() == false) {
                    putParam("_source", Boolean.FALSE.toString());
                }
                if (fetchSourceContext.includes() != null && fetchSourceContext.includes().length > 0) {
                    putParam("_source_include", String.join(",", fetchSourceContext.includes()));
                }
                if (fetchSourceContext.excludes() != null && fetchSourceContext.excludes().length > 0) {
                    putParam("_source_exclude", String.join(",", fetchSourceContext.excludes()));
                }
            }
            return this;
        }

        Params withParent(String parent) {
            return putParam("parent", parent);
        }

        Params withPipeline(String pipeline) {
            return putParam("pipeline", pipeline);
        }

        Params withPreference(String preference) {
            return putParam("preference", preference);
        }

        Params withRealtime(boolean realtime) {
            if (realtime == false) {
                return putParam("realtime", Boolean.FALSE.toString());
            }
            return this;
        }

        Params withRefresh(boolean refresh) {
            if (refresh) {
                return withRefreshPolicy(WriteRequest.RefreshPolicy.IMMEDIATE);
            }
            return this;
        }

        Params withRefreshPolicy(WriteRequest.RefreshPolicy refreshPolicy) {
            if (refreshPolicy != WriteRequest.RefreshPolicy.NONE) {
                return putParam("refresh", refreshPolicy.getValue());
            }
            return this;
        }

        Params withRetryOnConflict(int retryOnConflict) {
            if (retryOnConflict > 0) {
                return putParam("retry_on_conflict", String.valueOf(retryOnConflict));
            }
            return this;
        }

        Params withRouting(String routing) {
            return putParam("routing", routing);
        }

        Params withStoredFields(String[] storedFields) {
            if (storedFields != null && storedFields.length > 0) {
                return putParam("stored_fields", String.join(",", storedFields));
            }
            return this;
        }

        Params withTimeout(TimeValue timeout) {
            return putParam("timeout", timeout);
        }

        Params withVersion(long version) {
            if (version != Versions.MATCH_ANY) {
                return putParam("version", Long.toString(version));
            }
            return this;
        }

        Params withVersionType(VersionType versionType) {
            if (versionType != VersionType.INTERNAL) {
                return putParam("version_type", versionType.name().toLowerCase(Locale.ROOT));
            }
            return this;
        }

        Params withWaitForActiveShards(ActiveShardCount activeShardCount) {
            if (activeShardCount != null && activeShardCount != ActiveShardCount.DEFAULT) {
                return putParam("wait_for_active_shards", activeShardCount.toString().toLowerCase(Locale.ROOT));
            }
            return this;
        }

        Map<String, String> getParams() {
            return Collections.unmodifiableMap(params);
        }

        static Params builder() {
            return new Params();
        }
    }

    /**
     * Ensure that the {@link IndexRequest}'s content type is supported by the Bulk API and that it conforms
     * to the current {@link BulkRequest}'s content type (if it's known at the time of this method get called).
     *
     * @return the {@link IndexRequest}'s content type
     */
    static XContentType enforceSameContentType(IndexRequest indexRequest, @Nullable XContentType xContentType) {
        XContentType requestContentType = indexRequest.getContentType();
        if (requestContentType != XContentType.JSON && requestContentType != XContentType.SMILE) {
            throw new IllegalArgumentException("Unsupported content-type found for request with content-type [" + requestContentType
                    + "], only JSON and SMILE are supported");
        }
        if (xContentType == null) {
            return requestContentType;
        }
        if (requestContentType != xContentType) {
            throw new IllegalArgumentException("Mismatching content-type found for request with content-type [" + requestContentType
                    + "], previous requests have content-type [" + xContentType + "]");
        }
        return xContentType;
    }
}
