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
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import org.apache.catalina.Container;
import org.apache.catalina.Context;
import org.apache.catalina.LifecycleEvent;
import org.apache.catalina.LifecycleListener;
import org.apache.catalina.LifecycleState;
import org.apache.catalina.Service;
import org.apache.catalina.Valve;
import org.apache.catalina.Wrapper;
import org.apache.catalina.connector.Connector;
import org.apache.catalina.startup.Tomcat;
import org.apache.catalina.valves.RemoteIpValve;
import org.apache.coyote.http11.AbstractHttp11JsseProtocol;
import org.junit.Test;
import org.mockito.InOrder;

import org.springframework.boot.context.embedded.AbstractEmbeddedServletContainerFactoryTests;
import org.springframework.boot.context.embedded.EmbeddedServletContainerException;
import org.springframework.boot.context.embedded.Ssl;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.util.SocketUtils;

import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.fail;
import static org.mockito.BDDMockito.given;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyObject;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

/**
 * Tests for {@link TomcatEmbeddedServletContainerFactory} and
 * {@link TomcatEmbeddedServletContainer}.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Stephane Nicoll
 */
public class TomcatEmbeddedServletContainerFactoryTests
		extends AbstractEmbeddedServletContainerFactoryTests {

	@Override
	protected TomcatEmbeddedServletContainerFactory getFactory() {
		return new TomcatEmbeddedServletContainerFactory(0);
	}

	// JMX MBean names clash if you get more than one Engine with the same name...
	@Test
	public void tomcatEngineNames() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.container = factory.getEmbeddedServletContainer();
		factory.setPort(SocketUtils.findAvailableTcpPort(40000));
		TomcatEmbeddedServletContainer container2 = (TomcatEmbeddedServletContainer) factory
				.getEmbeddedServletContainer();

		// Make sure that the names are different
		String firstContainerName = ((TomcatEmbeddedServletContainer) this.container)
				.getTomcat().getEngine().getName();
		String secondContainerName = container2.getTomcat().getEngine().getName();
		assertFalse("Tomcat engines must have different names",
				firstContainerName.equals(secondContainerName));
		container2.stop();
	}

	@Test
	public void tomcatListeners() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		LifecycleListener[] listeners = new LifecycleListener[4];
		for (int i = 0; i < listeners.length; i++) {
			listeners[i] = mock(LifecycleListener.class);
		}
		factory.setContextLifecycleListeners(Arrays.asList(listeners[0], listeners[1]));
		factory.addContextLifecycleListeners(listeners[2], listeners[3]);
		this.container = factory.getEmbeddedServletContainer();
		InOrder ordered = inOrder((Object[]) listeners);
		for (LifecycleListener listener : listeners) {
			ordered.verify(listener).lifecycleEvent((LifecycleEvent) anyObject());
		}
	}

	@Test
	public void tomcatCustomizers() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		TomcatContextCustomizer[] listeners = new TomcatContextCustomizer[4];
		for (int i = 0; i < listeners.length; i++) {
			listeners[i] = mock(TomcatContextCustomizer.class);
		}
		factory.setTomcatContextCustomizers(Arrays.asList(listeners[0], listeners[1]));
		factory.addContextCustomizers(listeners[2], listeners[3]);
		this.container = factory.getEmbeddedServletContainer();
		InOrder ordered = inOrder((Object[]) listeners);
		for (TomcatContextCustomizer listener : listeners) {
			ordered.verify(listener).customize((Context) anyObject());
		}
	}

	@Test
	public void tomcatConnectorCustomizers() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		TomcatConnectorCustomizer[] listeners = new TomcatConnectorCustomizer[4];
		for (int i = 0; i < listeners.length; i++) {
			listeners[i] = mock(TomcatConnectorCustomizer.class);
		}
		factory.setTomcatConnectorCustomizers(Arrays.asList(listeners[0], listeners[1]));
		factory.addConnectorCustomizers(listeners[2], listeners[3]);
		this.container = factory.getEmbeddedServletContainer();
		InOrder ordered = inOrder((Object[]) listeners);
		for (TomcatConnectorCustomizer listener : listeners) {
			ordered.verify(listener).customize((Connector) anyObject());
		}
	}

	@Test
	public void tomcatAdditionalConnectors() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		Connector[] listeners = new Connector[4];
		for (int i = 0; i < listeners.length; i++) {
			Connector connector = mock(Connector.class);
			given(connector.getState()).willReturn(LifecycleState.STOPPED);
			listeners[i] = connector;
		}
		factory.addAdditionalTomcatConnectors(listeners);
		this.container = factory.getEmbeddedServletContainer();
		Map<Service, Connector[]> connectors = ((TomcatEmbeddedServletContainer) this.container)
				.getServiceConnectors();
		assertThat(connectors.values().iterator().next().length,
				equalTo(listeners.length + 1));
	}

	@Test
	public void addNullAdditionalConnectorThrows() {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("Connectors must not be null");
		factory.addAdditionalTomcatConnectors((Connector[]) null);
	}

	@Test
	public void sessionTimeout() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.setSessionTimeout(10);
		assertTimeout(factory, 1);
	}

	@Test
	public void sessionTimeoutInMins() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.setSessionTimeout(1, TimeUnit.MINUTES);
		assertTimeout(factory, 1);
	}

	@Test
	public void noSessionTimeout() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.setSessionTimeout(0);
		assertTimeout(factory, -1);
	}

	@Test
	public void valve() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		Valve valve = mock(Valve.class);
		factory.addContextValves(valve);
		this.container = factory.getEmbeddedServletContainer();
		verify(valve).setNext(any(Valve.class));
	}

	@Test
	public void setNullTomcatContextCustomizersThrows() {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("TomcatContextCustomizers must not be null");
		factory.setTomcatContextCustomizers(null);
	}

	@Test
	public void addNullContextCustomizersThrows() {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("TomcatContextCustomizers must not be null");
		factory.addContextCustomizers((TomcatContextCustomizer[]) null);
	}

	@Test
	public void setNullTomcatConnectorCustomizersThrows() {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("TomcatConnectorCustomizers must not be null");
		factory.setTomcatConnectorCustomizers(null);
	}

	@Test
	public void addNullConnectorCustomizersThrows() {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("TomcatConnectorCustomizers must not be null");
		factory.addConnectorCustomizers((TomcatConnectorCustomizer[]) null);
	}

	@Test
	public void uriEncoding() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.setUriEncoding(Charset.forName("US-ASCII"));
		Tomcat tomcat = getTomcat(factory);
		assertEquals("US-ASCII", tomcat.getConnector().getURIEncoding());
	}

	@Test
	public void defaultUriEncoding() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		Tomcat tomcat = getTomcat(factory);
		assertEquals("UTF-8", tomcat.getConnector().getURIEncoding());
	}

	@Test
	public void sslCiphersConfiguration() throws Exception {
		Ssl ssl = new Ssl();
		ssl.setKeyStore("test.jks");
		ssl.setKeyStorePassword("secret");
		ssl.setCiphers(new String[] { "ALPHA", "BRAVO", "CHARLIE" });

		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.setSsl(ssl);

		Tomcat tomcat = getTomcat(factory);
		Connector connector = tomcat.getConnector();

		AbstractHttp11JsseProtocol<?> jsseProtocol = (AbstractHttp11JsseProtocol<?>) connector
				.getProtocolHandler();
		assertThat(jsseProtocol.getCiphers(), equalTo("ALPHA,BRAVO,CHARLIE"));
	}

	@Test
	public void primaryConnectorPortClashThrowsIllegalStateException()
			throws InterruptedException, IOException {
		final int port = SocketUtils.findAvailableTcpPort(40000);

		doWithBlockedPort(port, new Runnable() {

			@Override
			public void run() {
				TomcatEmbeddedServletContainerFactory factory = getFactory();
				factory.setPort(port);

				try {
					TomcatEmbeddedServletContainerFactoryTests.this.container = factory
							.getEmbeddedServletContainer();
					TomcatEmbeddedServletContainerFactoryTests.this.container.start();
					fail();
				}
				catch (EmbeddedServletContainerException ex) {
					// Ignore
				}
			}

		});

	}

	@Test
	public void additionalConnectorPortClashThrowsIllegalStateException()
			throws InterruptedException, IOException {
		final int port = SocketUtils.findAvailableTcpPort(40000);

		doWithBlockedPort(port, new Runnable() {

			@Override
			public void run() {
				TomcatEmbeddedServletContainerFactory factory = getFactory();
				Connector connector = new Connector(
						"org.apache.coyote.http11.Http11NioProtocol");
				connector.setPort(port);
				factory.addAdditionalTomcatConnectors(connector);

				try {
					TomcatEmbeddedServletContainerFactoryTests.this.container = factory
							.getEmbeddedServletContainer();
					TomcatEmbeddedServletContainerFactoryTests.this.container.start();
					fail();
				}
				catch (EmbeddedServletContainerException ex) {
					// Ignore
				}
			}

		});

	}

	@Test
	public void jspServletInitParameters() {
		Map<String, String> initParameters = new HashMap<String, String>();
		initParameters.put("a", "alpha");
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.getJspServlet().setInitParameters(initParameters);
		this.container = factory.getEmbeddedServletContainer();
		Wrapper jspServlet = getJspServlet();
		assertThat(jspServlet.findInitParameter("a"), is(equalTo("alpha")));
	}

	@Test
	public void useForwardHeaders() throws Exception {
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		factory.addContextValves(new RemoteIpValve());
		assertForwardHeaderIsUsed(factory);
	}

	@Test
	public void disableDoesNotSaveSessionFiles() throws Exception {
		File baseDir = this.temporaryFolder.newFolder();
		TomcatEmbeddedServletContainerFactory factory = getFactory();
		// If baseDir is not set SESSIONS.ser is written to a different temp directory
		// each time. By setting it we can really ensure that data isn't saved
		factory.setBaseDirectory(baseDir);
		this.container = factory
				.getEmbeddedServletContainer(sessionServletRegistration());
		this.container.start();
		String s1 = getResponse(getLocalUrl("/session"));
		String s2 = getResponse(getLocalUrl("/session"));
		this.container.stop();
		this.container = factory
				.getEmbeddedServletContainer(sessionServletRegistration());
		this.container.start();
		String s3 = getResponse(getLocalUrl("/session"));
		System.out.println(s1);
		System.out.println(s2);
		System.out.println(s3);
		String message = "Session error s1=" + s1 + " s2=" + s2 + " s3=" + s3;
		assertThat(message, s2.split(":")[0], equalTo(s1.split(":")[1]));
		assertThat(message, s3.split(":")[0], not(equalTo(s2.split(":")[1])));
	}

	@Override
	protected Wrapper getJspServlet() {
		Container context = ((TomcatEmbeddedServletContainer) this.container).getTomcat()
				.getHost().findChildren()[0];
		return (Wrapper) context.findChild("jsp");
	}

	@SuppressWarnings("unchecked")
	@Override
	protected Map<String, String> getActualMimeMappings() {
		Context context = (Context) ((TomcatEmbeddedServletContainer) this.container)
				.getTomcat().getHost().findChildren()[0];
		return (Map<String, String>) ReflectionTestUtils.getField(context,
				"mimeMappings");
	}

	private void assertTimeout(TomcatEmbeddedServletContainerFactory factory,
			int expected) {
		Tomcat tomcat = getTomcat(factory);
		Context context = (Context) tomcat.getHost().findChildren()[0];
		assertThat(context.getSessionTimeout(), equalTo(expected));
	}

	private Tomcat getTomcat(TomcatEmbeddedServletContainerFactory factory) {
		this.container = factory.getEmbeddedServletContainer();
		return ((TomcatEmbeddedServletContainer) this.container).getTomcat();
	}

	private void doWithBlockedPort(final int port, Runnable action) throws IOException {
		ServerSocket serverSocket = new ServerSocket();
		serverSocket.bind(new InetSocketAddress(port));
		try {
			action.run();
		}
		finally {
			serverSocket.close();
		}
	}

}
