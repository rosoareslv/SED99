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

package org.elasticsearch.plan.a;

import java.util.Collections;

public class WhenThingsGoWrongTests extends ScriptTestCase {
    public void testNullPointer() {
        try {
            exec("int x = (int) ((Map) input).get(\"missing\"); return x;");
            fail("should have hit npe");
        } catch (NullPointerException expected) {}
    }

    public void testInvalidShift() {
        try {
            exec("float x = 15F; x <<= 2; return x;");
            fail("should have hit cce");
        } catch (ClassCastException expected) {}

        try {
            exec("double x = 15F; x <<= 2; return x;");
            fail("should have hit cce");
        } catch (ClassCastException expected) {}
    }
    
    public void testBogusParameter() {
        try {
            exec("return 5;", null, Collections.singletonMap("bogusParameterKey", "bogusParameterValue"));
            fail("should have hit IAE");
        } catch (IllegalArgumentException expected) {
            assertTrue(expected.getMessage().contains("Unrecognized compile-time parameter"));
        }
    }
}
