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

import org.elasticsearch.index.mapper.LegacyFloatFieldMapper;
import org.elasticsearch.index.mapper.MappedFieldType;
import org.elasticsearch.index.mapper.LegacyFloatFieldMapper.FloatFieldType;
import org.elasticsearch.index.mapper.MappedFieldType.Relation;
import org.junit.Before;

import java.io.IOException;

public class LegacyFloatFieldTypeTests extends FieldTypeTestCase {
    @Override
    protected MappedFieldType createDefaultFieldType() {
        return new LegacyFloatFieldMapper.FloatFieldType();
    }

    @Before
    public void setupProperties() {
        setDummyNullValue(10.0f);
    }

    public void testIsFieldWithinQuery() throws IOException {
        FloatFieldType ft = new FloatFieldType();
        // current impl ignores args and shourd always return INTERSECTS
        assertEquals(Relation.INTERSECTS, ft.isFieldWithinQuery(null, randomFloat(), randomFloat(),
                randomBoolean(), randomBoolean(), null, null));
    }

    public void testValueForSearch() {
        MappedFieldType ft = createDefaultFieldType();
        assertEquals(Float.valueOf(1.2f), ft.valueForSearch(1.2f));
    }
}
