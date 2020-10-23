/*
 * Copyright 2012-2013 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.context.embedded;

import java.io.File;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;

import org.springframework.util.Assert;

/**
 * Abstract base class for {@link ConfigurableEmbeddedServletContainer} implementations.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @see AbstractEmbeddedServletContainerFactory
 */
public abstract class AbstractConfigurableEmbeddedServletContainer implements
		ConfigurableEmbeddedServletContainer {

	private static final int DEFAULT_SESSION_TIMEOUT = (int) TimeUnit.MINUTES
			.toSeconds(30);

	private String contextPath = "";

	private boolean registerDefaultServlet = true;

	private boolean registerJspServlet = true;

	private String jspServletClassName = "org.apache.jasper.servlet.JspServlet";

	private int port = 8080;

	private List<ServletContextInitializer> initializers = new ArrayList<ServletContextInitializer>();

	private File documentRoot;

	private Set<ErrorPage> errorPages = new LinkedHashSet<ErrorPage>();

	private MimeMappings mimeMappings = new MimeMappings(MimeMappings.DEFAULT);

	private InetAddress address;

	private int sessionTimeout = DEFAULT_SESSION_TIMEOUT;

	private Ssl ssl;

	/**
	 * Create a new {@link AbstractConfigurableEmbeddedServletContainer} instance.
	 */
	public AbstractConfigurableEmbeddedServletContainer() {
	}

	/**
	 * Create a new {@link AbstractConfigurableEmbeddedServletContainer} instance with the
	 * specified port.
	 * @param port the port number for the embedded servlet container
	 */
	public AbstractConfigurableEmbeddedServletContainer(int port) {
		this.port = port;
	}

	/**
	 * Create a new {@link AbstractConfigurableEmbeddedServletContainer} instance with the
	 * specified context path and port.
	 * @param contextPath the context path for the embedded servlet container
	 * @param port the port number for the embedded servlet container
	 */
	public AbstractConfigurableEmbeddedServletContainer(String contextPath, int port) {
		checkContextPath(contextPath);
		this.contextPath = contextPath;
		this.port = port;
	}

	@Override
	public void setContextPath(String contextPath) {
		checkContextPath(contextPath);
		this.contextPath = contextPath;
	}

	private void checkContextPath(String contextPath) {
		Assert.notNull(contextPath, "ContextPath must not be null");
		if (contextPath.length() > 0) {
			if ("/".equals(contextPath)) {
				throw new IllegalArgumentException(
						"Root ContextPath must be specified using an empty string");
			}
			if (!contextPath.startsWith("/") || contextPath.endsWith("/")) {
				throw new IllegalArgumentException(
						"ContextPath must start with '/ and not end with '/'");
			}
		}
	}

	/**
	 * Returns the context path for the embedded servlet container. The path will start
	 * with "/" and not end with "/". The root context is represented by an empty string.
	 * @return the context path
	 */
	public String getContextPath() {
		return this.contextPath;
	}

	@Override
	public void setPort(int port) {
		this.port = port;
	}

	/**
	 * The port that the embedded server listens on.
	 * @return the port
	 */
	public int getPort() {
		return this.port;
	}

	@Override
	public void setAddress(InetAddress address) {
		this.address = address;
	}

	/**
	 * @return the address the embedded container binds to
	 */
	public InetAddress getAddress() {
		return this.address;
	}

	@Override
	public void setSessionTimeout(int sessionTimeout) {
		this.sessionTimeout = sessionTimeout;
	}

	@Override
	public void setSessionTimeout(int sessionTimeout, TimeUnit timeUnit) {
		Assert.notNull(timeUnit, "TimeUnit must not be null");
		this.sessionTimeout = (int) timeUnit.toSeconds(sessionTimeout);
	}

	/**
	 * @return the session timeout in seconds
	 */
	public int getSessionTimeout() {
		return this.sessionTimeout;
	}

	@Override
	public void setInitializers(List<? extends ServletContextInitializer> initializers) {
		Assert.notNull(initializers, "Initializers must not be null");
		this.initializers = new ArrayList<ServletContextInitializer>(initializers);
	}

	@Override
	public void addInitializers(ServletContextInitializer... initializers) {
		Assert.notNull(initializers, "Initializers must not be null");
		this.initializers.addAll(Arrays.asList(initializers));
	}

	@Override
	public void setDocumentRoot(File documentRoot) {
		this.documentRoot = documentRoot;
	}

	/**
	 * Returns the document root which will be used by the web context to serve static
	 * files.
	 * @return the document root
	 */
	public File getDocumentRoot() {
		return this.documentRoot;
	}

	@Override
	public void setErrorPages(Set<ErrorPage> errorPages) {
		Assert.notNull(errorPages, "ErrorPages must not be null");
		this.errorPages = new LinkedHashSet<ErrorPage>(errorPages);
	}

	@Override
	public void addErrorPages(ErrorPage... errorPages) {
		Assert.notNull(errorPages, "ErrorPages must not be null");
		this.errorPages.addAll(Arrays.asList(errorPages));
	}

	/**
	 * Returns a mutable set of {@link ErrorPage ErrorPages} that will be used when
	 * handling exceptions.
	 * @return the error pages
	 */
	public Set<ErrorPage> getErrorPages() {
		return this.errorPages;
	}

	@Override
	public void setMimeMappings(MimeMappings mimeMappings) {
		this.mimeMappings = new MimeMappings(mimeMappings);
	}

	/**
	 * Returns the mime-type mappings.
	 * @return the mimeMappings the mime-type mappings.
	 */
	public MimeMappings getMimeMappings() {
		return this.mimeMappings;
	}

	@Override
	public void setRegisterDefaultServlet(boolean registerDefaultServlet) {
		this.registerDefaultServlet = registerDefaultServlet;
	}

	/**
	 * Flag to indicate that the JSP servlet should be registered if available on the
	 * classpath.
	 * @return true if the JSP servlet is to be registered
	 */
	public boolean isRegisterJspServlet() {
		return this.registerJspServlet;
	}

	@Override
	public void setRegisterJspServlet(boolean registerJspServlet) {
		this.registerJspServlet = registerJspServlet;
	}

	/**
	 * Flag to indicate that the default servlet should be registered.
	 * @return true if the default servlet is to be registered
	 */
	public boolean isRegisterDefaultServlet() {
		return this.registerDefaultServlet;
	}

	@Override
	public void setSsl(Ssl ssl) {
		this.ssl = ssl;
	}

	public Ssl getSsl() {
		return this.ssl;
	}

	@Override
	public void setJspServletClassName(String jspServletClassName) {
		this.jspServletClassName = jspServletClassName;
	}

	/**
	 * @return the JSP servlet class name
	 */
	protected String getJspServletClassName() {
		return this.jspServletClassName;
	}

	/**
	 * Utility method that can be used by subclasses wishing to combine the specified
	 * {@link ServletContextInitializer} parameters with those defined in this instance.
	 * @param initializers the initializers to merge
	 * @return a complete set of merged initializers (with the specified parameters
	 * appearing first)
	 */
	protected final ServletContextInitializer[] mergeInitializers(
			ServletContextInitializer... initializers) {
		List<ServletContextInitializer> mergedInitializers = new ArrayList<ServletContextInitializer>();
		mergedInitializers.addAll(Arrays.asList(initializers));
		mergedInitializers.addAll(this.initializers);
		return mergedInitializers
				.toArray(new ServletContextInitializer[mergedInitializers.size()]);
	}

}
