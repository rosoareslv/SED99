/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack;

import org.bouncycastle.operator.OperatorCreationException;
import org.elasticsearch.SpecialPermission;
import org.elasticsearch.action.ActionRequest;
import org.elasticsearch.action.ActionResponse;
import org.elasticsearch.action.support.ActionFilter;
import org.elasticsearch.bootstrap.BootstrapCheck;
import org.elasticsearch.client.Client;
import org.elasticsearch.client.transport.TransportClient;
import org.elasticsearch.cluster.ClusterState;
import org.elasticsearch.cluster.metadata.IndexNameExpressionResolver;
import org.elasticsearch.cluster.metadata.IndexTemplateMetaData;
import org.elasticsearch.cluster.node.DiscoveryNodes;
import org.elasticsearch.cluster.service.ClusterService;
import org.elasticsearch.common.inject.Binder;
import org.elasticsearch.common.inject.Module;
import org.elasticsearch.common.inject.multibindings.Multibinder;
import org.elasticsearch.common.inject.util.Providers;
import org.elasticsearch.common.io.stream.NamedWriteableRegistry;
import org.elasticsearch.common.network.NetworkService;
import org.elasticsearch.common.settings.ClusterSettings;
import org.elasticsearch.common.settings.IndexScopedSettings;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.settings.SettingsFilter;
import org.elasticsearch.common.util.BigArrays;
import org.elasticsearch.common.util.concurrent.ThreadContext;
import org.elasticsearch.common.xcontent.NamedXContentRegistry;
import org.elasticsearch.env.Environment;
import org.elasticsearch.env.NodeEnvironment;
import org.elasticsearch.http.HttpServerTransport;
import org.elasticsearch.index.IndexModule;
import org.elasticsearch.indices.breaker.CircuitBreakerService;
import org.elasticsearch.ingest.Processor;
import org.elasticsearch.license.LicenseService;
import org.elasticsearch.license.Licensing;
import org.elasticsearch.license.XPackLicenseState;
import org.elasticsearch.plugins.ActionPlugin;
import org.elasticsearch.plugins.ClusterPlugin;
import org.elasticsearch.plugins.IngestPlugin;
import org.elasticsearch.plugins.NetworkPlugin;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.plugins.ScriptPlugin;
import org.elasticsearch.rest.RestController;
import org.elasticsearch.rest.RestHandler;
import org.elasticsearch.script.ScriptContext;
import org.elasticsearch.script.ScriptService;
import org.elasticsearch.threadpool.ExecutorBuilder;
import org.elasticsearch.threadpool.ThreadPool;
import org.elasticsearch.transport.Transport;
import org.elasticsearch.transport.TransportInterceptor;
import org.elasticsearch.watcher.ResourceWatcherService;
import org.elasticsearch.xpack.action.TransportXPackInfoAction;
import org.elasticsearch.xpack.action.TransportXPackUsageAction;
import org.elasticsearch.xpack.action.XPackInfoAction;
import org.elasticsearch.xpack.action.XPackUsageAction;
import org.elasticsearch.xpack.common.http.HttpClient;
import org.elasticsearch.xpack.common.http.HttpRequestTemplate;
import org.elasticsearch.xpack.common.http.HttpSettings;
import org.elasticsearch.xpack.common.http.auth.HttpAuthFactory;
import org.elasticsearch.xpack.common.http.auth.HttpAuthRegistry;
import org.elasticsearch.xpack.common.http.auth.basic.BasicAuth;
import org.elasticsearch.xpack.common.http.auth.basic.BasicAuthFactory;
import org.elasticsearch.xpack.common.text.TextTemplateEngine;
import org.elasticsearch.xpack.deprecation.Deprecation;
import org.elasticsearch.xpack.extensions.XPackExtension;
import org.elasticsearch.xpack.extensions.XPackExtensionsService;
import org.elasticsearch.xpack.graph.Graph;
import org.elasticsearch.xpack.graph.GraphFeatureSet;
import org.elasticsearch.xpack.logstash.Logstash;
import org.elasticsearch.xpack.logstash.LogstashFeatureSet;
import org.elasticsearch.xpack.ml.MachineLearning;
import org.elasticsearch.xpack.ml.MachineLearningFeatureSet;
import org.elasticsearch.xpack.monitoring.Monitoring;
import org.elasticsearch.xpack.monitoring.MonitoringFeatureSet;
import org.elasticsearch.xpack.monitoring.MonitoringSettings;
import org.elasticsearch.xpack.notification.email.Account;
import org.elasticsearch.xpack.notification.email.EmailService;
import org.elasticsearch.xpack.notification.email.attachment.DataAttachmentParser;
import org.elasticsearch.xpack.notification.email.attachment.EmailAttachmentParser;
import org.elasticsearch.xpack.notification.email.attachment.EmailAttachmentsParser;
import org.elasticsearch.xpack.notification.email.attachment.HttpEmailAttachementParser;
import org.elasticsearch.xpack.notification.email.attachment.ReportingAttachmentParser;
import org.elasticsearch.xpack.notification.email.support.BodyPartSource;
import org.elasticsearch.xpack.notification.hipchat.HipChatService;
import org.elasticsearch.xpack.notification.jira.JiraService;
import org.elasticsearch.xpack.notification.pagerduty.PagerDutyAccount;
import org.elasticsearch.xpack.notification.pagerduty.PagerDutyService;
import org.elasticsearch.xpack.notification.slack.SlackService;
import org.elasticsearch.xpack.rest.action.RestXPackInfoAction;
import org.elasticsearch.xpack.rest.action.RestXPackUsageAction;
import org.elasticsearch.xpack.security.InternalClient;
import org.elasticsearch.xpack.security.Security;
import org.elasticsearch.xpack.security.SecurityFeatureSet;
import org.elasticsearch.xpack.security.authc.AuthenticationService;
import org.elasticsearch.xpack.security.authc.support.UsernamePasswordToken;
import org.elasticsearch.xpack.security.crypto.CryptoService;
import org.elasticsearch.xpack.ssl.SSLConfigurationReloader;
import org.elasticsearch.xpack.ssl.SSLService;
import org.elasticsearch.xpack.upgrade.Upgrade;
import org.elasticsearch.xpack.watcher.Watcher;
import org.elasticsearch.xpack.watcher.WatcherFeatureSet;

import javax.security.auth.DestroyFailedException;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.security.AccessController;
import java.security.GeneralSecurityException;
import java.security.PrivilegedAction;
import java.time.Clock;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Supplier;
import java.util.function.UnaryOperator;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import static org.elasticsearch.xpack.watcher.Watcher.ENCRYPT_SENSITIVE_DATA_SETTING;

public class XPackPlugin extends Plugin implements ScriptPlugin, ActionPlugin, IngestPlugin, NetworkPlugin, ClusterPlugin {

    public static final String NAME = "x-pack";

    /** Name constant for the security feature. */
    public static final String SECURITY = "security";

    /** Name constant for the monitoring feature. */
    public static final String MONITORING = "monitoring";

    /** Name constant for the watcher feature. */
    public static final String WATCHER = "watcher";

    /** Name constant for the graph feature. */
    public static final String GRAPH = "graph";

    /** Name constant for the machine learning feature. */
    public static final String MACHINE_LEARNING = "ml";

    /** Name constant for the Logstash feature. */
    public static final String LOGSTASH = "logstash";

    /** Name constant for the Deprecation API feature. */
    public static final String DEPRECATION = "deprecation";

    /** Name constant for the upgrade feature. */
    public static final String UPGRADE = "upgrade";

    // inside of YAML settings we still use xpack do not having handle issues with dashes
    private static final String SETTINGS_NAME = "xpack";

    // TODO: clean up this library to not ask for write access to all system properties!
    static {
        // invoke this clinit in unbound with permissions to access all system properties
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new SpecialPermission());
        }
        try {
            AccessController.doPrivileged(new PrivilegedAction<Void>() {
                @Override
                public Void run() {
                    try {
                        Class.forName("com.unboundid.util.Debug");
                    } catch (ClassNotFoundException e) {
                        throw new RuntimeException(e);
                    }
                    return null;
                }
            });
            // TODO: fix gradle to add all security resources (plugin metadata) to test classpath
            // of watcher plugin, which depends on it directly. This prevents these plugins
            // from being initialized correctly by the test framework, and means we have to
            // have this leniency.
        } catch (ExceptionInInitializerError bogus) {
            if (bogus.getCause() instanceof SecurityException == false) {
                throw bogus; // some other bug
            }
        }
        // some classes need to have their own clinit blocks
        BodyPartSource.init();
        Account.init();
    }

    protected final Settings settings;
    private final Environment env;
    protected boolean transportClientMode;
    protected final XPackExtensionsService extensionsService;

    protected XPackLicenseState licenseState;
    protected SSLService sslService;
    protected Licensing licensing;
    protected Security security;
    protected Monitoring monitoring;
    protected Watcher watcher;
    protected Graph graph;
    protected MachineLearning machineLearning;
    protected Logstash logstash;

    protected Deprecation deprecation;
    protected Upgrade upgrade;

    public XPackPlugin(
            final Settings settings,
            final Path configPath) throws IOException, DestroyFailedException, OperatorCreationException, GeneralSecurityException {
        this.settings = settings;
        this.transportClientMode = transportClientMode(settings);
        this.env = transportClientMode ? null : new Environment(settings, configPath);
        this.licenseState = new XPackLicenseState();
        this.sslService = new SSLService(settings, env);

        this.licensing = new Licensing(settings);
        this.security = new Security(settings, env, licenseState, sslService);
        this.monitoring = new Monitoring(settings, licenseState);
        this.watcher = new Watcher(settings);
        this.graph = new Graph(settings);
        this.machineLearning = new MachineLearning(settings, env, licenseState);
        this.logstash = new Logstash(settings);
        this.deprecation = new Deprecation();
        this.upgrade = new Upgrade(settings);
        // Check if the node is a transport client.
        if (transportClientMode == false) {
            this.extensionsService = new XPackExtensionsService(settings, resolveXPackExtensionsFile(env), getExtensions());
        } else {
            this.extensionsService = null;
        }
    }

    // For tests only
    public Collection<Class<? extends XPackExtension>> getExtensions() {
        return Collections.emptyList();
    }

    // overridable by tests
    protected Clock getClock() {
        return Clock.systemUTC();
    }

    @Override
    public Collection<Module> createGuiceModules() {
        ArrayList<Module> modules = new ArrayList<>();
        modules.add(b -> b.bind(Clock.class).toInstance(getClock()));
        modules.addAll(security.nodeModules());
        modules.addAll(monitoring.nodeModules());
        modules.addAll(watcher.nodeModules());
        modules.addAll(graph.createGuiceModules());
        modules.addAll(machineLearning.nodeModules());
        modules.addAll(logstash.nodeModules());

        if (transportClientMode) {
            modules.add(b -> b.bind(XPackLicenseState.class).toProvider(Providers.of(null)));
        }
        return modules;
    }

    @Override
    public Collection<Object> createComponents(Client client, ClusterService clusterService, ThreadPool threadPool,
                                               ResourceWatcherService resourceWatcherService, ScriptService scriptService,
                                               NamedXContentRegistry xContentRegistry, Environment environment,
                                               NodeEnvironment nodeEnvironment, NamedWriteableRegistry namedWriteableRegistry) {
        List<Object> components = new ArrayList<>();
        components.add(sslService);

        final InternalClient internalClient = new InternalClient(settings, threadPool, client);
        components.add(internalClient);

        LicenseService licenseService = new LicenseService(settings, clusterService, getClock(),
                env, resourceWatcherService, licenseState);
        components.add(licenseService);
        components.add(licenseState);

        try {
            components.addAll(security.createComponents(internalClient, threadPool, clusterService, resourceWatcherService,
                    extensionsService.getExtensions()));
        } catch (final Exception e) {
            throw new IllegalStateException("security initialization failed", e);
        }
        components.addAll(monitoring.createComponents(internalClient, threadPool, clusterService, licenseService, sslService));

        final CryptoService cryptoService;
        try {
            cryptoService = ENCRYPT_SENSITIVE_DATA_SETTING.get(settings) ? new CryptoService(settings) : null;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }

        // watcher http stuff
        Map<String, HttpAuthFactory> httpAuthFactories = new HashMap<>();
        httpAuthFactories.put(BasicAuth.TYPE, new BasicAuthFactory(cryptoService));
        // TODO: add more auth types, or remove this indirection
        HttpAuthRegistry httpAuthRegistry = new HttpAuthRegistry(httpAuthFactories);
        HttpRequestTemplate.Parser httpTemplateParser = new HttpRequestTemplate.Parser(httpAuthRegistry);
        components.add(httpTemplateParser);
        final HttpClient httpClient = new HttpClient(settings, httpAuthRegistry, sslService);
        components.add(httpClient);

        Collection<Object> notificationComponents = createNotificationComponents(clusterService.getClusterSettings(), httpClient,
                httpTemplateParser, scriptService, httpAuthRegistry, cryptoService);
        components.addAll(notificationComponents);

        components.addAll(watcher.createComponents(getClock(), scriptService, internalClient, licenseState,
                httpClient, httpTemplateParser, threadPool, clusterService, cryptoService, xContentRegistry, components));


        components.addAll(machineLearning.createComponents(internalClient, clusterService, threadPool, xContentRegistry));

        components.addAll(logstash.createComponents(internalClient, clusterService));

        components.addAll(upgrade.createComponents(internalClient, clusterService, threadPool, resourceWatcherService,
                scriptService, xContentRegistry));

        // just create the reloader as it will pull all of the loaded ssl configurations and start watching them
        new SSLConfigurationReloader(settings, env, sslService, resourceWatcherService);
        return components;
    }

    private Collection<Object> createNotificationComponents(ClusterSettings clusterSettings, HttpClient httpClient,
                                                            HttpRequestTemplate.Parser httpTemplateParser, ScriptService scriptService,
                                                            HttpAuthRegistry httpAuthRegistry, CryptoService cryptoService) {
        List<Object> components = new ArrayList<>();
        components.add(new EmailService(settings, cryptoService, clusterSettings));
        components.add(new HipChatService(settings, httpClient, clusterSettings));
        components.add(new JiraService(settings, httpClient, clusterSettings));
        components.add(new SlackService(settings, httpClient, clusterSettings));
        components.add(new PagerDutyService(settings, httpClient, clusterSettings));

        TextTemplateEngine textTemplateEngine = new TextTemplateEngine(settings, scriptService);
        components.add(textTemplateEngine);
        Map<String, EmailAttachmentParser> parsers = new HashMap<>();
        parsers.put(HttpEmailAttachementParser.TYPE, new HttpEmailAttachementParser(httpClient, httpTemplateParser, textTemplateEngine));
        parsers.put(DataAttachmentParser.TYPE, new DataAttachmentParser());
        parsers.put(ReportingAttachmentParser.TYPE, new ReportingAttachmentParser(settings, httpClient, textTemplateEngine,
                httpAuthRegistry));
        components.add(new EmailAttachmentsParser(parsers));

        return components;
    }

    @Override
    public Settings additionalSettings() {
        Settings.Builder builder = Settings.builder();
        builder.put(security.additionalSettings());
        builder.put(watcher.additionalSettings());
        builder.put(machineLearning.additionalSettings());
        return builder.build();
    }

    @Override
    public Collection<String> getRestHeaders() {
        if (transportClientMode) {
            return Collections.emptyList();
        }
        Set<String> headers = new HashSet<>();
        headers.add(UsernamePasswordToken.BASIC_AUTH_HEADER);
        if (AuthenticationService.RUN_AS_ENABLED.get(settings)) {
            headers.add(AuthenticationService.RUN_AS_USER_HEADER);
        }
        headers.addAll(extensionsService.getExtensions().stream()
            .flatMap(e -> e.getRestHeaders().stream()).collect(Collectors.toList()));
        return headers;
    }

    @Override
    public List<ScriptContext> getContexts() {
        return Arrays.asList(Watcher.SCRIPT_SEARCH_CONTEXT, Watcher.SCRIPT_EXECUTABLE_CONTEXT, Watcher.SCRIPT_TEMPLATE_CONTEXT);
    }

    @Override
    public List<Setting<?>> getSettings() {
        ArrayList<Setting<?>> settings = new ArrayList<>();
        settings.addAll(Security.getSettings(transportClientMode, extensionsService));
        settings.addAll(MonitoringSettings.getSettings());
        settings.addAll(watcher.getSettings());
        settings.addAll(machineLearning.getSettings());
        settings.addAll(licensing.getSettings());
        settings.addAll(XPackSettings.getAllSettings());

        settings.add(LicenseService.SELF_GENERATED_LICENSE_TYPE);

        // we add the `xpack.version` setting to all internal indices
        settings.add(Setting.simpleString("index.xpack.version", Setting.Property.IndexScope));

        // notification services
        settings.add(SlackService.SLACK_ACCOUNT_SETTING);
        settings.add(EmailService.EMAIL_ACCOUNT_SETTING);
        settings.add(HipChatService.HIPCHAT_ACCOUNT_SETTING);
        settings.add(JiraService.JIRA_ACCOUNT_SETTING);
        settings.add(PagerDutyService.PAGERDUTY_ACCOUNT_SETTING);
        settings.add(ReportingAttachmentParser.RETRIES_SETTING);
        settings.add(ReportingAttachmentParser.INTERVAL_SETTING);

        // http settings
        settings.addAll(HttpSettings.getSettings());
        return settings;
    }

    @Override
    public List<String> getSettingsFilter() {
        List<String> filters = new ArrayList<>();
        filters.add("xpack.notification.email.account.*.smtp.password");
        filters.add("xpack.notification.jira.account.*.password");
        filters.add("xpack.notification.slack.account.*.url");
        filters.add("xpack.notification.pagerduty.account.*.url");
        filters.add("xpack.notification.pagerduty." + PagerDutyAccount.SERVICE_KEY_SETTING);
        filters.add("xpack.notification.pagerduty.account.*." + PagerDutyAccount.SERVICE_KEY_SETTING);
        filters.add("xpack.notification.hipchat.account.*.auth_token");
        filters.addAll(security.getSettingsFilter(extensionsService));
        filters.addAll(MonitoringSettings.getSettingsFilter());
        if (transportClientMode == false) {
            for (XPackExtension extension : extensionsService.getExtensions()) {
                filters.addAll(extension.getSettingsFilter());
            }
        }
        return filters;
    }

    @Override
    public List<ExecutorBuilder<?>> getExecutorBuilders(final Settings settings) {
        List<ExecutorBuilder<?>> executorBuilders = new ArrayList<ExecutorBuilder<?>>();
        executorBuilders.addAll(watcher.getExecutorBuilders(settings));
        executorBuilders.addAll(machineLearning.getExecutorBuilders(settings));
        executorBuilders.addAll(security.getExecutorBuilders(settings));
        return executorBuilders;
    }

    @Override
    public List<ActionHandler<? extends ActionRequest, ? extends ActionResponse>> getActions() {
        List<ActionHandler<? extends ActionRequest, ? extends ActionResponse>> actions = new ArrayList<>();
        actions.add(new ActionHandler<>(XPackInfoAction.INSTANCE, TransportXPackInfoAction.class));
        actions.add(new ActionHandler<>(XPackUsageAction.INSTANCE, TransportXPackUsageAction.class));
        actions.addAll(licensing.getActions());
        actions.addAll(monitoring.getActions());
        actions.addAll(security.getActions());
        actions.addAll(watcher.getActions());
        actions.addAll(graph.getActions());
        actions.addAll(machineLearning.getActions());
        actions.addAll(deprecation.getActions());
        actions.addAll(upgrade.getActions());
        return actions;
    }

    @Override
    public List<Class<? extends ActionFilter>> getActionFilters() {
        List<Class<? extends ActionFilter>> filters = new ArrayList<>();
        filters.addAll(licensing.getActionFilters());
        filters.addAll(monitoring.getActionFilters());
        filters.addAll(security.getActionFilters());
        filters.addAll(watcher.getActionFilters());
        filters.addAll(machineLearning.getActionFilters());
        filters.addAll(upgrade.getActionFilters());
        return filters;
    }

    @Override
    public List<RestHandler> getRestHandlers(Settings settings, RestController restController, ClusterSettings clusterSettings,
            IndexScopedSettings indexScopedSettings, SettingsFilter settingsFilter, IndexNameExpressionResolver indexNameExpressionResolver,
            Supplier<DiscoveryNodes> nodesInCluster) {
        List<RestHandler> handlers = new ArrayList<>();
        handlers.add(new RestXPackInfoAction(settings, restController));
        handlers.add(new RestXPackUsageAction(settings, restController));
        handlers.addAll(licensing.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(monitoring.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(security.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(watcher.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(graph.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(machineLearning.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(deprecation.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
            indexNameExpressionResolver, nodesInCluster));
        handlers.addAll(upgrade.getRestHandlers(settings, restController, clusterSettings, indexScopedSettings, settingsFilter,
                indexNameExpressionResolver, nodesInCluster));
        return handlers;
    }

    @Override
    public Map<String, Processor.Factory> getProcessors(Processor.Parameters parameters) {
        return security.getProcessors(parameters);
    }

    @Override
    public List<NamedWriteableRegistry.Entry> getNamedWriteables() {
        List<NamedWriteableRegistry.Entry> entries = new ArrayList<>();
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, SECURITY, SecurityFeatureSet.Usage::new));
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, WATCHER, WatcherFeatureSet.Usage::new));
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, MONITORING, MonitoringFeatureSet.Usage::new));
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, GRAPH, GraphFeatureSet.Usage::new));
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, MACHINE_LEARNING, MachineLearningFeatureSet.Usage::new));
        entries.add(new NamedWriteableRegistry.Entry(XPackFeatureSet.Usage.class, LOGSTASH, LogstashFeatureSet.Usage::new));
        entries.addAll(watcher.getNamedWriteables());
        entries.addAll(machineLearning.getNamedWriteables());
        entries.addAll(licensing.getNamedWriteables());
        entries.addAll(Security.getNamedWriteables());
        return entries;
    }

    @Override
    public List<NamedXContentRegistry.Entry> getNamedXContent() {
        List<NamedXContentRegistry.Entry> entries = new ArrayList<>();
        entries.addAll(watcher.getNamedXContent());
        entries.addAll(machineLearning.getNamedXContent());
        entries.addAll(licensing.getNamedXContent());
        return entries;
    }

    @Override
    public UnaryOperator<Map<String, IndexTemplateMetaData>> getIndexTemplateMetaDataUpgrader() {
        return templates -> {
            templates = watcher.getIndexTemplateMetaDataUpgrader().apply(templates);
            templates = security.getIndexTemplateMetaDataUpgrader().apply(templates);
            return templates;
        };
    }

    public void onIndexModule(IndexModule module) {
        security.onIndexModule(module);
        watcher.onIndexModule(module);
    }

    public static void bindFeatureSet(Binder binder, Class<? extends XPackFeatureSet> featureSet) {
        binder.bind(featureSet).asEagerSingleton();
        Multibinder<XPackFeatureSet> featureSetBinder = Multibinder.newSetBinder(binder, XPackFeatureSet.class);
        featureSetBinder.addBinding().to(featureSet);
    }

    public static boolean transportClientMode(Settings settings) {
        return TransportClient.CLIENT_TYPE.equals(settings.get(Client.CLIENT_TYPE_SETTING_S.getKey()));
    }

    public static boolean isTribeNode(Settings settings) {
        return settings.getGroups("tribe", true).isEmpty() == false;
    }

    public static boolean isTribeClientNode(Settings settings) {
        return settings.get("tribe.name") != null;
    }

    public static Path resolveConfigFile(Environment env, String name) {
        return env.configFile().resolve(NAME).resolve(name);
    }

    public static String featureSettingPrefix(String featureName) {
        return SETTINGS_NAME + "." + featureName;
    }

    public static Path resolveXPackExtensionsFile(Environment env) {
        return env.pluginsFile().resolve(XPackPlugin.NAME).resolve("extensions");
    }

    @Override
    public List<TransportInterceptor> getTransportInterceptors(NamedWriteableRegistry namedWriteableRegistry, ThreadContext threadContext) {
        return security.getTransportInterceptors(namedWriteableRegistry, threadContext);
    }

    @Override
    public Map<String, Supplier<Transport>> getTransports(Settings settings, ThreadPool threadPool, BigArrays bigArrays,
                                                          CircuitBreakerService circuitBreakerService,
                                                          NamedWriteableRegistry namedWriteableRegistry,
                                                          NetworkService networkService) {
        return security.getTransports(settings, threadPool, bigArrays, circuitBreakerService, namedWriteableRegistry, networkService);
    }

    @Override
    public Map<String, Supplier<HttpServerTransport>> getHttpTransports(Settings settings, ThreadPool threadPool, BigArrays bigArrays,
                                                                        CircuitBreakerService circuitBreakerService,
                                                                        NamedWriteableRegistry namedWriteableRegistry,
                                                                        NamedXContentRegistry xContentRegistry,
                                                                        NetworkService networkService,
                                                                        HttpServerTransport.Dispatcher dispatcher) {
        return security.getHttpTransports(settings, threadPool, bigArrays, circuitBreakerService, namedWriteableRegistry, xContentRegistry,
                networkService, dispatcher);
    }

    @Override
    public UnaryOperator<RestHandler> getRestHandlerWrapper(ThreadContext threadContext) {
        return security.getRestHandlerWrapper(threadContext);
    }

    @Override
    public List<BootstrapCheck> getBootstrapChecks() {
        return Collections.unmodifiableList(
                Stream.of(security.getBootstrapChecks(), watcher.getBootstrapChecks())
                        .flatMap(Collection::stream)
                        .collect(Collectors.toList()));
    }

    @Override
    public Map<String, Supplier<ClusterState.Custom>> getInitialClusterStateCustomSupplier() {
        return security.getInitialClusterStateCustomSupplier();
    }
}
