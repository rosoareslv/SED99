/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.watcher.input.simple;

import org.elasticsearch.ElasticsearchParseException;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.xpack.watcher.input.ExecutableInput;
import org.elasticsearch.xpack.watcher.input.Input;
import org.elasticsearch.xpack.watcher.input.InputFactory;
import org.elasticsearch.xpack.watcher.watch.Payload;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.elasticsearch.common.xcontent.XContentFactory.jsonBuilder;
import static org.hamcrest.Matchers.containsString;

public class SimpleInputTests extends ESTestCase {

    public void testExecute() throws Exception {
        Map<String, Object> data = new HashMap<>();
        data.put("foo", "bar");
        data.put("baz", new ArrayList<String>() );
        ExecutableInput staticInput = new ExecutableSimpleInput(new SimpleInput(new Payload.Simple(data)), logger);

        Input.Result staticResult = staticInput.execute(null, new Payload.Simple());
        assertEquals(staticResult.payload().data().get("foo"), "bar");
        List baz = (List)staticResult.payload().data().get("baz");
        assertTrue(baz.isEmpty());
    }

    public void testParserValid() throws Exception {
        Map<String, Object> data = new HashMap<>();
        data.put("foo", "bar");
        data.put("baz", new ArrayList<String>());

        XContentBuilder jsonBuilder = jsonBuilder().map(data);
        InputFactory parser = new SimpleInputFactory(Settings.builder().build());
        XContentParser xContentParser = createParser(jsonBuilder);
        xContentParser.nextToken();
        ExecutableInput input = parser.parseExecutable("_id", xContentParser);
        assertEquals(input.type(), SimpleInput.TYPE);


        Input.Result staticResult = input.execute(null, new Payload.Simple());
        assertEquals(staticResult.payload().data().get("foo"), "bar");
        List baz = (List)staticResult.payload().data().get("baz");
        assertTrue(baz.isEmpty());
    }

    public void testParserInvalid() throws Exception {
        XContentBuilder jsonBuilder = jsonBuilder().value("just a string");

        InputFactory parser = new SimpleInputFactory(Settings.builder().build());
        XContentParser xContentParser = createParser(jsonBuilder);
        xContentParser.nextToken();
        try {
            parser.parseInput("_id", xContentParser);
            fail("[simple] input parse should fail with an InputException for an empty json object");
        } catch (ElasticsearchParseException e) {
            assertThat(e.getMessage(), containsString("expected an object but found [VALUE_STRING] instead"));
        }
    }
}
