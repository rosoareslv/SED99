/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.action;

import org.elasticsearch.action.ActionListener;
import org.elasticsearch.action.support.ActionFilters;
import org.elasticsearch.action.support.HandledTransportAction;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.license.core.License;
import org.elasticsearch.license.plugin.core.LicensesService;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.xpack.XPackBuild;
import org.elasticsearch.xpack.XPackFeatureSet;
import org.elasticsearch.xpack.action.XPackInfoResponse.FeatureSetsInfo.FeatureSet;
import org.elasticsearch.xpack.action.XPackInfoResponse.LicenseInfo;

import java.util.Set;
import java.util.stream.Collectors;

/**
 */
public class TransportXPackInfoAction extends HandledTransportAction<XPackInfoRequest, XPackInfoResponse> {

    private final LicensesService licensesService;
    private final Set<XPackFeatureSet> featureSets;

    @Inject
    public TransportXPackInfoAction(Settings settings, ThreadPool threadPool, TransportService transportService,
                                    ActionFilters actionFilters, IndexNameExpressionResolver indexNameExpressionResolver,
                                    LicensesService licensesService, Set<XPackFeatureSet> featureSets) {
        super(settings, XPackInfoAction.NAME, threadPool, transportService, actionFilters, indexNameExpressionResolver,
                XPackInfoRequest::new);
        this.licensesService = licensesService;
        this.featureSets = featureSets;
    }

    @Override
    protected void doExecute(XPackInfoRequest request, ActionListener<XPackInfoResponse> listener) {


        XPackInfoResponse.BuildInfo buildInfo = null;
        if (request.getCategories().contains(XPackInfoRequest.Category.BUILD)) {
            buildInfo = new XPackInfoResponse.BuildInfo(XPackBuild.CURRENT);
        }

        LicenseInfo licenseInfo = null;
        if (request.getCategories().contains(XPackInfoRequest.Category.LICENSE)) {
            License license = licensesService.getLicense();
            if (license != null) {
                licenseInfo = new LicenseInfo(license);
            }
        }

        XPackInfoResponse.FeatureSetsInfo featureSetsInfo = null;
        if (request.getCategories().contains(XPackInfoRequest.Category.FEATURES)) {
            Set<FeatureSet> featureSets = this.featureSets.stream().map(fs ->
                    new FeatureSet(fs.name(), request.isVerbose() ? fs.description() : null, fs.available(), fs.enabled()))
                    .collect(Collectors.toSet());
            featureSetsInfo = new XPackInfoResponse.FeatureSetsInfo(featureSets);
        }

        listener.onResponse(new XPackInfoResponse(buildInfo, licenseInfo, featureSetsInfo));
    }
}
