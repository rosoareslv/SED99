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

package org.springframework.boot.context.embedded.undertow;

import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;

import io.undertow.Undertow.Builder;
import io.undertow.servlet.api.DeploymentInfo;
import io.undertow.servlet.api.DeploymentManager;
import io.undertow.servlet.api.ServletContainer;
import org.junit.Test;
import org.mockito.InOrder;

import org.springframework.boot.context.embedded.AbstractEmbeddedServletContainerFactory;
import org.springframework.boot.context.embedded.AbstractEmbeddedServletContainerFactoryTests;
import org.springframework.boot.context.embedded.ErrorPage;
import org.springframework.boot.context.embedded.ExampleServlet;
import org.springframework.boot.context.embedded.MimeMappings.Mapping;
import org.springframework.boot.context.embedded.ServletRegistrationBean;
import org.springframework.http.HttpStatus;
import org.springframework.test.util.ReflectionTestUtils;

import static org.hamcrest.Matchers.arrayContaining;
import static org.hamcrest.Matchers.arrayWithSize;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.anyObject;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;

/**
 * Tests for {@link UndertowEmbeddedServletContainerFactory} and
 * {@link UndertowEmbeddedServletContainer} .
 *
 * @author Ivan Sopov
 * @author Andy Wilkinson
 */
public class UndertowEmbeddedServletContainerFactoryTests
		extends AbstractEmbeddedServletContainerFactoryTests {

	@Override
	protected UndertowEmbeddedServletContainerFactory getFactory() {
		return new UndertowEmbeddedServletContainerFactory(0);
	}

	@Test
	public void errorPage404() throws Exception {
		AbstractEmbeddedServletContainerFactory factory = getFactory();
		factory.addErrorPages(new ErrorPage(HttpStatus.NOT_FOUND, "/hello"));
		this.container = factory.getEmbeddedServletContainer(
				new ServletRegistrationBean(new ExampleServlet(), "/hello"));
		this.container.start();
		assertThat(getResponse(getLocalUrl("/hello")), equalTo("Hello World"));
		assertThat(getResponse(getLocalUrl("/not-found")), equalTo("Hello World"));
	}

	@Test
	public void setNullBuilderCustomizersThrows() {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("Customizers must not be null");
		factory.setBuilderCustomizers(null);
	}

	@Test
	public void addNullAddBuilderCustomizersThrows() {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("Customizers must not be null");
		factory.addBuilderCustomizers((UndertowBuilderCustomizer[]) null);
	}

	@Test
	public void builderCustomizers() throws Exception {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		UndertowBuilderCustomizer[] customizers = new UndertowBuilderCustomizer[4];
		for (int i = 0; i < customizers.length; i++) {
			customizers[i] = mock(UndertowBuilderCustomizer.class);
		}
		factory.setBuilderCustomizers(Arrays.asList(customizers[0], customizers[1]));
		factory.addBuilderCustomizers(customizers[2], customizers[3]);
		this.container = factory.getEmbeddedServletContainer();
		InOrder ordered = inOrder((Object[]) customizers);
		for (UndertowBuilderCustomizer customizer : customizers) {
			ordered.verify(customizer).customize((Builder) anyObject());
		}
	}

	@Test
	public void setNullDeploymentInfoCustomizersThrows() {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("Customizers must not be null");
		factory.setDeploymentInfoCustomizers(null);
	}

	@Test
	public void addNullAddDeploymentInfoCustomizersThrows() {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		this.thrown.expect(IllegalArgumentException.class);
		this.thrown.expectMessage("Customizers must not be null");
		factory.addDeploymentInfoCustomizers((UndertowDeploymentInfoCustomizer[]) null);
	}

	@Test
	public void deploymentInfo() throws Exception {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		UndertowDeploymentInfoCustomizer[] customizers = new UndertowDeploymentInfoCustomizer[4];
		for (int i = 0; i < customizers.length; i++) {
			customizers[i] = mock(UndertowDeploymentInfoCustomizer.class);
		}
		factory.setDeploymentInfoCustomizers(
				Arrays.asList(customizers[0], customizers[1]));
		factory.addDeploymentInfoCustomizers(customizers[2], customizers[3]);
		this.container = factory.getEmbeddedServletContainer();
		InOrder ordered = inOrder((Object[]) customizers);
		for (UndertowDeploymentInfoCustomizer customizer : customizers) {
			ordered.verify(customizer).customize((DeploymentInfo) anyObject());
		}
	}

	@Test
	public void basicSslClasspathKeyStore() throws Exception {
		testBasicSslWithKeyStore("classpath:test.jks");
	}

	@Test
	public void defaultContextPath() throws Exception {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		final AtomicReference<String> contextPath = new AtomicReference<String>();
		factory.addDeploymentInfoCustomizers(new UndertowDeploymentInfoCustomizer() {

			@Override
			public void customize(DeploymentInfo deploymentInfo) {
				contextPath.set(deploymentInfo.getContextPath());
			}
		});
		this.container = factory.getEmbeddedServletContainer();
		assertEquals("/", contextPath.get());
	}

	@Test
	public void useForwardHeaders() throws Exception {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		factory.setUseForwardHeaders(true);
		assertForwardHeaderIsUsed(factory);
	}

	@Test
	public void eachFactoryUsesADiscreteServletContainer() {
		assertThat(getServletContainerFromNewFactory(),
				is(not(equalTo(getServletContainerFromNewFactory()))));
	}

	@Test
	public void accessLogCanBeEnabled()
			throws IOException, URISyntaxException, InterruptedException {
		UndertowEmbeddedServletContainerFactory factory = getFactory();
		factory.setAccessLogEnabled(true);
		File accessLogDirectory = this.temporaryFolder.getRoot();
		factory.setAccessLogDirectory(accessLogDirectory);
		assertThat(accessLogDirectory.listFiles(), is(arrayWithSize(0)));
		this.container = factory.getEmbeddedServletContainer(
				new ServletRegistrationBean(new ExampleServlet(), "/hello"));
		this.container.start();
		assertThat(getResponse(getLocalUrl("/hello")), equalTo("Hello World"));
		File accessLog = new File(accessLogDirectory, "access_log.log");
		awaitFile(accessLog);
		assertThat(accessLogDirectory.listFiles(), is(arrayContaining(accessLog)));
	}

	@Override
	protected Object getJspServlet() {
		return null; // Undertow does not support JSPs
	}

	private void awaitFile(File file) throws InterruptedException {
		long end = System.currentTimeMillis() + 10000;
		while (!file.exists() && System.currentTimeMillis() < end) {
			Thread.sleep(100);
		}
	}

	private ServletContainer getServletContainerFromNewFactory() {
		UndertowEmbeddedServletContainer undertow1 = (UndertowEmbeddedServletContainer) getFactory()
				.getEmbeddedServletContainer();
		return ((DeploymentManager) ReflectionTestUtils.getField(undertow1, "manager"))
				.getDeployment().getServletContainer();
	}

	@Override
	protected Map<String, String> getActualMimeMappings() {
		return ((DeploymentManager) ReflectionTestUtils.getField(this.container,
				"manager")).getDeployment().getMimeExtensionMappings();
	}

	@Override
	protected Collection<Mapping> getExpectedMimeMappings() {
		// Unlike Tomcat and Jetty, Undertow performs a case-sensitive match on file
		// extension so it has a mapping for "z" and "Z".
		Set<Mapping> expectedMappings = new HashSet<Mapping>(
				super.getExpectedMimeMappings());
		expectedMappings.add(new Mapping("Z", "application/x-compress"));
		return expectedMappings;
	}

}
