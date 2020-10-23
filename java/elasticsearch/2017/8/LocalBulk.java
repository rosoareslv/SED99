/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.monitoring.exporter.local;

import org.apache.logging.log4j.Logger;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.bulk.BulkItemResponse;
import org.elasticsearch.action.bulk.BulkRequestBuilder;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.xpack.monitoring.exporter.ExportBulk;
import org.elasticsearch.xpack.monitoring.exporter.ExportException;
import org.elasticsearch.xpack.monitoring.exporter.MonitoringDoc;
import org.elasticsearch.xpack.monitoring.exporter.MonitoringTemplateUtils;
import org.elasticsearch.xpack.monitoring.resolver.MonitoringIndexNameResolver;
import org.elasticsearch.xpack.monitoring.resolver.ResolversRegistry;
import org.elasticsearch.xpack.security.InternalClient;

import java.util.Arrays;
import java.util.Collection;

/**
 * LocalBulk exports monitoring data in the local cluster using bulk requests. Its usage is not thread safe since the
 * {@link LocalBulk#add(Collection)}, {@link LocalBulk#flush(org.elasticsearch.action.ActionListener)} and
 * {@link LocalBulk#doClose(ActionListener)} methods are not synchronized.
 */
public class LocalBulk extends ExportBulk {

    private final Logger logger;
    private final InternalClient client;
    private final ResolversRegistry resolvers;
    private final boolean usePipeline;

    private BulkRequestBuilder requestBuilder;


    public LocalBulk(String name, Logger logger, InternalClient client, ResolversRegistry resolvers, boolean usePipeline) {
        super(name, client.threadPool().getThreadContext());
        this.logger = logger;
        this.client = client;
        this.resolvers = resolvers;
        this.usePipeline = usePipeline;
    }

    @Override
    public void doAdd(Collection<MonitoringDoc> docs) throws ExportException {
        ExportException exception = null;

        for (MonitoringDoc doc : docs) {
            if (isClosed()) {
                return;
            }
            if (requestBuilder == null) {
                requestBuilder = client.prepareBulk();
            }

            try {
                MonitoringIndexNameResolver<MonitoringDoc> resolver = resolvers.getResolver(doc);
                IndexRequest request = new IndexRequest(resolver.index(doc), "doc");
                if (Strings.hasText(doc.getId())) {
                    request.id(doc.getId());
                }
                request.source(resolver.source(doc, XContentType.SMILE), XContentType.SMILE);

                // allow the use of ingest pipelines to be completely optional
                if (usePipeline) {
                    request.setPipeline(MonitoringTemplateUtils.pipelineName(MonitoringTemplateUtils.TEMPLATE_VERSION));
                }

                requestBuilder.add(request);

                if (logger.isTraceEnabled()) {
                    logger.trace("local exporter [{}] - added index request [index={}, type={}, id={}, pipeline={}]",
                                 name, request.index(), request.type(), request.id(), request.getPipeline());
                }
            } catch (Exception e) {
                if (exception == null) {
                    exception = new ExportException("failed to add documents to export bulk [{}]", name);
                }
                exception.addExportException(new ExportException("failed to add document [{}]", e, doc, name));
            }
        }

        if (exception != null) {
            throw exception;
        }
    }

    @Override
    public void doFlush(ActionListener<Void> listener) {
        if (requestBuilder == null || requestBuilder.numberOfActions() == 0 || isClosed()) {
            listener.onResponse(null);
        } else {
            try {
                logger.trace("exporter [{}] - exporting {} documents", name, requestBuilder.numberOfActions());
                requestBuilder.execute(ActionListener.wrap(bulkResponse -> {
                    if (bulkResponse.hasFailures()) {
                        throwExportException(bulkResponse.getItems(), listener);
                    } else {
                        listener.onResponse(null);
                    }
                }, e -> listener.onFailure(new ExportException("failed to flush export bulk [{}]", e, name))));
            } finally {
                requestBuilder = null;
            }
        }
    }

    void throwExportException(BulkItemResponse[] bulkItemResponses, ActionListener<Void> listener) {
        ExportException exception = new ExportException("bulk [{}] reports failures when exporting documents", name);

        Arrays.stream(bulkItemResponses)
                .filter(BulkItemResponse::isFailed)
                .map(item -> new ExportException(item.getFailure().getCause()))
                .forEach(exception::addExportException);

        if (exception.hasExportExceptions()) {
            for (ExportException e : exception) {
                logger.warn("unexpected error while indexing monitoring document", e);
            }
            listener.onFailure(exception);
        } else {
            listener.onResponse(null);
        }
    }

    @Override
    protected void doClose(ActionListener<Void> listener) {
        if (isClosed() == false) {
            requestBuilder = null;
        }
        listener.onResponse(null);
    }
}
