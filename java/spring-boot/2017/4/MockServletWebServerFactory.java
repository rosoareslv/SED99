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

package org.springframework.boot.web.servlet.server;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;

import javax.servlet.Filter;
import javax.servlet.FilterRegistration;
import javax.servlet.RequestDispatcher;
import javax.servlet.Servlet;
import javax.servlet.ServletContext;
import javax.servlet.ServletException;
import javax.servlet.ServletRegistration;

import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;

import org.springframework.boot.web.server.WebServer;
import org.springframework.boot.web.server.WebServerException;
import org.springframework.boot.web.servlet.ServletContextInitializer;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;

/**
 * Mock {@link ServletWebServerFactory}.
 *
 * @author Phillip Webb
 * @author Andy Wilkinson
 */
public class MockServletWebServerFactory extends AbstractServletWebServerFactory {

	private MockServletWebServer webServer;

	@Override
	public WebServer getWebServer(ServletContextInitializer... initializers) {
		this.webServer = spy(
				new MockServletWebServer(mergeInitializers(initializers), getPort()));
		return this.webServer;
	}

	public MockServletWebServer getWebServer() {
		return this.webServer;
	}

	public ServletContext getServletContext() {
		return getWebServer() == null ? null : getWebServer().servletContext;
	}

	public RegisteredServlet getRegisteredServlet(int index) {
		return getWebServer() == null ? null
				: getWebServer().getRegisteredServlets().get(index);
	}

	public RegisteredFilter getRegisteredFilter(int index) {
		return getWebServer() == null ? null
				: getWebServer().getRegisteredFilters().get(index);
	}

	public static class MockServletWebServer implements WebServer {

		private ServletContext servletContext;

		private final ServletContextInitializer[] initializers;

		private final List<RegisteredServlet> registeredServlets = new ArrayList<>();

		private final List<RegisteredFilter> registeredFilters = new ArrayList<>();

		private final int port;

		public MockServletWebServer(ServletContextInitializer[] initializers, int port) {
			this.initializers = initializers;
			this.port = port;
			initialize();
		}

		private void initialize() {
			try {
				this.servletContext = mock(ServletContext.class);
				given(this.servletContext.addServlet(anyString(), (Servlet) any()))
						.willAnswer(new Answer<ServletRegistration.Dynamic>() {
							@Override
							public ServletRegistration.Dynamic answer(
									InvocationOnMock invocation) throws Throwable {
								RegisteredServlet registeredServlet = new RegisteredServlet(
										(Servlet) invocation.getArguments()[1]);
								MockServletWebServer.this.registeredServlets
										.add(registeredServlet);
								return registeredServlet.getRegistration();
							}
						});
				given(this.servletContext.addFilter(anyString(), (Filter) any()))
						.willAnswer(new Answer<FilterRegistration.Dynamic>() {
							@Override
							public FilterRegistration.Dynamic answer(
									InvocationOnMock invocation) throws Throwable {
								RegisteredFilter registeredFilter = new RegisteredFilter(
										(Filter) invocation.getArguments()[1]);
								MockServletWebServer.this.registeredFilters
										.add(registeredFilter);
								return registeredFilter.getRegistration();
							}
						});
				final Map<String, String> initParameters = new HashMap<>();
				given(this.servletContext.setInitParameter(anyString(), anyString()))
						.will(new Answer<Void>() {
							@Override
							public Void answer(InvocationOnMock invocation)
									throws Throwable {
								initParameters.put(invocation.getArgument(0),
										invocation.getArgument(1));
								return null;
							}

						});
				given(this.servletContext.getInitParameterNames())
						.willReturn(Collections.enumeration(initParameters.keySet()));
				given(this.servletContext.getInitParameter(anyString()))
						.willAnswer(new Answer<String>() {
							@Override
							public String answer(InvocationOnMock invocation)
									throws Throwable {
								return initParameters.get(invocation.getArgument(0));
							}
						});
				given(this.servletContext.getAttributeNames())
						.willReturn(MockServletWebServer.<String>emptyEnumeration());
				given(this.servletContext.getNamedDispatcher("default"))
						.willReturn(mock(RequestDispatcher.class));
				for (ServletContextInitializer initializer : this.initializers) {
					initializer.onStartup(this.servletContext);
				}
			}
			catch (ServletException ex) {
				throw new RuntimeException(ex);
			}
		}

		@SuppressWarnings("unchecked")
		public static <T> Enumeration<T> emptyEnumeration() {
			return (Enumeration<T>) EmptyEnumeration.EMPTY_ENUMERATION;
		}

		@Override
		public void start() throws WebServerException {
		}

		@Override
		public void stop() {
			this.servletContext = null;
			this.registeredServlets.clear();
		}

		public Servlet[] getServlets() {
			Servlet[] servlets = new Servlet[this.registeredServlets.size()];
			for (int i = 0; i < servlets.length; i++) {
				servlets[i] = this.registeredServlets.get(i).getServlet();
			}
			return servlets;
		}

		public List<RegisteredServlet> getRegisteredServlets() {
			return this.registeredServlets;
		}

		public List<RegisteredFilter> getRegisteredFilters() {
			return this.registeredFilters;
		}

		@Override
		public int getPort() {
			return this.port;
		}

		private static class EmptyEnumeration<E> implements Enumeration<E> {

			static final EmptyEnumeration<Object> EMPTY_ENUMERATION = new EmptyEnumeration<>();

			@Override
			public boolean hasMoreElements() {
				return false;
			}

			@Override
			public E nextElement() {
				throw new NoSuchElementException();
			}

		}

	}

	public static class RegisteredServlet {

		private final Servlet servlet;

		private final ServletRegistration.Dynamic registration;

		public RegisteredServlet(Servlet servlet) {
			this.servlet = servlet;
			this.registration = mock(ServletRegistration.Dynamic.class);
		}

		public ServletRegistration.Dynamic getRegistration() {
			return this.registration;
		}

		public Servlet getServlet() {
			return this.servlet;
		}

	}

	public static class RegisteredFilter {

		private final Filter filter;

		private final FilterRegistration.Dynamic registration;

		public RegisteredFilter(Filter filter) {
			this.filter = filter;
			this.registration = mock(FilterRegistration.Dynamic.class);
		}

		public FilterRegistration.Dynamic getRegistration() {
			return this.registration;
		}

		public Filter getFilter() {
			return this.filter;
		}

	}

}
