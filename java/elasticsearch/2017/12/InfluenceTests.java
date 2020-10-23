/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.job.results;

import org.elasticsearch.common.io.stream.Writeable.Reader;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.test.AbstractSerializingTestCase;

import java.util.ArrayList;
import java.util.List;

public class InfluenceTests extends AbstractSerializingTestCase<Influence> {

    @Override
    protected Influence createTestInstance() {
        int size = randomInt(10);
        List<String> fieldValues = new ArrayList<>(size);
        for (int i = 0; i < size; i++) {
            fieldValues.add(randomAlphaOfLengthBetween(1, 20));
        }
        return new Influence(randomAlphaOfLengthBetween(1, 30), fieldValues);
    }

    @Override
    protected Reader<Influence> instanceReader() {
        return Influence::new;
    }

    @Override
    protected Influence doParseInstance(XContentParser parser) {
        return Influence.PARSER.apply(parser, null);
    }

}
