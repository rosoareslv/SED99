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

package org.elasticsearch.index.reindex.remote;

import org.apache.http.HttpEntity;
import org.apache.http.entity.ByteArrayEntity;
import org.apache.http.entity.ContentType;
import org.apache.http.entity.StringEntity;
import org.apache.lucene.util.BytesRef;
import org.elasticsearch.ElasticsearchException;
import org.elasticsearch.Version;
import org.elasticsearch.action.search.SearchRequest;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentHelper;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.json.JsonXContent;
import org.elasticsearch.search.sort.FieldSortBuilder;
import org.elasticsearch.search.sort.SortBuilder;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import static java.util.Collections.singletonMap;

final class RemoteRequestBuilders {
    private RemoteRequestBuilders() {}

    static String initialSearchPath(SearchRequest searchRequest) {
        // It is nasty to build paths with StringBuilder but we'll be careful....
        StringBuilder path = new StringBuilder("/");
        addIndexesOrTypes(path, "Index", searchRequest.indices());
        addIndexesOrTypes(path, "Type", searchRequest.types());
        path.append("_search");
        return path.toString();
    }

    static Map<String, String> initialSearchParams(SearchRequest searchRequest, Version remoteVersion) {
        Map<String, String> params = new HashMap<>();
        if (searchRequest.scroll() != null) {
            params.put("scroll", searchRequest.scroll().keepAlive().toString());
        }
        params.put("size", Integer.toString(searchRequest.source().size()));
        if (searchRequest.source().version() == null || searchRequest.source().version() == true) {
            // false is the only value that makes it false. Null defaults to true....
            params.put("version", null);
        }
        if (searchRequest.source().sorts() != null) {
            boolean useScan = false;
            // Detect if we should use search_type=scan rather than a sort
            if (remoteVersion.before(Version.V_2_1_0)) {
                for (SortBuilder<?> sort : searchRequest.source().sorts()) {
                    if (sort instanceof FieldSortBuilder) {
                        FieldSortBuilder f = (FieldSortBuilder) sort;
                        if (f.getFieldName().equals(FieldSortBuilder.DOC_FIELD_NAME)) {
                            useScan = true;
                            break;
                        }
                    }
                }
            }
            if (useScan) {
                params.put("search_type", "scan");
            } else {
                StringBuilder sorts = new StringBuilder(sortToUri(searchRequest.source().sorts().get(0)));
                for (int i = 1; i < searchRequest.source().sorts().size(); i++) {
                    sorts.append(',').append(sortToUri(searchRequest.source().sorts().get(i)));
                }
                params.put("sorts", sorts.toString());
            }
        }
        if (searchRequest.source().storedFields() != null && false == searchRequest.source().storedFields().fieldNames().isEmpty()) {
            StringBuilder fields = new StringBuilder(searchRequest.source().storedFields().fieldNames().get(0));
            for (int i = 1; i < searchRequest.source().storedFields().fieldNames().size(); i++) {
                fields.append(',').append(searchRequest.source().storedFields().fieldNames().get(i));
            }
            String storedFieldsParamName = remoteVersion.before(Version.V_5_0_0_alpha4) ? "fields" : "stored_fields";
            params.put(storedFieldsParamName, fields.toString());
        }
        return params;
    }

    static HttpEntity initialSearchEntity(BytesReference query) {
        try (XContentBuilder entity = JsonXContent.contentBuilder(); XContentParser queryParser = XContentHelper.createParser(query)) {
            entity.startObject();
            entity.field("query");
            /*
             * We're intentionally a bit paranoid here - copying the query as xcontent rather than writing a raw field. We don't want poorly
             * written queries to escape. Ever.
             */
            entity.copyCurrentStructure(queryParser);
            XContentParser.Token shouldBeEof = queryParser.nextToken();
            if (shouldBeEof != null) {
                throw new ElasticsearchException(
                        "query was more than a single object. This first token after the object is [" + shouldBeEof + "]");
            }
            entity.endObject();
            BytesRef bytes = entity.bytes().toBytesRef();
            return new ByteArrayEntity(bytes.bytes, bytes.offset, bytes.length, ContentType.APPLICATION_JSON);
        } catch (IOException e) {
            throw new ElasticsearchException("unexpected error building entity", e);
        }
    }

    private static void addIndexesOrTypes(StringBuilder path, String name, String[] indicesOrTypes) {
        if (indicesOrTypes == null || indicesOrTypes.length == 0) {
            return;
        }
        for (String indexOrType : indicesOrTypes) {
            checkIndexOrType(name, indexOrType);
        }
        path.append(Strings.arrayToCommaDelimitedString(indicesOrTypes)).append('/');
    }

    private static void checkIndexOrType(String name, String indexOrType) {
        if (indexOrType.indexOf(',') >= 0) {
            throw new IllegalArgumentException(name + " containing [,] not supported but got [" + indexOrType + "]");
        }
        if (indexOrType.indexOf('/') >= 0) {
            throw new IllegalArgumentException(name + " containing [/] not supported but got [" + indexOrType + "]");
        }
    }

    private static String sortToUri(SortBuilder<?> sort) {
        if (sort instanceof FieldSortBuilder) {
            FieldSortBuilder f = (FieldSortBuilder) sort;
            return f.getFieldName() + ":" + f.order();
        }
        throw new IllegalArgumentException("Unsupported sort [" + sort + "]");
    }

    static String scrollPath() {
        return "/_search/scroll";
    }

    static Map<String, String> scrollParams(TimeValue keepAlive) {
        return singletonMap("scroll", keepAlive.toString());
    }

    static HttpEntity scrollEntity(String scroll) {
        return new StringEntity(scroll, ContentType.TEXT_PLAIN);
    }
}
