/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.watcher.actions;

import org.elasticsearch.common.logging.support.LoggerMessageFormat;
import org.elasticsearch.watcher.WatcherException;

/**
 *
 */
public class ActionException extends WatcherException {

    public ActionException(String msg, Object... args) {
        super(LoggerMessageFormat.format(msg, args));
    }

    public ActionException(String msg, Throwable cause, Object... args) {
        super(LoggerMessageFormat.format(msg, args), cause);
    }

}
