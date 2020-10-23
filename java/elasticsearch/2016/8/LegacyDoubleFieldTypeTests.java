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

import org.elasticsearch.index.mapper.LegacyDoubleFieldMapper;
import org.elasticsearch.index.mapper.MappedFieldType;
import org.elasticsearch.index.mapper.LegacyDoubleFieldMapper.DoubleFieldType;
import org.elasticsearch.index.mapper.MappedFieldType.Relation;
import org.junit.Before;

import java.io.IOException;

public class LegacyDoubleFieldTypeTests extends FieldTypeTestCase {
    @Override
    protected MappedFieldType createDefaultFieldType() {
        return new LegacyDoubleFieldMapper.DoubleFieldType();
    }

    @Before
    public void setupProperties() {
        setDummyNullValue(10.0D);
    }

    public void testIsFieldWithinQuery() throws IOException {
        DoubleFieldType ft = new DoubleFieldType();
        // current impl ignores args and shourd always return INTERSECTS
        assertEquals(Relation.INTERSECTS, ft.isFieldWithinQuery(null, randomDouble(), randomDouble(),
                randomBoolean(), randomBoolean(), null, null));
    }

    public void testValueForSearch() {
        MappedFieldType ft = createDefaultFieldType();
        assertEquals(Double.valueOf(1.2), ft.valueForSearch(1.2));
    }
}
