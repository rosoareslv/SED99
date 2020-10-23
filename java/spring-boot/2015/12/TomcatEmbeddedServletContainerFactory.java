/*
 * Copyright 2012-2015 the original author or authors.
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

package org.springframework.boot.context.embedded.tomcat;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map.Entry;
import java.util.Set;
import java.util.concurrent.TimeUnit;

import javax.servlet.ServletContainerInitializer;
import javax.servlet.ServletContext;

import org.apache.catalina.Context;
import org.apache.catalina.Host;
import org.apache.catalina.Lifecycle;
import org.apache.catalina.LifecycleEvent;
import org.apache.catalina.LifecycleListener;
import org.apache.catalina.Manager;
import org.apache.catalina.Valve;
import org.apache.catalina.Wrapper;
import org.apache.catalina.connector.Connector;
import org.apache.catalina.loader.WebappLoader;
import org.apache.catalina.session.StandardManager;
import org.apache.catalina.startup.Tomcat;
import org.apache.catalina.startup.Tomcat.FixContextListener;
import org.apache.coyote.AbstractProtocol;
import org.apache.coyote.ProtocolHandler;
import org.apache.coyote.http11.AbstractHttp11JsseProtocol;
import org.apache.coyote.http11.AbstractHttp11Protocol;

import org.springframework.beans.BeanUtils;
import org.springframework.boot.context.embedded.AbstractEmbeddedServletContainerFactory;
import org.springframework.boot.context.embedded.Compression;
import org.springframework.boot.context.embedded.EmbeddedServletContainer;
import org.springframework.boot.context.embedded.EmbeddedServletContainerException;
import org.springframework.boot.context.embedded.EmbeddedServletContainerFactory;
import org.springframework.boot.context.embedded.ErrorPage;
import org.springframework.boot.context.embedded.MimeMappings;
import org.springframework.boot.context.embedded.ServletContextInitializer;
import org.springframework.boot.context.embedded.Ssl;
import org.springframework.boot.context.embedded.Ssl.ClientAuth;
import org.springframework.context.ResourceLoaderAware;
import org.springframework.core.io.ResourceLoader;
import org.springframework.util.Assert;
import org.springframework.util.ClassUtils;
import org.springframework.util.ReflectionUtils;
import org.springframework.util.ResourceUtils;
import org.springframework.util.StreamUtils;
import org.springframework.util.StringUtils;

/**
 * {@link EmbeddedServletContainerFactory} that can be used to create
 * {@link TomcatEmbeddedServletContainer}s. Can be initialized using Spring's
 * {@link ServletContextInitializer}s or Tomcat {@link LifecycleListener}s.
 * <p>
 * Unless explicitly configured otherwise this factory will created containers that
 * listens for HTTP requests on port 8080.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Brock Mills
 * @author Stephane Nicoll
 * @author Andy Wilkinson
 * @author Eddú Meléndez
 * @see #setPort(int)
 * @see #setContextLifecycleListeners(Collection)
 * @see TomcatEmbeddedServletContainer
 */
public class TomcatEmbeddedServletContainerFactory
		extends AbstractEmbeddedServletContainerFactory implements ResourceLoaderAware {

	private static final Charset DEFAULT_CHARSET = Charset.forName("UTF-8");

	private static final Set<Class<?>> NO_CLASSES = Collections.emptySet();

	/**
	 * The class name of default protocol used.
	 */
	public static final String DEFAULT_PROTOCOL = "org.apache.coyote.http11.Http11NioProtocol";

	private File baseDirectory;

	private List<Valve> contextValves = new ArrayList<Valve>();

	private List<LifecycleListener> contextLifecycleListeners = new ArrayList<LifecycleListener>();

	private List<TomcatContextCustomizer> tomcatContextCustomizers = new ArrayList<TomcatContextCustomizer>();

	private List<TomcatConnectorCustomizer> tomcatConnectorCustomizers = new ArrayList<TomcatConnectorCustomizer>();

	private List<Connector> additionalTomcatConnectors = new ArrayList<Connector>();

	private ResourceLoader resourceLoader;

	private String protocol = DEFAULT_PROTOCOL;

	private String tldSkip;

	private Charset uriEncoding = DEFAULT_CHARSET;

	/**
	 * Create a new {@link TomcatEmbeddedServletContainerFactory} instance.
	 */
	public TomcatEmbeddedServletContainerFactory() {
		super();
	}

	/**
	 * Create a new {@link TomcatEmbeddedServletContainerFactory} that listens for
	 * requests using the specified port.
	 * @param port the port to listen on
	 */
	public TomcatEmbeddedServletContainerFactory(int port) {
		super(port);
	}

	/**
	 * Create a new {@link TomcatEmbeddedServletContainerFactory} with the specified
	 * context path and port.
	 * @param contextPath root the context path
	 * @param port the port to listen on
	 */
	public TomcatEmbeddedServletContainerFactory(String contextPath, int port) {
		super(contextPath, port);
	}

	@Override
	public EmbeddedServletContainer getEmbeddedServletContainer(
			ServletContextInitializer... initializers) {
		Tomcat tomcat = new Tomcat();
		File baseDir = (this.baseDirectory != null ? this.baseDirectory
				: createTempDir("tomcat"));
		tomcat.setBaseDir(baseDir.getAbsolutePath());
		Connector connector = new Connector(this.protocol);
		tomcat.getService().addConnector(connector);
		customizeConnector(connector);
		tomcat.setConnector(connector);
		tomcat.getHost().setAutoDeploy(false);
		tomcat.getEngine().setBackgroundProcessorDelay(-1);
		for (Connector additionalConnector : this.additionalTomcatConnectors) {
			tomcat.getService().addConnector(additionalConnector);
		}
		prepareContext(tomcat.getHost(), initializers);
		return getTomcatEmbeddedServletContainer(tomcat);
	}

	protected void prepareContext(Host host, ServletContextInitializer[] initializers) {
		File docBase = getValidDocumentRoot();
		docBase = (docBase != null ? docBase : createTempDir("tomcat-docbase"));
		TomcatEmbeddedContext context = new TomcatEmbeddedContext();
		context.setName(getContextPath());
		context.setDisplayName(getDisplayName());
		context.setPath(getContextPath());
		context.setDocBase(docBase.getAbsolutePath());
		context.addLifecycleListener(new FixContextListener());
		context.setParentClassLoader(
				this.resourceLoader != null ? this.resourceLoader.getClassLoader()
						: ClassUtils.getDefaultClassLoader());
		try {
			context.setUseRelativeRedirects(false);
		}
		catch (NoSuchMethodError ex) {
			// Tomcat is < 8.0.30. Continue
		}
		SkipPatternJarScanner.apply(context, this.tldSkip);
		WebappLoader loader = new WebappLoader(context.getParentClassLoader());
		loader.setLoaderClass(TomcatEmbeddedWebappClassLoader.class.getName());
		loader.setDelegate(true);
		context.setLoader(loader);
		if (isRegisterDefaultServlet()) {
			addDefaultServlet(context);
		}
		if (shouldRegisterJspServlet()) {
			addJspServlet(context);
			addJasperInitializer(context);
			context.addLifecycleListener(new StoreMergedWebXmlListener());
		}
		ServletContextInitializer[] initializersToUse = mergeInitializers(initializers);
		configureContext(context, initializersToUse);
		host.addChild(context);
		postProcessContext(context);
	}

	private void addDefaultServlet(Context context) {
		Wrapper defaultServlet = context.createWrapper();
		defaultServlet.setName("default");
		defaultServlet.setServletClass("org.apache.catalina.servlets.DefaultServlet");
		defaultServlet.addInitParameter("debug", "0");
		defaultServlet.addInitParameter("listings", "false");
		defaultServlet.setLoadOnStartup(1);
		// Otherwise the default location of a Spring DispatcherServlet cannot be set
		defaultServlet.setOverridable(true);
		context.addChild(defaultServlet);
		context.addServletMapping("/", "default");
	}

	private void addJspServlet(Context context) {
		Wrapper jspServlet = context.createWrapper();
		jspServlet.setName("jsp");
		jspServlet.setServletClass(getJspServlet().getClassName());
		jspServlet.addInitParameter("fork", "false");
		for (Entry<String, String> initParameter : getJspServlet().getInitParameters()
				.entrySet()) {
			jspServlet.addInitParameter(initParameter.getKey(), initParameter.getValue());
		}
		jspServlet.setLoadOnStartup(3);
		context.addChild(jspServlet);
		context.addServletMapping("*.jsp", "jsp");
		context.addServletMapping("*.jspx", "jsp");
	}

	private void addJasperInitializer(TomcatEmbeddedContext context) {
		try {
			ServletContainerInitializer initializer = (ServletContainerInitializer) ClassUtils
					.forName("org.apache.jasper.servlet.JasperInitializer", null)
					.newInstance();
			context.addServletContainerInitializer(initializer, null);
		}
		catch (Exception ex) {
			// Probably not Tomcat 8
		}
	}

	// Needs to be protected so it can be used by subclasses
	protected void customizeConnector(Connector connector) {
		int port = (getPort() >= 0 ? getPort() : 0);
		connector.setPort(port);
		if (StringUtils.hasText(this.getServerHeader())) {
			connector.setAttribute("server", this.getServerHeader());
		}
		if (connector.getProtocolHandler() instanceof AbstractProtocol) {
			customizeProtocol((AbstractProtocol<?>) connector.getProtocolHandler());
		}
		if (getUriEncoding() != null) {
			connector.setURIEncoding(getUriEncoding().name());
		}

		// If ApplicationContext is slow to start we want Tomcat not to bind to the socket
		// prematurely...
		connector.setProperty("bindOnInit", "false");

		if (getSsl() != null && getSsl().isEnabled()) {
			customizeSsl(connector);
		}
		if (getCompression() != null && getCompression().getEnabled()) {
			customizeCompression(connector);
		}
		for (TomcatConnectorCustomizer customizer : this.tomcatConnectorCustomizers) {
			customizer.customize(connector);
		}
	}

	private void customizeProtocol(AbstractProtocol<?> protocol) {
		if (getAddress() != null) {
			protocol.setAddress(getAddress());
		}
	}

	private void customizeSsl(Connector connector) {
		ProtocolHandler handler = connector.getProtocolHandler();
		Assert.state(handler instanceof AbstractHttp11JsseProtocol,
				"To use SSL, the connector's protocol handler must be an "
						+ "AbstractHttp11JsseProtocol subclass");
		configureSsl((AbstractHttp11JsseProtocol<?>) handler, getSsl());
		connector.setScheme("https");
		connector.setSecure(true);
	}

	private void customizeCompression(Connector connector) {
		ProtocolHandler handler = connector.getProtocolHandler();
		if (handler instanceof AbstractHttp11Protocol) {
			AbstractHttp11Protocol<?> protocol = (AbstractHttp11Protocol<?>) handler;
			Compression compression = getCompression();
			protocol.setCompression("on");
			protocol.setCompressionMinSize(compression.getMinResponseSize());
			protocol.setCompressableMimeTypes(
					StringUtils.arrayToCommaDelimitedString(compression.getMimeTypes()));
			if (getCompression().getExcludedUserAgents() != null) {
				protocol.setNoCompressionUserAgents(
						StringUtils.arrayToCommaDelimitedString(
								getCompression().getExcludedUserAgents()));
			}
		}
	}

	/**
	 * Configure Tomcat's {@link AbstractHttp11JsseProtocol} for SSL.
	 * @param protocol the protocol
	 * @param ssl the ssl details
	 */
	protected void configureSsl(AbstractHttp11JsseProtocol<?> protocol, Ssl ssl) {
		protocol.setSSLEnabled(true);
		protocol.setSslProtocol(ssl.getProtocol());
		configureSslClientAuth(protocol, ssl);
		protocol.setKeystorePass(ssl.getKeyStorePassword());
		protocol.setKeyPass(ssl.getKeyPassword());
		protocol.setKeyAlias(ssl.getKeyAlias());
		configureSslKeyStore(protocol, ssl);
		String ciphers = StringUtils.arrayToCommaDelimitedString(ssl.getCiphers());
		protocol.setCiphers(ciphers);
		configureSslTrustStore(protocol, ssl);
	}

	private void configureSslClientAuth(AbstractHttp11JsseProtocol<?> protocol, Ssl ssl) {
		if (ssl.getClientAuth() == ClientAuth.NEED) {
			protocol.setClientAuth(Boolean.TRUE.toString());
		}
		else if (ssl.getClientAuth() == ClientAuth.WANT) {
			protocol.setClientAuth("want");
		}
	}

	private void configureSslKeyStore(AbstractHttp11JsseProtocol<?> protocol, Ssl ssl) {
		try {
			protocol.setKeystoreFile(ResourceUtils.getURL(ssl.getKeyStore()).toString());
		}
		catch (FileNotFoundException ex) {
			throw new EmbeddedServletContainerException(
					"Could not load key store: " + ex.getMessage(), ex);
		}
		if (ssl.getKeyStoreType() != null) {
			protocol.setKeystoreType(ssl.getKeyStoreType());
		}
		if (ssl.getKeyStoreProvider() != null) {
			protocol.setKeystoreProvider(ssl.getKeyStoreProvider());
		}
	}

	private void configureSslTrustStore(AbstractHttp11JsseProtocol<?> protocol, Ssl ssl) {
		if (ssl.getTrustStore() != null) {
			try {
				protocol.setTruststoreFile(
						ResourceUtils.getURL(ssl.getTrustStore()).toString());
			}
			catch (FileNotFoundException ex) {
				throw new EmbeddedServletContainerException(
						"Could not load trust store: " + ex.getMessage(), ex);
			}
		}
		protocol.setTruststorePass(ssl.getTrustStorePassword());
		if (ssl.getTrustStoreType() != null) {
			protocol.setTruststoreType(ssl.getTrustStoreType());
		}
		if (ssl.getTrustStoreProvider() != null) {
			protocol.setTruststoreProvider(ssl.getTrustStoreProvider());
		}
	}

	/**
	 * Configure the Tomcat {@link Context}.
	 * @param context the Tomcat context
	 * @param initializers initializers to apply
	 */
	protected void configureContext(Context context,
			ServletContextInitializer[] initializers) {
		TomcatStarter starter = new TomcatStarter(initializers);
		if (context instanceof TomcatEmbeddedContext) {
			// Should be true
			((TomcatEmbeddedContext) context).setStarter(starter);
		}
		context.addServletContainerInitializer(starter, NO_CLASSES);
		for (LifecycleListener lifecycleListener : this.contextLifecycleListeners) {
			context.addLifecycleListener(lifecycleListener);
		}
		for (Valve valve : this.contextValves) {
			context.getPipeline().addValve(valve);
		}
		for (ErrorPage errorPage : getErrorPages()) {
			new TomcatErrorPage(errorPage).addToContext(context);
		}
		for (MimeMappings.Mapping mapping : getMimeMappings()) {
			context.addMimeMapping(mapping.getExtension(), mapping.getMimeType());
		}
		configureSession(context);
		for (TomcatContextCustomizer customizer : this.tomcatContextCustomizers) {
			customizer.customize(context);
		}
	}

	private void configureSession(Context context) {
		long sessionTimeout = getSessionTimeoutInMinutes();
		context.setSessionTimeout((int) sessionTimeout);
		Manager manager = context.getManager();
		if (manager == null) {
			manager = new StandardManager();
			context.setManager(manager);
		}
		if (isPersistSession()) {
			configurePersistSession(manager);
		}
		else {
			disablePersistSession(manager);
		}
	}

	private void configurePersistSession(Manager manager) {
		Assert.state(manager instanceof StandardManager,
				"Unable to persist HTTP session state using manager type "
						+ manager.getClass().getName());
		File dir = getValidSessionStoreDir();
		File file = new File(dir, "SESSIONS.ser");
		((StandardManager) manager).setPathname(file.getAbsolutePath());
	}

	private void disablePersistSession(Manager manager) {
		if (manager instanceof StandardManager) {
			((StandardManager) manager).setPathname(null);
		}
	}

	private long getSessionTimeoutInMinutes() {
		long sessionTimeout = getSessionTimeout();
		if (sessionTimeout > 0) {
			sessionTimeout = Math.max(TimeUnit.SECONDS.toMinutes(sessionTimeout), 1L);
		}
		return sessionTimeout;
	}

	/**
	 * Post process the Tomcat {@link Context} before it used with the Tomcat Server.
	 * Subclasses can override this method to apply additional processing to the
	 * {@link Context}.
	 * @param context the Tomcat {@link Context}
	 */
	protected void postProcessContext(Context context) {
	}

	/**
	 * Factory method called to create the {@link TomcatEmbeddedServletContainer}.
	 * Subclasses can override this method to return a different
	 * {@link TomcatEmbeddedServletContainer} or apply additional processing to the Tomcat
	 * server.
	 * @param tomcat the Tomcat server.
	 * @return a new {@link TomcatEmbeddedServletContainer} instance
	 */
	protected TomcatEmbeddedServletContainer getTomcatEmbeddedServletContainer(
			Tomcat tomcat) {
		return new TomcatEmbeddedServletContainer(tomcat, getPort() >= 0);
	}

	@Override
	public void setResourceLoader(ResourceLoader resourceLoader) {
		this.resourceLoader = resourceLoader;
	}

	/**
	 * Set the Tomcat base directory. If not specified a temporary directory will be used.
	 * @param baseDirectory the tomcat base directory
	 */
	public void setBaseDirectory(File baseDirectory) {
		this.baseDirectory = baseDirectory;
	}

	/**
	 * A comma-separated list of jars to ignore for TLD scanning. See Tomcat's
	 * catalina.properties for typical values. Defaults to a list drawn from that source.
	 * @param tldSkip the jars to skip when scanning for TLDs etc
	 */
	public void setTldSkip(String tldSkip) {
		Assert.notNull(tldSkip, "TldSkip must not be null");
		this.tldSkip = tldSkip;
	}

	/**
	 * The Tomcat protocol to use when create the {@link Connector}.
	 * @param protocol the protocol
	 * @see Connector#Connector(String)
	 */
	public void setProtocol(String protocol) {
		Assert.hasLength(protocol, "Protocol must not be empty");
		this.protocol = protocol;
	}

	/**
	 * Set {@link Valve}s that should be applied to the Tomcat {@link Context}. Calling
	 * this method will replace any existing listeners.
	 * @param contextValves the valves to set
	 */
	public void setContextValves(Collection<? extends Valve> contextValves) {
		Assert.notNull(contextValves, "Valves must not be null");
		this.contextValves = new ArrayList<Valve>(contextValves);
	}

	/**
	 * Returns a mutable collection of the {@link Valve}s that will be applied to the
	 * Tomcat {@link Context}.
	 * @return the contextValves the valves that will be applied
	 */
	public Collection<Valve> getValves() {
		return this.contextValves;
	}

	/**
	 * Add {@link Valve}s that should be applied to the Tomcat {@link Context}.
	 * @param contextValves the valves to add
	 */
	public void addContextValves(Valve... contextValves) {
		Assert.notNull(contextValves, "Valves must not be null");
		this.contextValves.addAll(Arrays.asList(contextValves));
	}

	/**
	 * Set {@link LifecycleListener}s that should be applied to the Tomcat {@link Context}
	 * . Calling this method will replace any existing listeners.
	 * @param contextLifecycleListeners the listeners to set
	 */
	public void setContextLifecycleListeners(
			Collection<? extends LifecycleListener> contextLifecycleListeners) {
		Assert.notNull(contextLifecycleListeners,
				"ContextLifecycleListeners must not be null");
		this.contextLifecycleListeners = new ArrayList<LifecycleListener>(
				contextLifecycleListeners);
	}

	/**
	 * Returns a mutable collection of the {@link LifecycleListener}s that will be applied
	 * to the Tomcat {@link Context} .
	 * @return the contextLifecycleListeners the listeners that will be applied
	 */
	public Collection<LifecycleListener> getContextLifecycleListeners() {
		return this.contextLifecycleListeners;
	}

	/**
	 * Add {@link LifecycleListener}s that should be added to the Tomcat {@link Context}.
	 * @param contextLifecycleListeners the listeners to add
	 */
	public void addContextLifecycleListeners(
			LifecycleListener... contextLifecycleListeners) {
		Assert.notNull(contextLifecycleListeners,
				"ContextLifecycleListeners must not be null");
		this.contextLifecycleListeners.addAll(Arrays.asList(contextLifecycleListeners));
	}

	/**
	 * Set {@link TomcatContextCustomizer}s that should be applied to the Tomcat
	 * {@link Context} . Calling this method will replace any existing customizers.
	 * @param tomcatContextCustomizers the customizers to set
	 */
	public void setTomcatContextCustomizers(
			Collection<? extends TomcatContextCustomizer> tomcatContextCustomizers) {
		Assert.notNull(tomcatContextCustomizers,
				"TomcatContextCustomizers must not be null");
		this.tomcatContextCustomizers = new ArrayList<TomcatContextCustomizer>(
				tomcatContextCustomizers);
	}

	/**
	 * Returns a mutable collection of the {@link TomcatContextCustomizer}s that will be
	 * applied to the Tomcat {@link Context} .
	 * @return the listeners that will be applied
	 */
	public Collection<TomcatContextCustomizer> getTomcatContextCustomizers() {
		return this.tomcatContextCustomizers;
	}

	/**
	 * Add {@link TomcatContextCustomizer}s that should be added to the Tomcat
	 * {@link Context}.
	 * @param tomcatContextCustomizers the customizers to add
	 */
	public void addContextCustomizers(
			TomcatContextCustomizer... tomcatContextCustomizers) {
		Assert.notNull(tomcatContextCustomizers,
				"TomcatContextCustomizers must not be null");
		this.tomcatContextCustomizers.addAll(Arrays.asList(tomcatContextCustomizers));
	}

	/**
	 * Set {@link TomcatConnectorCustomizer}s that should be applied to the Tomcat
	 * {@link Connector} . Calling this method will replace any existing customizers.
	 * @param tomcatConnectorCustomizers the customizers to set
	 */
	public void setTomcatConnectorCustomizers(
			Collection<? extends TomcatConnectorCustomizer> tomcatConnectorCustomizers) {
		Assert.notNull(tomcatConnectorCustomizers,
				"TomcatConnectorCustomizers must not be null");
		this.tomcatConnectorCustomizers = new ArrayList<TomcatConnectorCustomizer>(
				tomcatConnectorCustomizers);
	}

	/**
	 * Add {@link TomcatContextCustomizer}s that should be added to the Tomcat
	 * {@link Connector}.
	 * @param tomcatConnectorCustomizers the customizers to add
	 */
	public void addConnectorCustomizers(
			TomcatConnectorCustomizer... tomcatConnectorCustomizers) {
		Assert.notNull(tomcatConnectorCustomizers,
				"TomcatConnectorCustomizers must not be null");
		this.tomcatConnectorCustomizers.addAll(Arrays.asList(tomcatConnectorCustomizers));
	}

	/**
	 * Returns a mutable collection of the {@link TomcatConnectorCustomizer}s that will be
	 * applied to the Tomcat {@link Context} .
	 * @return the listeners that will be applied
	 */
	public Collection<TomcatConnectorCustomizer> getTomcatConnectorCustomizers() {
		return this.tomcatConnectorCustomizers;
	}

	/**
	 * Add {@link Connector}s in addition to the default connector, e.g. for SSL or AJP
	 * @param connectors the connectors to add
	 */
	public void addAdditionalTomcatConnectors(Connector... connectors) {
		Assert.notNull(connectors, "Connectors must not be null");
		this.additionalTomcatConnectors.addAll(Arrays.asList(connectors));
	}

	/**
	 * Returns a mutable collection of the {@link Connector}s that will be added to the
	 * Tomcat.
	 * @return the additionalTomcatConnectors
	 */
	public List<Connector> getAdditionalTomcatConnectors() {
		return this.additionalTomcatConnectors;
	}

	/**
	 * Set the character encoding to use for URL decoding. If not specified 'UTF-8' will
	 * be used.
	 * @param uriEncoding the uri encoding to set
	 */
	public void setUriEncoding(Charset uriEncoding) {
		this.uriEncoding = uriEncoding;
	}

	/**
	 * Returns the character encoding to use for URL decoding.
	 * @return the URI encoding
	 */
	public Charset getUriEncoding() {
		return this.uriEncoding;
	}

	private static class TomcatErrorPage {

		private static final String ERROR_PAGE_TOMCAT7 = "org.apache.catalina.deploy.ErrorPage";

		private static final String ERROR_PAGE_TOMCAT = "org.apache.tomcat.util.descriptor.web.ErrorPage";

		private final String location;

		private final String exceptionType;

		private final int errorCode;

		private final Object nativePage;

		TomcatErrorPage(ErrorPage errorPage) {
			this.location = errorPage.getPath();
			this.exceptionType = errorPage.getExceptionName();
			this.errorCode = errorPage.getStatusCode();
			this.nativePage = createNativePage(errorPage);
		}

		private Object createNativePage(ErrorPage errorPage) {
			Object nativePage = null;
			try {
				if (ClassUtils.isPresent(ERROR_PAGE_TOMCAT, null)) {
					nativePage = BeanUtils.instantiate(ClassUtils
							.forName(ERROR_PAGE_TOMCAT, null));
				}
				else if (ClassUtils.isPresent(ERROR_PAGE_TOMCAT7, null)) {
					nativePage = BeanUtils.instantiate(ClassUtils
							.forName(ERROR_PAGE_TOMCAT7, null));
				}
			}
			catch (ClassNotFoundException ex) {
				// Swallow and continue
			}
			catch (LinkageError ex) {
				// Swallow and continue
			}
			return nativePage;
		}

		public void addToContext(Context context) {
			Assert.state(this.nativePage != null,
					"Neither Tomcat 7 nor 8 detected so no native error page exists");
			if (ClassUtils.isPresent(ERROR_PAGE_TOMCAT, null)) {
				org.apache.tomcat.util.descriptor.web.ErrorPage errorPage = (org.apache.tomcat.util.descriptor.web.ErrorPage) this.nativePage;
				errorPage.setLocation(this.location);
				errorPage.setErrorCode(this.errorCode);
				errorPage.setExceptionType(this.exceptionType);
				context.addErrorPage(errorPage);
			}
			else {
				callMethod(this.nativePage, "setLocation", this.location, String.class);
				callMethod(this.nativePage, "setErrorCode", this.errorCode, int.class);
				callMethod(this.nativePage, "setExceptionType", this.exceptionType,
						String.class);
				callMethod(context, "addErrorPage", this.nativePage,
						this.nativePage.getClass());
			}
		}

		private void callMethod(Object target, String name, Object value, Class<?> type) {
			Method method = ReflectionUtils.findMethod(target.getClass(), name, type);
			ReflectionUtils.invokeMethod(method, target, value);
		}

	}

	/**
	 * {@link LifecycleListener} that stores an empty merged web.xml. This is critical for
	 * Jasper to prevent warnings about missing web.xml files and to enable EL.
	 */
	private static class StoreMergedWebXmlListener implements LifecycleListener {

		private final String MERGED_WEB_XML = org.apache.tomcat.util.scan.Constants.MERGED_WEB_XML;

		@Override
		public void lifecycleEvent(LifecycleEvent event) {
			if (event.getType().equals(Lifecycle.CONFIGURE_START_EVENT)) {
				onStart((Context) event.getLifecycle());
			}
		}

		private void onStart(Context context) {
			ServletContext servletContext = context.getServletContext();
			if (servletContext.getAttribute(this.MERGED_WEB_XML) == null) {
				servletContext.setAttribute(this.MERGED_WEB_XML, getEmptyWebXml());
			}
			TomcatResources.get(context).addClasspathResources();
		}

		private String getEmptyWebXml() {
			InputStream stream = TomcatEmbeddedServletContainerFactory.class
					.getResourceAsStream("empty-web.xml");
			Assert.state(stream != null, "Unable to read empty web.xml");
			try {
				try {
					return StreamUtils.copyToString(stream, Charset.forName("UTF-8"));
				}
				finally {
					stream.close();
				}
			}
			catch (IOException ex) {
				throw new IllegalStateException(ex);
			}
		}

	}

}
