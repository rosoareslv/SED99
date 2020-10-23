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

package org.springframework.boot.autoconfigure.web;

import java.util.Arrays;
import java.util.List;

import javax.servlet.MultipartConfigElement;
import javax.servlet.ServletRegistration;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.boot.autoconfigure.AutoConfigureAfter;
import org.springframework.boot.autoconfigure.AutoConfigureOrder;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionOutcome;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnWebApplication;
import org.springframework.boot.autoconfigure.condition.SpringBootCondition;
import org.springframework.boot.context.embedded.ServletRegistrationBean;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.boot.context.web.SpringBootServletInitializer;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.ConditionContext;
import org.springframework.context.annotation.Conditional;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.core.type.AnnotatedTypeMetadata;
import org.springframework.web.multipart.MultipartResolver;
import org.springframework.web.servlet.DispatcherServlet;

/**
 * {@link EnableAutoConfiguration Auto-configuration} for the Spring
 * {@link DispatcherServlet}. Should work for a standalone application where an embedded
 * servlet container is already present and also for a deployable application using
 * {@link SpringBootServletInitializer}.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Stephane Nicoll
 */
@AutoConfigureOrder(Ordered.HIGHEST_PRECEDENCE)
@Configuration
@ConditionalOnWebApplication
@ConditionalOnClass(DispatcherServlet.class)
@AutoConfigureAfter(EmbeddedServletContainerAutoConfiguration.class)
public class DispatcherServletAutoConfiguration {

	/*
	 * The bean name for a DispatcherServlet that will be mapped to the root URL "/"
	 */
	public static final String DEFAULT_DISPATCHER_SERVLET_BEAN_NAME = "dispatcherServlet";

	/*
	 * The bean name for a ServletRegistrationBean for the DispatcherServlet "/"
	 */
	public static final String DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME = "dispatcherServletRegistration";

	@Configuration
	@Conditional(DefaultDispatcherServletCondition.class)
	@ConditionalOnClass(ServletRegistration.class)
	@EnableConfigurationProperties(WebMvcProperties.class)
	protected static class DispatcherServletConfiguration {

		@Autowired
		private ServerProperties server;

		@Autowired
		private WebMvcProperties webMvcProperties;

		@Autowired(required = false)
		private MultipartConfigElement multipartConfig;

		@Bean(name = DEFAULT_DISPATCHER_SERVLET_BEAN_NAME)
		public DispatcherServlet dispatcherServlet() {
			DispatcherServlet dispatcherServlet = new DispatcherServlet();
			dispatcherServlet.setDispatchOptionsRequest(
					this.webMvcProperties.isDispatchOptionsRequest());
			dispatcherServlet.setDispatchTraceRequest(
					this.webMvcProperties.isDispatchTraceRequest());
			dispatcherServlet.setThrowExceptionIfNoHandlerFound(
					this.webMvcProperties.isThrowExceptionIfNoHandlerFound());
			return dispatcherServlet;
		}

		@Bean(name = DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME)
		public ServletRegistrationBean dispatcherServletRegistration() {
			ServletRegistrationBean registration = new ServletRegistrationBean(
					dispatcherServlet(), this.server.getServletMapping());
			registration.setName(DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
			if (this.multipartConfig != null) {
				registration.setMultipartConfig(this.multipartConfig);
			}
			return registration;
		}

		@Bean
		@ConditionalOnBean(MultipartResolver.class)
		@ConditionalOnMissingBean(name = DispatcherServlet.MULTIPART_RESOLVER_BEAN_NAME)
		public MultipartResolver multipartResolver(MultipartResolver resolver) {
			// Detect if the user has created a MultipartResolver but named it incorrectly
			return resolver;
		}

	}

	@Order(Ordered.LOWEST_PRECEDENCE - 10)
	private static class DefaultDispatcherServletCondition extends SpringBootCondition {

		@Override
		public ConditionOutcome getMatchOutcome(ConditionContext context,
				AnnotatedTypeMetadata metadata) {
			ConfigurableListableBeanFactory beanFactory = context.getBeanFactory();
			ConditionOutcome outcome = checkServlets(beanFactory);
			if (!outcome.isMatch()) {
				return outcome;
			}
			return checkServletRegistrations(beanFactory);
		}

		private ConditionOutcome checkServlets(
				ConfigurableListableBeanFactory beanFactory) {
			List<String> servlets = Arrays.asList(beanFactory
					.getBeanNamesForType(DispatcherServlet.class, false, false));
			boolean containsDispatcherBean = beanFactory
					.containsBean(DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
			if (servlets.isEmpty()) {
				if (containsDispatcherBean) {
					return ConditionOutcome.noMatch("found no DispatcherServlet "
							+ "but a non-DispatcherServlet named "
							+ DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
				}
				return ConditionOutcome.match("no DispatcherServlet found");
			}
			if (servlets.contains(DEFAULT_DISPATCHER_SERVLET_BEAN_NAME)) {
				return ConditionOutcome.noMatch("found DispatcherServlet named "
						+ DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
			}
			if (containsDispatcherBean) {
				return ConditionOutcome.noMatch("found non-DispatcherServlet named "
						+ DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
			}
			return ConditionOutcome.match("one or more DispatcherServlets "
					+ "found and none is named " + DEFAULT_DISPATCHER_SERVLET_BEAN_NAME);
		}

		private ConditionOutcome checkServletRegistrations(
				ConfigurableListableBeanFactory beanFactory) {
			List<String> registrations = Arrays.asList(beanFactory
					.getBeanNamesForType(ServletRegistrationBean.class, false, false));
			boolean containsDispatcherRegistrationBean = beanFactory
					.containsBean(DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME);
			if (registrations.isEmpty()) {
				if (containsDispatcherRegistrationBean) {
					return ConditionOutcome.noMatch("found no ServletRegistrationBean "
							+ "but a non-ServletRegistrationBean named "
							+ DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME);
				}
				return ConditionOutcome.match("no ServletRegistrationBean found");
			}
			if (registrations
					.contains(DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME)) {
				return ConditionOutcome.noMatch("found ServletRegistrationBean named "
						+ DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME);
			}
			if (containsDispatcherRegistrationBean) {
				return ConditionOutcome.noMatch("found non-ServletRegistrationBean named "
						+ DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME);
			}
			return ConditionOutcome
					.match("one or more ServletRegistrationBeans is found and none is named "
							+ DEFAULT_DISPATCHER_SERVLET_REGISTRATION_BEAN_NAME);

		}
	}

}
