/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.watcher.condition;

import org.elasticsearch.watcher.WatcherException;

/**
 *
 */
public class ConditionException extends WatcherException {

    public ConditionException(String msg, Object... args) {
        super(msg, args);
    }

    public ConditionException(String msg, Throwable cause, Object... args) {
        super(msg, cause, args);
    }
}
