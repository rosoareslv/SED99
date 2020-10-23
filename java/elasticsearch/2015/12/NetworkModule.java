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

package org.elasticsearch.common.network;

import org.elasticsearch.client.support.Headers;
import org.elasticsearch.client.transport.TransportClientNodesService;
import org.elasticsearch.client.transport.support.TransportProxyClient;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.common.inject.AbstractModule;
import org.elasticsearch.common.io.stream.NamedWriteableRegistry;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.util.ExtensionPoint;
import org.elasticsearch.http.HttpServer;
import org.elasticsearch.http.HttpServerTransport;
import org.elasticsearch.http.netty.NettyHttpServerTransport;
import org.elasticsearch.rest.RestController;
import org.elasticsearch.rest.RestHandler;
import org.elasticsearch.rest.action.admin.cluster.health.RestClusterHealthAction;
import org.elasticsearch.rest.action.admin.cluster.node.hotthreads.RestNodesHotThreadsAction;
import org.elasticsearch.rest.action.admin.cluster.node.info.RestNodesInfoAction;
import org.elasticsearch.rest.action.admin.cluster.node.stats.RestNodesStatsAction;
import org.elasticsearch.rest.action.admin.cluster.repositories.delete.RestDeleteRepositoryAction;
import org.elasticsearch.rest.action.admin.cluster.repositories.get.RestGetRepositoriesAction;
import org.elasticsearch.rest.action.admin.cluster.repositories.put.RestPutRepositoryAction;
import org.elasticsearch.rest.action.admin.cluster.repositories.verify.RestVerifyRepositoryAction;
import org.elasticsearch.rest.action.admin.cluster.reroute.RestClusterRerouteAction;
import org.elasticsearch.rest.action.admin.cluster.settings.RestClusterGetSettingsAction;
import org.elasticsearch.rest.action.admin.cluster.settings.RestClusterUpdateSettingsAction;
import org.elasticsearch.rest.action.admin.cluster.shards.RestClusterSearchShardsAction;
import org.elasticsearch.rest.action.admin.cluster.snapshots.create.RestCreateSnapshotAction;
import org.elasticsearch.rest.action.admin.cluster.snapshots.delete.RestDeleteSnapshotAction;
import org.elasticsearch.rest.action.admin.cluster.snapshots.get.RestGetSnapshotsAction;
import org.elasticsearch.rest.action.admin.cluster.snapshots.restore.RestRestoreSnapshotAction;
import org.elasticsearch.rest.action.admin.cluster.snapshots.status.RestSnapshotsStatusAction;
import org.elasticsearch.rest.action.admin.cluster.state.RestClusterStateAction;
import org.elasticsearch.rest.action.admin.cluster.stats.RestClusterStatsAction;
import org.elasticsearch.rest.action.admin.cluster.tasks.RestPendingClusterTasksAction;
import org.elasticsearch.rest.action.admin.indices.alias.RestIndicesAliasesAction;
import org.elasticsearch.rest.action.admin.indices.alias.delete.RestIndexDeleteAliasesAction;
import org.elasticsearch.rest.action.admin.indices.alias.get.RestGetAliasesAction;
import org.elasticsearch.rest.action.admin.indices.alias.get.RestGetIndicesAliasesAction;
import org.elasticsearch.rest.action.admin.indices.alias.head.RestAliasesExistAction;
import org.elasticsearch.rest.action.admin.indices.alias.put.RestIndexPutAliasAction;
import org.elasticsearch.rest.action.admin.indices.analyze.RestAnalyzeAction;
import org.elasticsearch.rest.action.admin.indices.cache.clear.RestClearIndicesCacheAction;
import org.elasticsearch.rest.action.admin.indices.close.RestCloseIndexAction;
import org.elasticsearch.rest.action.admin.indices.create.RestCreateIndexAction;
import org.elasticsearch.rest.action.admin.indices.delete.RestDeleteIndexAction;
import org.elasticsearch.rest.action.admin.indices.exists.indices.RestIndicesExistsAction;
import org.elasticsearch.rest.action.admin.indices.exists.types.RestTypesExistsAction;
import org.elasticsearch.rest.action.admin.indices.flush.RestFlushAction;
import org.elasticsearch.rest.action.admin.indices.flush.RestSyncedFlushAction;
import org.elasticsearch.rest.action.admin.indices.forcemerge.RestForceMergeAction;
import org.elasticsearch.rest.action.admin.indices.get.RestGetIndicesAction;
import org.elasticsearch.rest.action.admin.indices.mapping.get.RestGetFieldMappingAction;
import org.elasticsearch.rest.action.admin.indices.mapping.get.RestGetMappingAction;
import org.elasticsearch.rest.action.admin.indices.mapping.put.RestPutMappingAction;
import org.elasticsearch.rest.action.admin.indices.open.RestOpenIndexAction;
import org.elasticsearch.rest.action.admin.indices.recovery.RestRecoveryAction;
import org.elasticsearch.rest.action.admin.indices.refresh.RestRefreshAction;
import org.elasticsearch.rest.action.admin.indices.segments.RestIndicesSegmentsAction;
import org.elasticsearch.rest.action.admin.indices.settings.RestGetSettingsAction;
import org.elasticsearch.rest.action.admin.indices.settings.RestUpdateSettingsAction;
import org.elasticsearch.rest.action.admin.indices.shards.RestIndicesShardStoresAction;
import org.elasticsearch.rest.action.admin.indices.stats.RestIndicesStatsAction;
import org.elasticsearch.rest.action.admin.indices.template.delete.RestDeleteIndexTemplateAction;
import org.elasticsearch.rest.action.admin.indices.template.get.RestGetIndexTemplateAction;
import org.elasticsearch.rest.action.admin.indices.template.head.RestHeadIndexTemplateAction;
import org.elasticsearch.rest.action.admin.indices.template.put.RestPutIndexTemplateAction;
import org.elasticsearch.rest.action.admin.indices.upgrade.RestUpgradeAction;
import org.elasticsearch.rest.action.admin.indices.validate.query.RestValidateQueryAction;
import org.elasticsearch.rest.action.admin.indices.validate.template.RestRenderSearchTemplateAction;
import org.elasticsearch.rest.action.admin.indices.warmer.delete.RestDeleteWarmerAction;
import org.elasticsearch.rest.action.admin.indices.warmer.get.RestGetWarmerAction;
import org.elasticsearch.rest.action.admin.indices.warmer.put.RestPutWarmerAction;
import org.elasticsearch.rest.action.bulk.RestBulkAction;
import org.elasticsearch.rest.action.cat.AbstractCatAction;
import org.elasticsearch.rest.action.cat.RestAliasAction;
import org.elasticsearch.rest.action.cat.RestAllocationAction;
import org.elasticsearch.rest.action.cat.RestCatAction;
import org.elasticsearch.rest.action.cat.RestFielddataAction;
import org.elasticsearch.rest.action.cat.RestHealthAction;
import org.elasticsearch.rest.action.cat.RestIndicesAction;
import org.elasticsearch.rest.action.cat.RestMasterAction;
import org.elasticsearch.rest.action.cat.RestNodeAttrsAction;
import org.elasticsearch.rest.action.cat.RestNodesAction;
import org.elasticsearch.rest.action.cat.RestPluginsAction;
import org.elasticsearch.rest.action.cat.RestRepositoriesAction;
import org.elasticsearch.rest.action.cat.RestSegmentsAction;
import org.elasticsearch.rest.action.cat.RestShardsAction;
import org.elasticsearch.rest.action.cat.RestSnapshotAction;
import org.elasticsearch.rest.action.cat.RestThreadPoolAction;
import org.elasticsearch.rest.action.delete.RestDeleteAction;
import org.elasticsearch.rest.action.explain.RestExplainAction;
import org.elasticsearch.rest.action.fieldstats.RestFieldStatsAction;
import org.elasticsearch.rest.action.get.RestGetAction;
import org.elasticsearch.rest.action.get.RestGetSourceAction;
import org.elasticsearch.rest.action.get.RestHeadAction;
import org.elasticsearch.rest.action.get.RestMultiGetAction;
import org.elasticsearch.rest.action.index.RestIndexAction;
import org.elasticsearch.rest.action.main.RestMainAction;
import org.elasticsearch.rest.action.percolate.RestMultiPercolateAction;
import org.elasticsearch.rest.action.percolate.RestPercolateAction;
import org.elasticsearch.rest.action.script.RestDeleteIndexedScriptAction;
import org.elasticsearch.rest.action.script.RestGetIndexedScriptAction;
import org.elasticsearch.rest.action.script.RestPutIndexedScriptAction;
import org.elasticsearch.rest.action.search.RestClearScrollAction;
import org.elasticsearch.rest.action.search.RestMultiSearchAction;
import org.elasticsearch.rest.action.search.RestSearchAction;
import org.elasticsearch.rest.action.search.RestSearchScrollAction;
import org.elasticsearch.rest.action.suggest.RestSuggestAction;
import org.elasticsearch.rest.action.template.RestDeleteSearchTemplateAction;
import org.elasticsearch.rest.action.template.RestGetSearchTemplateAction;
import org.elasticsearch.rest.action.template.RestPutSearchTemplateAction;
import org.elasticsearch.rest.action.termvectors.RestMultiTermVectorsAction;
import org.elasticsearch.rest.action.termvectors.RestTermVectorsAction;
import org.elasticsearch.rest.action.update.RestUpdateAction;
import org.elasticsearch.transport.Transport;
import org.elasticsearch.transport.TransportService;
import org.elasticsearch.transport.local.LocalTransport;
import org.elasticsearch.transport.netty.NettyTransport;

import java.util.Arrays;
import java.util.List;

/**
 * A module to handle registering and binding all network related classes.
 */
public class NetworkModule extends AbstractModule {

    public static final String TRANSPORT_TYPE_KEY = "transport.type";
    public static final String TRANSPORT_SERVICE_TYPE_KEY = "transport.service.type";

    public static final String LOCAL_TRANSPORT = "local";
    public static final String NETTY_TRANSPORT = "netty";

    public static final String HTTP_TYPE_KEY = "http.type";
    public static final String HTTP_ENABLED = "http.enabled";

    private static final List<Class<? extends RestHandler>> builtinRestHandlers = Arrays.asList(
        RestMainAction.class,

        RestNodesInfoAction.class,
        RestNodesStatsAction.class,
        RestNodesHotThreadsAction.class,
        RestClusterStatsAction.class,
        RestClusterStateAction.class,
        RestClusterHealthAction.class,
        RestClusterUpdateSettingsAction.class,
        RestClusterGetSettingsAction.class,
        RestClusterRerouteAction.class,
        RestClusterSearchShardsAction.class,
        RestPendingClusterTasksAction.class,
        RestPutRepositoryAction.class,
        RestGetRepositoriesAction.class,
        RestDeleteRepositoryAction.class,
        RestVerifyRepositoryAction.class,
        RestGetSnapshotsAction.class,
        RestCreateSnapshotAction.class,
        RestRestoreSnapshotAction.class,
        RestDeleteSnapshotAction.class,
        RestSnapshotsStatusAction.class,

        RestIndicesExistsAction.class,
        RestTypesExistsAction.class,
        RestGetIndicesAction.class,
        RestIndicesStatsAction.class,
        RestIndicesSegmentsAction.class,
        RestIndicesShardStoresAction.class,
        RestGetAliasesAction.class,
        RestAliasesExistAction.class,
        RestIndexDeleteAliasesAction.class,
        RestIndexPutAliasAction.class,
        RestIndicesAliasesAction.class,
        RestGetIndicesAliasesAction.class,
        RestCreateIndexAction.class,
        RestDeleteIndexAction.class,
        RestCloseIndexAction.class,
        RestOpenIndexAction.class,

        RestUpdateSettingsAction.class,
        RestGetSettingsAction.class,

        RestAnalyzeAction.class,
        RestGetIndexTemplateAction.class,
        RestPutIndexTemplateAction.class,
        RestDeleteIndexTemplateAction.class,
        RestHeadIndexTemplateAction.class,

        RestPutWarmerAction.class,
        RestDeleteWarmerAction.class,
        RestGetWarmerAction.class,

        RestPutMappingAction.class,
        RestGetMappingAction.class,
        RestGetFieldMappingAction.class,

        RestRefreshAction.class,
        RestFlushAction.class,
        RestSyncedFlushAction.class,
        RestForceMergeAction.class,
        RestUpgradeAction.class,
        RestClearIndicesCacheAction.class,

        RestIndexAction.class,
        RestGetAction.class,
        RestGetSourceAction.class,
        RestHeadAction.class,
        RestMultiGetAction.class,
        RestDeleteAction.class,
        org.elasticsearch.rest.action.count.RestCountAction.class,
        RestSuggestAction.class,
        RestTermVectorsAction.class,
        RestMultiTermVectorsAction.class,
        RestBulkAction.class,
        RestUpdateAction.class,
        RestPercolateAction.class,
        RestMultiPercolateAction.class,

        RestSearchAction.class,
        RestSearchScrollAction.class,
        RestClearScrollAction.class,
        RestMultiSearchAction.class,
        RestRenderSearchTemplateAction.class,

        RestValidateQueryAction.class,

        RestExplainAction.class,

        RestRecoveryAction.class,

        // Templates API
        RestGetSearchTemplateAction.class,
        RestPutSearchTemplateAction.class,
        RestDeleteSearchTemplateAction.class,

        // Scripts API
        RestGetIndexedScriptAction.class,
        RestPutIndexedScriptAction.class,
        RestDeleteIndexedScriptAction.class,

        RestFieldStatsAction.class,

        // no abstract cat action
        RestCatAction.class
    );

    private static final List<Class<? extends AbstractCatAction>> builtinCatHandlers = Arrays.asList(
        RestAllocationAction.class,
        RestShardsAction.class,
        RestMasterAction.class,
        RestNodesAction.class,
        RestIndicesAction.class,
        RestSegmentsAction.class,
        // Fully qualified to prevent interference with rest.action.count.RestCountAction
        org.elasticsearch.rest.action.cat.RestCountAction.class,
        // Fully qualified to prevent interference with rest.action.indices.RestRecoveryAction
        org.elasticsearch.rest.action.cat.RestRecoveryAction.class,
        RestHealthAction.class,
        org.elasticsearch.rest.action.cat.RestPendingClusterTasksAction.class,
        RestAliasAction.class,
        RestThreadPoolAction.class,
        RestPluginsAction.class,
        RestFielddataAction.class,
        RestNodeAttrsAction.class,
        RestRepositoriesAction.class,
        RestSnapshotAction.class
    );

    private final NetworkService networkService;
    private final Settings settings;
    private final boolean transportClient;

    private final ExtensionPoint.SelectedType<TransportService> transportServiceTypes = new ExtensionPoint.SelectedType<>("transport_service", TransportService.class);
    private final ExtensionPoint.SelectedType<Transport> transportTypes = new ExtensionPoint.SelectedType<>("transport", Transport.class);
    private final ExtensionPoint.SelectedType<HttpServerTransport> httpTransportTypes = new ExtensionPoint.SelectedType<>("http_transport", HttpServerTransport.class);
    private final ExtensionPoint.ClassSet<RestHandler> restHandlers = new ExtensionPoint.ClassSet<>("rest_handler", RestHandler.class);
    // we must separate the cat rest handlers so RestCatAction can collect them...
    private final ExtensionPoint.ClassSet<AbstractCatAction> catHandlers = new ExtensionPoint.ClassSet<>("cat_handler", AbstractCatAction.class);

    /**
     * Creates a network module that custom networking classes can be plugged into.
     *
     * @param networkService A constructed network service object to bind.
     * @param settings The settings for the node
     * @param transportClient True if only transport classes should be allowed to be registered, false otherwise.
     */
    public NetworkModule(NetworkService networkService, Settings settings, boolean transportClient) {
        this.networkService = networkService;
        this.settings = settings;
        this.transportClient = transportClient;
        registerTransportService(NETTY_TRANSPORT, TransportService.class);
        registerTransport(LOCAL_TRANSPORT, LocalTransport.class);
        registerTransport(NETTY_TRANSPORT, NettyTransport.class);

        if (transportClient == false) {
            registerHttpTransport(NETTY_TRANSPORT, NettyHttpServerTransport.class);

            for (Class<? extends AbstractCatAction> catAction : builtinCatHandlers) {
                catHandlers.registerExtension(catAction);
            }
            for (Class<? extends RestHandler> restAction : builtinRestHandlers) {
                restHandlers.registerExtension(restAction);
            }
        }
    }

    /** Adds a transport service implementation that can be selected by setting {@link #TRANSPORT_SERVICE_TYPE_KEY}. */
    public void registerTransportService(String name, Class<? extends TransportService> clazz) {
        transportServiceTypes.registerExtension(name, clazz);
    }

    /** Adds a transport implementation that can be selected by setting {@link #TRANSPORT_TYPE_KEY}. */
    public void registerTransport(String name, Class<? extends Transport> clazz) {
        transportTypes.registerExtension(name, clazz);
    }

    /** Adds an http transport implementation that can be selected by setting {@link #HTTP_TYPE_KEY}. */
    // TODO: we need another name than "http transport"....so confusing with transportClient...
    public void registerHttpTransport(String name, Class<? extends HttpServerTransport> clazz) {
        if (transportClient) {
            throw new IllegalArgumentException("Cannot register http transport " + clazz.getName() + " for transport client");
        }
        httpTransportTypes.registerExtension(name, clazz);
    }

    /** Adds an additional rest action. */
    // TODO: change this further to eliminate the middle man, ie RestController, and just register method and path here
    public void registerRestHandler(Class<? extends RestHandler> clazz) {
        if (transportClient) {
            throw new IllegalArgumentException("Cannot register rest handler " + clazz.getName() + " for transport client");
        }
        if (AbstractCatAction.class.isAssignableFrom(clazz)) {
            catHandlers.registerExtension(clazz.asSubclass(AbstractCatAction.class));
        } else {
            restHandlers.registerExtension(clazz);
        }
    }

    @Override
    protected void configure() {
        bind(NetworkService.class).toInstance(networkService);
        bind(NamedWriteableRegistry.class).asEagerSingleton();

        transportServiceTypes.bindType(binder(), settings, TRANSPORT_SERVICE_TYPE_KEY, NETTY_TRANSPORT);
        String defaultTransport = DiscoveryNode.localNode(settings) ? LOCAL_TRANSPORT : NETTY_TRANSPORT;
        transportTypes.bindType(binder(), settings, TRANSPORT_TYPE_KEY, defaultTransport);

        if (transportClient) {
            bind(Headers.class).asEagerSingleton();
            bind(TransportProxyClient.class).asEagerSingleton();
            bind(TransportClientNodesService.class).asEagerSingleton();
        } else {
            if (settings.getAsBoolean(HTTP_ENABLED, true)) {
                bind(HttpServer.class).asEagerSingleton();
                httpTransportTypes.bindType(binder(), settings, HTTP_TYPE_KEY, NETTY_TRANSPORT);
            }
            bind(RestController.class).asEagerSingleton();
            catHandlers.bind(binder());
            restHandlers.bind(binder());
        }
    }
}
