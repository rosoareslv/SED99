/*
x * Licensed to Elasticsearch under one or more contributor
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

package org.elasticsearch.search.sort;

import org.apache.lucene.search.SortField;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.json.JsonXContent;
import org.elasticsearch.index.query.QueryParseContext;
import org.elasticsearch.search.DocValueFormat;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;

public class FieldSortBuilderTests extends AbstractSortTestCase<FieldSortBuilder> {

    @Override
    protected FieldSortBuilder createTestItem() {
        return randomFieldSortBuilder();
    }

    private List<Object> missingContent = Arrays.asList(
            "_last",
            "_first",
            Integer.toString(randomInt()),
            randomInt());


    public FieldSortBuilder randomFieldSortBuilder() {
        String fieldName = rarely() ? FieldSortBuilder.DOC_FIELD_NAME : randomAlphaOfLengthBetween(1, 10);
        FieldSortBuilder builder = new FieldSortBuilder(fieldName);
        if (randomBoolean()) {
            builder.order(randomFrom(SortOrder.values()));
        }

        if (randomBoolean()) {
            builder.missing(randomFrom(missingContent));
        }

        if (randomBoolean()) {
            builder.unmappedType(randomAlphaOfLengthBetween(1, 10));
        }

        if (randomBoolean()) {
            builder.sortMode(randomFrom(SortMode.values()));
        }

        if (randomBoolean()) {
            builder.setNestedFilter(randomNestedFilter());
        }

        if (randomBoolean()) {
            builder.setNestedPath(randomAlphaOfLengthBetween(1, 10));
        }

        return builder;
    }

    @Override
    protected FieldSortBuilder mutate(FieldSortBuilder original) throws IOException {
        FieldSortBuilder mutated = new FieldSortBuilder(original);
        int parameter = randomIntBetween(0, 5);
        switch (parameter) {
        case 0:
            mutated.setNestedPath(randomValueOtherThan(
                    original.getNestedPath(),
                    () -> randomAlphaOfLengthBetween(1, 10)));
            break;
        case 1:
            mutated.setNestedFilter(randomValueOtherThan(
                    original.getNestedFilter(),
                    () -> randomNestedFilter()));
            break;
        case 2:
            mutated.sortMode(randomValueOtherThan(original.sortMode(), () -> randomFrom(SortMode.values())));
            break;
        case 3:
            mutated.unmappedType(randomValueOtherThan(
                    original.unmappedType(),
                    () -> randomAlphaOfLengthBetween(1, 10)));
            break;
        case 4:
            mutated.missing(randomValueOtherThan(original.missing(), () -> randomFrom(missingContent)));
            break;
        case 5:
            mutated.order(randomValueOtherThan(original.order(), () -> randomFrom(SortOrder.values())));
            break;
        default:
            throw new IllegalStateException("Unsupported mutation.");
        }
        return mutated;
    }

    @Override
    protected void sortFieldAssertions(FieldSortBuilder builder, SortField sortField, DocValueFormat format) throws IOException {
        SortField.Type expectedType;
        if (builder.getFieldName().equals(FieldSortBuilder.DOC_FIELD_NAME)) {
            expectedType = SortField.Type.DOC;
        } else {
            expectedType = SortField.Type.CUSTOM;
        }
        assertEquals(expectedType, sortField.getType());
        assertEquals(builder.order() == SortOrder.ASC ? false : true, sortField.getReverse());
        if (expectedType == SortField.Type.CUSTOM) {
            assertEquals(builder.getFieldName(), sortField.getField());
        }
        assertEquals(DocValueFormat.RAW, format);
    }

    public void testReverseOptionFails() throws IOException {
        String json = "{ \"post_date\" : {\"reverse\" : true} },\n";

        XContentParser parser = createParser(JsonXContent.jsonXContent, json);
        // need to skip until parser is located on second START_OBJECT
        parser.nextToken();
        parser.nextToken();
        parser.nextToken();

        QueryParseContext context = new QueryParseContext(parser);

        try {
          FieldSortBuilder.fromXContent(context, "");
          fail("adding reverse sorting option should fail with an exception");
        } catch (IllegalArgumentException e) {
            // all good
        }
    }


    @Override
    protected FieldSortBuilder fromXContent(QueryParseContext context, String fieldName) throws IOException {
        return FieldSortBuilder.fromXContent(context, fieldName);
    }
}
