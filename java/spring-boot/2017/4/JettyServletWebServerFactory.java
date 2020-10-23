/*
 * Copyright 2012-2017 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.web.embedded.jetty;

import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URL;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import javax.servlet.ServletException;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.eclipse.jetty.http.HttpVersion;
import org.eclipse.jetty.http.MimeTypes;
import org.eclipse.jetty.server.AbstractConnector;
import org.eclipse.jetty.server.ConnectionFactory;
import org.eclipse.jetty.server.Connector;
import org.eclipse.jetty.server.ForwardedRequestCustomizer;
import org.eclipse.jetty.server.Handler;
import org.eclipse.jetty.server.HttpConfiguration;
import org.eclipse.jetty.server.HttpConnectionFactory;
import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.server.SecureRequestCustomizer;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.server.ServerConnector;
import org.eclipse.jetty.server.SslConnectionFactory;
import org.eclipse.jetty.server.handler.ErrorHandler;
import org.eclipse.jetty.server.handler.HandlerWrapper;
import org.eclipse.jetty.server.handler.gzip.GzipHandler;
import org.eclipse.jetty.server.session.DefaultSessionCache;
import org.eclipse.jetty.server.session.FileSessionDataStore;
import org.eclipse.jetty.server.session.SessionHandler;
import org.eclipse.jetty.servlet.ErrorPageErrorHandler;
import org.eclipse.jetty.servlet.ServletHolder;
import org.eclipse.jetty.servlet.ServletMapping;
import org.eclipse.jetty.util.resource.JarResource;
import org.eclipse.jetty.util.resource.Resource;
import org.eclipse.jetty.util.resource.ResourceCollection;
import org.eclipse.jetty.util.ssl.SslContextFactory;
import org.eclipse.jetty.util.thread.ThreadPool;
import org.eclipse.jetty.webapp.AbstractConfiguration;
import org.eclipse.jetty.webapp.Configuration;
import org.eclipse.jetty.webapp.WebAppContext;

import org.springframework.boot.web.server.Compression;
import org.springframework.boot.web.server.ErrorPage;
import org.springframework.boot.web.server.MimeMappings;
import org.springframework.boot.web.server.Ssl;
import org.springframework.boot.web.server.Ssl.ClientAuth;
import org.springframework.boot.web.server.WebServer;
import org.springframework.boot.web.server.WebServerException;
import org.springframework.boot.web.servlet.ServletContextInitializer;
import org.springframework.boot.web.servlet.server.AbstractServletWebServerFactory;
import org.springframework.boot.web.servlet.server.ServletWebServerFactory;
import org.springframework.context.ResourceLoaderAware;
import org.springframework.core.io.ResourceLoader;
import org.springframework.util.Assert;
import org.springframework.util.ObjectUtils;
import org.springframework.util.ResourceUtils;
import org.springframework.util.StringUtils;

/**
 * {@link ServletWebServerFactory} that can be used to create a {@link JettyWebServer}.
 * Can be initialized using Spring's {@link ServletContextInitializer}s or Jetty
 * {@link Configuration}s.
 * <p>
 * Unless explicitly configured otherwise this factory will created servers that listens
 * for HTTP requests on port 8080.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Andrey Hihlovskiy
 * @author Andy Wilkinson
 * @author Eddú Meléndez
 * @author Venil Noronha
 * @author Henri Kerola
 * @since 2.0.0
 * @see #setPort(int)
 * @see #setConfigurations(Collection)
 * @see JettyWebServer
 */
public class JettyServletWebServerFactory extends AbstractServletWebServerFactory
		implements ResourceLoaderAware {

	private List<Configuration> configurations = new ArrayList<>();

	private boolean useForwardHeaders;

	/**
	 * The number of acceptor threads to use.
	 */
	private int acceptors = -1;

	/**
	 * The number of selector threads to use.
	 */
	private int selectors = -1;

	private List<JettyServerCustomizer> jettyServerCustomizers = new ArrayList<>();

	private ResourceLoader resourceLoader;

	private ThreadPool threadPool;

	/**
	 * Create a new {@link JettyServletWebServerFactory} instance.
	 */
	public JettyServletWebServerFactory() {
		super();
	}

	/**
	 * Create a new {@link JettyServletWebServerFactory} that listens for requests using
	 * the specified port.
	 * @param port the port to listen on
	 */
	public JettyServletWebServerFactory(int port) {
		super(port);
	}

	/**
	 * Create a new {@link JettyServletWebServerFactory} with the specified context path
	 * and port.
	 * @param contextPath the root context path
	 * @param port the port to listen on
	 */
	public JettyServletWebServerFactory(String contextPath, int port) {
		super(contextPath, port);
	}

	@Override
	public WebServer getWebServer(ServletContextInitializer... initializers) {
		JettyEmbeddedWebAppContext context = new JettyEmbeddedWebAppContext();
		int port = (getPort() >= 0 ? getPort() : 0);
		InetSocketAddress address = new InetSocketAddress(getAddress(), port);
		Server server = createServer(address);
		configureWebAppContext(context, initializers);
		server.setHandler(addHandlerWrappers(context));
		this.logger.info("Server initialized with port: " + port);
		if (getSsl() != null && getSsl().isEnabled()) {
			SslContextFactory sslContextFactory = new SslContextFactory();
			configureSsl(sslContextFactory, getSsl());
			AbstractConnector connector = createSslConnector(server, sslContextFactory,
					port);
			server.setConnectors(new Connector[] { connector });
		}
		for (JettyServerCustomizer customizer : getServerCustomizers()) {
			customizer.customize(server);
		}
		if (this.useForwardHeaders) {
			new ForwardHeadersCustomizer().customize(server);
		}
		return getJettyWebServer(server);
	}

	private Server createServer(InetSocketAddress address) {
		Server server = new Server(getThreadPool());
		server.setConnectors(new Connector[] { createConnector(address, server) });
		return server;
	}

	private AbstractConnector createConnector(InetSocketAddress address, Server server) {
		ServerConnector connector = new ServerConnector(server, this.acceptors,
				this.selectors);
		connector.setHost(address.getHostName());
		connector.setPort(address.getPort());
		for (ConnectionFactory connectionFactory : connector.getConnectionFactories()) {
			if (connectionFactory instanceof HttpConfiguration.ConnectionFactory) {
				((HttpConfiguration.ConnectionFactory) connectionFactory)
						.getHttpConfiguration().setSendServerVersion(false);
			}
		}
		return connector;
	}

	private AbstractConnector createSslConnector(Server server,
			SslContextFactory sslContextFactory, int port) {
		HttpConfiguration config = new HttpConfiguration();
		config.setSendServerVersion(false);
		config.addCustomizer(new SecureRequestCustomizer());
		HttpConnectionFactory connectionFactory = new HttpConnectionFactory(config);
		SslConnectionFactory sslConnectionFactory = new SslConnectionFactory(
				sslContextFactory, HttpVersion.HTTP_1_1.asString());
		ServerConnector serverConnector = new ServerConnector(server,
				sslConnectionFactory, connectionFactory);
		serverConnector.setPort(port);
		return serverConnector;
	}

	private Handler addHandlerWrappers(Handler handler) {
		if (getCompression() != null && getCompression().getEnabled()) {
			handler = applyWrapper(handler, createGzipHandler());
		}
		if (StringUtils.hasText(getServerHeader())) {
			handler = applyWrapper(handler, new ServerHeaderHandler(getServerHeader()));
		}
		return handler;
	}

	private Handler applyWrapper(Handler handler, HandlerWrapper wrapper) {
		wrapper.setHandler(handler);
		return wrapper;
	}

	private HandlerWrapper createGzipHandler() {
		GzipHandler handler = new GzipHandler();
		Compression compression = getCompression();
		handler.setMinGzipSize(compression.getMinResponseSize());
		handler.setIncludedMimeTypes(compression.getMimeTypes());
		if (compression.getExcludedUserAgents() != null) {
			handler.setExcludedAgentPatterns(compression.getExcludedUserAgents());
		}
		return handler;
	}

	/**
	 * Configure the SSL connection.
	 * @param factory the Jetty {@link SslContextFactory}.
	 * @param ssl the ssl details.
	 */
	protected void configureSsl(SslContextFactory factory, Ssl ssl) {
		factory.setProtocol(ssl.getProtocol());
		configureSslClientAuth(factory, ssl);
		configureSslPasswords(factory, ssl);
		factory.setCertAlias(ssl.getKeyAlias());
		if (!ObjectUtils.isEmpty(ssl.getCiphers())) {
			factory.setIncludeCipherSuites(ssl.getCiphers());
			factory.setExcludeCipherSuites();
		}
		if (ssl.getEnabledProtocols() != null) {
			factory.setIncludeProtocols(ssl.getEnabledProtocols());
		}
		if (getSslStoreProvider() != null) {
			try {
				factory.setKeyStore(getSslStoreProvider().getKeyStore());
				factory.setTrustStore(getSslStoreProvider().getTrustStore());
			}
			catch (Exception ex) {
				throw new IllegalStateException("Unable to set SSL store", ex);
			}
		}
		else {
			configureSslKeyStore(factory, ssl);
			configureSslTrustStore(factory, ssl);
		}
	}

	private void configureSslClientAuth(SslContextFactory factory, Ssl ssl) {
		if (ssl.getClientAuth() == ClientAuth.NEED) {
			factory.setNeedClientAuth(true);
			factory.setWantClientAuth(true);
		}
		else if (ssl.getClientAuth() == ClientAuth.WANT) {
			factory.setWantClientAuth(true);
		}
	}

	private void configureSslPasswords(SslContextFactory factory, Ssl ssl) {
		if (ssl.getKeyStorePassword() != null) {
			factory.setKeyStorePassword(ssl.getKeyStorePassword());
		}
		if (ssl.getKeyPassword() != null) {
			factory.setKeyManagerPassword(ssl.getKeyPassword());
		}
	}

	private void configureSslKeyStore(SslContextFactory factory, Ssl ssl) {
		try {
			URL url = ResourceUtils.getURL(ssl.getKeyStore());
			factory.setKeyStoreResource(Resource.newResource(url));
		}
		catch (IOException ex) {
			throw new WebServerException(
					"Could not find key store '" + ssl.getKeyStore() + "'", ex);
		}
		if (ssl.getKeyStoreType() != null) {
			factory.setKeyStoreType(ssl.getKeyStoreType());
		}
		if (ssl.getKeyStoreProvider() != null) {
			factory.setKeyStoreProvider(ssl.getKeyStoreProvider());
		}
	}

	private void configureSslTrustStore(SslContextFactory factory, Ssl ssl) {
		if (ssl.getTrustStorePassword() != null) {
			factory.setTrustStorePassword(ssl.getTrustStorePassword());
		}
		if (ssl.getTrustStore() != null) {
			try {
				URL url = ResourceUtils.getURL(ssl.getTrustStore());
				factory.setTrustStoreResource(Resource.newResource(url));
			}
			catch (IOException ex) {
				throw new WebServerException(
						"Could not find trust store '" + ssl.getTrustStore() + "'", ex);
			}
		}
		if (ssl.getTrustStoreType() != null) {
			factory.setTrustStoreType(ssl.getTrustStoreType());
		}
		if (ssl.getTrustStoreProvider() != null) {
			factory.setTrustStoreProvider(ssl.getTrustStoreProvider());
		}
	}

	/**
	 * Configure the given Jetty {@link WebAppContext} for use.
	 * @param context the context to configure
	 * @param initializers the set of initializers to apply
	 */
	protected final void configureWebAppContext(WebAppContext context,
			ServletContextInitializer... initializers) {
		Assert.notNull(context, "Context must not be null");
		context.setTempDirectory(getTempDirectory());
		if (this.resourceLoader != null) {
			context.setClassLoader(this.resourceLoader.getClassLoader());
		}
		String contextPath = getContextPath();
		context.setContextPath(StringUtils.hasLength(contextPath) ? contextPath : "/");
		context.setDisplayName(getDisplayName());
		configureDocumentRoot(context);
		if (isRegisterDefaultServlet()) {
			addDefaultServlet(context);
		}
		if (shouldRegisterJspServlet()) {
			addJspServlet(context);
			context.addBean(new JasperInitializer(context), true);
		}
		addLocaleMappings(context);
		ServletContextInitializer[] initializersToUse = mergeInitializers(initializers);
		Configuration[] configurations = getWebAppContextConfigurations(context,
				initializersToUse);
		context.setConfigurations(configurations);
		configureSession(context);
		postProcessWebAppContext(context);
	}

	private void configureSession(WebAppContext context) {
		SessionHandler handler = context.getSessionHandler();
		handler.setMaxInactiveInterval(
				getSessionTimeout() > 0 ? getSessionTimeout() : -1);
		if (isPersistSession()) {
			DefaultSessionCache cache = new DefaultSessionCache(handler);
			FileSessionDataStore store = new FileSessionDataStore();
			store.setStoreDir(getValidSessionStoreDir());
			cache.setSessionDataStore(store);
			handler.setSessionCache(cache);
		}
	}

	private void addLocaleMappings(WebAppContext context) {
		for (Map.Entry<Locale, Charset> entry : getLocaleCharsetMappings().entrySet()) {
			Locale locale = entry.getKey();
			Charset charset = entry.getValue();
			context.addLocaleEncoding(locale.toString(), charset.toString());
		}
	}

	private File getTempDirectory() {
		String temp = System.getProperty("java.io.tmpdir");
		return (temp == null ? null : new File(temp));
	}

	private void configureDocumentRoot(WebAppContext handler) {
		File root = getValidDocumentRoot();
		root = (root != null ? root : createTempDir("jetty-docbase"));
		try {
			List<Resource> resources = new ArrayList<>();
			resources.add(
					root.isDirectory() ? Resource.newResource(root.getCanonicalFile())
							: JarResource.newJarResource(Resource.newResource(root)));
			for (URL resourceJarUrl : this.getUrlsOfJarsWithMetaInfResources()) {
				Resource resource = createResource(resourceJarUrl);
				// Jetty 9.2 and earlier do not support nested jars. See
				// https://github.com/eclipse/jetty.project/issues/518
				if (resource.exists() && resource.isDirectory()) {
					resources.add(resource);
				}
			}
			handler.setBaseResource(new ResourceCollection(
					resources.toArray(new Resource[resources.size()])));
		}
		catch (Exception ex) {
			throw new IllegalStateException(ex);
		}
	}

	private Resource createResource(URL url) throws IOException {
		if ("file".equals(url.getProtocol())) {
			File file = new File(url.getFile());
			if (file.isFile()) {
				return Resource.newResource("jar:" + url + "!/META-INF/resources");
			}
		}
		return Resource.newResource(url + "META-INF/resources");
	}

	/**
	 * Add Jetty's {@code DefaultServlet} to the given {@link WebAppContext}.
	 * @param context the jetty {@link WebAppContext}
	 */
	protected final void addDefaultServlet(WebAppContext context) {
		Assert.notNull(context, "Context must not be null");
		ServletHolder holder = new ServletHolder();
		holder.setName("default");
		holder.setClassName("org.eclipse.jetty.servlet.DefaultServlet");
		holder.setInitParameter("dirAllowed", "false");
		holder.setInitOrder(1);
		context.getServletHandler().addServletWithMapping(holder, "/");
		context.getServletHandler().getServletMapping("/").setDefault(true);
	}

	/**
	 * Add Jetty's {@code JspServlet} to the given {@link WebAppContext}.
	 * @param context the jetty {@link WebAppContext}
	 */
	protected final void addJspServlet(WebAppContext context) {
		Assert.notNull(context, "Context must not be null");
		ServletHolder holder = new ServletHolder();
		holder.setName("jsp");
		holder.setClassName(getJsp().getClassName());
		holder.setInitParameter("fork", "false");
		holder.setInitParameters(getJsp().getInitParameters());
		holder.setInitOrder(3);
		context.getServletHandler().addServlet(holder);
		ServletMapping mapping = new ServletMapping();
		mapping.setServletName("jsp");
		mapping.setPathSpecs(new String[] { "*.jsp", "*.jspx" });
		context.getServletHandler().addServletMapping(mapping);
	}

	/**
	 * Return the Jetty {@link Configuration}s that should be applied to the server.
	 * @param webAppContext the Jetty {@link WebAppContext}
	 * @param initializers the {@link ServletContextInitializer}s to apply
	 * @return configurations to apply
	 */
	protected Configuration[] getWebAppContextConfigurations(WebAppContext webAppContext,
			ServletContextInitializer... initializers) {
		List<Configuration> configurations = new ArrayList<>();
		configurations.add(
				getServletContextInitializerConfiguration(webAppContext, initializers));
		configurations.addAll(getConfigurations());
		configurations.add(getErrorPageConfiguration());
		configurations.add(getMimeTypeConfiguration());
		return configurations.toArray(new Configuration[configurations.size()]);
	}

	/**
	 * Create a configuration object that adds error handlers.
	 * @return a configuration object for adding error pages
	 */
	private Configuration getErrorPageConfiguration() {
		return new AbstractConfiguration() {

			@Override
			public void configure(WebAppContext context) throws Exception {
				ErrorHandler errorHandler = context.getErrorHandler();
				context.setErrorHandler(new JettyEmbeddedErrorHandler(errorHandler));
				addJettyErrorPages(errorHandler, getErrorPages());
			}

		};
	}

	/**
	 * Create a configuration object that adds mime type mappings.
	 * @return a configuration object for adding mime type mappings
	 */
	private Configuration getMimeTypeConfiguration() {
		return new AbstractConfiguration() {

			@Override
			public void configure(WebAppContext context) throws Exception {
				MimeTypes mimeTypes = context.getMimeTypes();
				for (MimeMappings.Mapping mapping : getMimeMappings()) {
					mimeTypes.addMimeMapping(mapping.getExtension(),
							mapping.getMimeType());
				}
			}

		};
	}

	/**
	 * Return a Jetty {@link Configuration} that will invoke the specified
	 * {@link ServletContextInitializer}s. By default this method will return a
	 * {@link ServletContextInitializerConfiguration}.
	 * @param webAppContext the Jetty {@link WebAppContext}
	 * @param initializers the {@link ServletContextInitializer}s to apply
	 * @return the {@link Configuration} instance
	 */
	protected Configuration getServletContextInitializerConfiguration(
			WebAppContext webAppContext, ServletContextInitializer... initializers) {
		return new ServletContextInitializerConfiguration(initializers);
	}

	/**
	 * Post process the Jetty {@link WebAppContext} before it used with the Jetty Server.
	 * Subclasses can override this method to apply additional processing to the
	 * {@link WebAppContext}.
	 * @param webAppContext the Jetty {@link WebAppContext}
	 */
	protected void postProcessWebAppContext(WebAppContext webAppContext) {
	}

	/**
	 * Factory method called to create the {@link JettyWebServer} . Subclasses can
	 * override this method to return a different {@link JettyWebServer} or apply
	 * additional processing to the Jetty server.
	 * @param server the Jetty server.
	 * @return a new {@link JettyWebServer} instance
	 */
	protected JettyWebServer getJettyWebServer(Server server) {
		return new JettyWebServer(server, getPort() >= 0);
	}

	@Override
	public void setResourceLoader(ResourceLoader resourceLoader) {
		this.resourceLoader = resourceLoader;
	}

	/**
	 * Set if x-forward-* headers should be processed.
	 * @param useForwardHeaders if x-forward headers should be used
	 * @since 1.3.0
	 */
	public void setUseForwardHeaders(boolean useForwardHeaders) {
		this.useForwardHeaders = useForwardHeaders;
	}

	/**
	 * Set the number of acceptor threads to use.
	 * @param acceptors the number of acceptor threads to use
	 * @since 1.4.0
	 */
	public void setAcceptors(int acceptors) {
		this.acceptors = acceptors;
	}

	/**
	 * Set the number of selector threads to use.
	 * @param selectors the number of selector threads to use
	 * @since 1.4.0
	 */
	public void setSelectors(int selectors) {
		this.selectors = selectors;
	}

	/**
	 * Sets {@link JettyServerCustomizer}s that will be applied to the {@link Server}
	 * before it is started. Calling this method will replace any existing configurations.
	 * @param customizers the Jetty customizers to apply
	 */
	public void setServerCustomizers(
			Collection<? extends JettyServerCustomizer> customizers) {
		Assert.notNull(customizers, "Customizers must not be null");
		this.jettyServerCustomizers = new ArrayList<>(customizers);
	}

	/**
	 * Returns a mutable collection of Jetty {@link Configuration}s that will be applied
	 * to the {@link WebAppContext} before the server is created.
	 * @return the Jetty {@link Configuration}s
	 */
	public Collection<JettyServerCustomizer> getServerCustomizers() {
		return this.jettyServerCustomizers;
	}

	/**
	 * Add {@link JettyServerCustomizer}s that will be applied to the {@link Server}
	 * before it is started.
	 * @param customizers the customizers to add
	 */
	public void addServerCustomizers(JettyServerCustomizer... customizers) {
		Assert.notNull(customizers, "Customizers must not be null");
		this.jettyServerCustomizers.addAll(Arrays.asList(customizers));
	}

	/**
	 * Sets Jetty {@link Configuration}s that will be applied to the {@link WebAppContext}
	 * before the server is created. Calling this method will replace any existing
	 * configurations.
	 * @param configurations the Jetty configurations to apply
	 */
	public void setConfigurations(Collection<? extends Configuration> configurations) {
		Assert.notNull(configurations, "Configurations must not be null");
		this.configurations = new ArrayList<>(configurations);
	}

	/**
	 * Returns a mutable collection of Jetty {@link Configuration}s that will be applied
	 * to the {@link WebAppContext} before the server is created.
	 * @return the Jetty {@link Configuration}s
	 */
	public Collection<Configuration> getConfigurations() {
		return this.configurations;
	}

	/**
	 * Add {@link Configuration}s that will be applied to the {@link WebAppContext} before
	 * the server is started.
	 * @param configurations the configurations to add
	 */
	public void addConfigurations(Configuration... configurations) {
		Assert.notNull(configurations, "Configurations must not be null");
		this.configurations.addAll(Arrays.asList(configurations));
	}

	/**
	 * Returns a Jetty {@link ThreadPool} that should be used by the {@link Server}.
	 * @return a Jetty {@link ThreadPool} or {@code null}
	 */
	public ThreadPool getThreadPool() {
		return this.threadPool;
	}

	/**
	 * Set a Jetty {@link ThreadPool} that should be used by the {@link Server}. If set to
	 * {@code null} (default), the {@link Server} creates a {@link ThreadPool} implicitly.
	 * @param threadPool a Jetty ThreadPool to be used
	 */
	public void setThreadPool(ThreadPool threadPool) {
		this.threadPool = threadPool;
	}

	private void addJettyErrorPages(ErrorHandler errorHandler,
			Collection<ErrorPage> errorPages) {
		if (errorHandler instanceof ErrorPageErrorHandler) {
			ErrorPageErrorHandler handler = (ErrorPageErrorHandler) errorHandler;
			for (ErrorPage errorPage : errorPages) {
				if (errorPage.isGlobal()) {
					handler.addErrorPage(ErrorPageErrorHandler.GLOBAL_ERROR_PAGE,
							errorPage.getPath());
				}
				else {
					if (errorPage.getExceptionName() != null) {
						handler.addErrorPage(errorPage.getExceptionName(),
								errorPage.getPath());
					}
					else {
						handler.addErrorPage(errorPage.getStatusCode(),
								errorPage.getPath());
					}
				}
			}
		}
	}

	/**
	 * {@link JettyServerCustomizer} to add {@link ForwardedRequestCustomizer}. Only
	 * supported with Jetty 9 (hence the inner class)
	 */
	private static class ForwardHeadersCustomizer implements JettyServerCustomizer {

		@Override
		public void customize(Server server) {
			ForwardedRequestCustomizer customizer = new ForwardedRequestCustomizer();
			for (Connector connector : server.getConnectors()) {
				for (ConnectionFactory connectionFactory : connector
						.getConnectionFactories()) {
					if (connectionFactory instanceof HttpConfiguration.ConnectionFactory) {
						((HttpConfiguration.ConnectionFactory) connectionFactory)
								.getHttpConfiguration().addCustomizer(customizer);
					}
				}
			}
		}

	}

	/**
	 * {@link HandlerWrapper} to add a custom {@code server} header.
	 */
	private static class ServerHeaderHandler extends HandlerWrapper {

		private static final String SERVER_HEADER = "server";

		private final String value;

		ServerHeaderHandler(String value) {
			this.value = value;
		}

		@Override
		public void handle(String target, Request baseRequest, HttpServletRequest request,
				HttpServletResponse response) throws IOException, ServletException {
			if (!response.getHeaderNames().contains(SERVER_HEADER)) {
				response.setHeader(SERVER_HEADER, this.value);
			}
			super.handle(target, baseRequest, request, response);
		}

	}

}
