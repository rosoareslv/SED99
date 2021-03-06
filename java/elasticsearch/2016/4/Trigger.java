/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.watcher.trigger;

import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentParser;

import java.io.IOException;

/**
 *
 */
public interface Trigger extends ToXContent {

    String type();

    interface Parser<T extends Trigger> {

        String type();

        T parse(XContentParser parser) throws IOException;
    }

    interface Builder<T extends Trigger> {

        T build();
    }

}
