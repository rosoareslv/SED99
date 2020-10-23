/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.watcher.history;

import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.common.component.AbstractComponent;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.index.engine.VersionConflictEngineException;
import org.elasticsearch.watcher.WatcherModule;
import org.elasticsearch.watcher.execution.ExecutionState;
import org.elasticsearch.watcher.support.WatcherIndexTemplateRegistry;
import org.elasticsearch.watcher.support.init.proxy.WatcherClientProxy;
import org.joda.time.DateTime;
import org.joda.time.format.DateTimeFormat;
import org.joda.time.format.DateTimeFormatter;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

import static org.elasticsearch.watcher.support.Exceptions.ioException;

/**
 */
public class HistoryStore extends AbstractComponent {

    public static final String INDEX_PREFIX = ".watcher-history-" + WatcherIndexTemplateRegistry.INDEX_TEMPLATE_VERSION + "-";
    public static final String DOC_TYPE = "watch_record";

    static final DateTimeFormatter indexTimeFormat = DateTimeFormat.forPattern("YYYY.MM.dd");

    private final WatcherClientProxy client;

    private final ReadWriteLock readWriteLock = new ReentrantReadWriteLock();
    private final Lock putUpdateLock = readWriteLock.readLock();
    private final Lock stopLock = readWriteLock.writeLock();
    private final AtomicBoolean started = new AtomicBoolean(false);

    @Inject
    public HistoryStore(Settings settings, WatcherClientProxy client) {
        super(settings);
        this.client = client;
    }

    public void start() {
        started.set(true);
    }

    public void stop() {
        stopLock.lock(); //This will block while put or update actions are underway
        try {
            started.set(false);
        } finally {
            stopLock.unlock();
        }

    }

    /**
     * Stores the specified watchRecord.
     * If the specified watchRecord already was stored this call will fail with a version conflict.
     */
    public void put(WatchRecord watchRecord) throws Exception {
        if (!started.get()) {
            throw new IllegalStateException("unable to persist watch record history store is not ready");
        }
        String index = getHistoryIndexNameForTime(watchRecord.triggerEvent().triggeredTime());
        putUpdateLock.lock();
        try {
            IndexRequest request = new IndexRequest(index, DOC_TYPE, watchRecord.id().value())
                    .source(XContentFactory.jsonBuilder().value(watchRecord))
                    .opType(IndexRequest.OpType.CREATE);
            client.index(request, (TimeValue) null);
        } catch (IOException ioe) {
            throw ioException("failed to persist watch record [{}]", ioe, watchRecord);
        } finally {
            putUpdateLock.unlock();
        }
    }

    /**
     * Stores the specified watchRecord.
     * Any existing watchRecord will be overwritten.
     */
    public void forcePut(WatchRecord watchRecord) throws Exception {
        if (!started.get()) {
            throw new IllegalStateException("unable to persist watch record history store is not ready");
        }
        String index = getHistoryIndexNameForTime(watchRecord.triggerEvent().triggeredTime());
        putUpdateLock.lock();
        try {
            IndexRequest request = new IndexRequest(index, DOC_TYPE, watchRecord.id().value())
                    .source(XContentFactory.jsonBuilder().value(watchRecord))
                    .opType(IndexRequest.OpType.CREATE);
            client.index(request, (TimeValue) null);
        } catch (VersionConflictEngineException vcee) {
            logger.warn("watch record [{}] has executed multiple times, this can happen during watcher restarts", watchRecord);
            watchRecord = new WatchRecord(watchRecord, ExecutionState.EXECUTED_MULTIPLE_TIMES,
                    "watch record has been stored before, previous state [" + watchRecord.state() + "]");
            IndexRequest request = new IndexRequest(index, DOC_TYPE, watchRecord.id().value())
                    .source(XContentFactory.jsonBuilder().value(watchRecord));
            client.index(request, (TimeValue) null);
        } catch (IOException ioe) {
            throw ioException("failed to persist watch record [{}]", ioe, watchRecord);
        } finally {
            putUpdateLock.unlock();
        }
    }

    /**
     * Calculates the correct history index name for a given time
     */
    public static String getHistoryIndexNameForTime(DateTime time) {
        return INDEX_PREFIX + indexTimeFormat.print(time);
    }

}
