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

package org.springframework.boot.autoconfigure.security;

import java.util.EnumSet;

import javax.servlet.DispatcherType;

import org.junit.After;
import org.junit.Test;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.autoconfigure.PropertyPlaceholderAutoConfiguration;
import org.springframework.boot.autoconfigure.TestAutoConfigurationPackage;
import org.springframework.boot.autoconfigure.jdbc.DataSourceAutoConfiguration;
import org.springframework.boot.autoconfigure.orm.jpa.HibernateJpaAutoConfiguration;
import org.springframework.boot.autoconfigure.orm.jpa.test.City;
import org.springframework.boot.autoconfigure.web.ServerPropertiesAutoConfiguration;
import org.springframework.boot.context.embedded.DelegatingFilterProxyRegistrationBean;
import org.springframework.boot.context.embedded.FilterRegistrationBean;
import org.springframework.boot.test.EnvironmentTestUtils;
import org.springframework.context.ApplicationEvent;
import org.springframework.context.ApplicationListener;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.annotation.Order;
import org.springframework.mock.web.MockServletContext;
import org.springframework.orm.jpa.JpaTransactionManager;
import org.springframework.security.authentication.AuthenticationManager;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.authentication.TestingAuthenticationToken;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.authentication.event.AbstractAuthenticationEvent;
import org.springframework.security.authentication.event.AuthenticationFailureBadCredentialsEvent;
import org.springframework.security.config.annotation.authentication.builders.AuthenticationManagerBuilder;
import org.springframework.security.config.annotation.authentication.configurers.GlobalAuthenticationConfigurerAdapter;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.config.annotation.web.configuration.WebSecurityConfigurerAdapter;
import org.springframework.security.core.Authentication;
import org.springframework.security.core.AuthenticationException;
import org.springframework.security.core.authority.AuthorityUtils;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.security.data.repository.query.SecurityEvaluationContextExtension;
import org.springframework.security.web.FilterChainProxy;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.context.support.AnnotationConfigWebApplicationContext;

import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

/**
 * Tests for {@link SecurityAutoConfiguration}.
 *
 * @author Dave Syer
 * @author Rob Winch
 * @author Andy Wilkinson
 */
public class SecurityAutoConfigurationTests {

	private AnnotationConfigWebApplicationContext context;

	@After
	public void close() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	public void testWebConfiguration() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertNotNull(this.context.getBean(AuthenticationManagerBuilder.class));
		// 4 for static resources and one for the rest
		assertEquals(5,
				this.context.getBean(FilterChainProxy.class).getFilterChains().size());
	}

	@Test
	public void testDefaultFilterOrderWithSecurityAdapter() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(WebSecurity.class, SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertEquals(FilterRegistrationBean.REQUEST_WRAPPER_FILTER_MAX_ORDER - 100,
				this.context.getBean("securityFilterChainRegistration",
						DelegatingFilterProxyRegistrationBean.class).getOrder());
	}

	@Test
	public void testFilterIsNotRegisteredInNonWeb() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext();
		context.register(SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		try {
			context.refresh();
			assertFalse(context.containsBean("securityFilterChainRegistration"));
		}
		finally {
			context.close();
		}
	}

	@Test
	public void testDefaultFilterOrder() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertEquals(FilterRegistrationBean.REQUEST_WRAPPER_FILTER_MAX_ORDER - 100,
				this.context.getBean("securityFilterChainRegistration",
						DelegatingFilterProxyRegistrationBean.class).getOrder());
	}

	@Test
	public void testCustomFilterOrder() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		EnvironmentTestUtils.addEnvironment(this.context, "security.filter-order:12345");
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertEquals(12345, this.context.getBean("securityFilterChainRegistration",
				DelegatingFilterProxyRegistrationBean.class).getOrder());
	}

	@Test
	public void testDisableIgnoredStaticApplicationPaths() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context, "security.ignored:none");
		this.context.refresh();
		// Just the application endpoints now
		assertEquals(1,
				this.context.getBean(FilterChainProxy.class).getFilterChains().size());
	}

	@Test
	public void testDisableBasicAuthOnApplicationPaths() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context, "security.basic.enabled:false");
		this.context.refresh();
		// Ignores and the "matches-none" filter only
		assertEquals(1, this.context.getBeanNamesForType(FilterChainProxy.class).length);
	}

	@Test
	public void testAuthenticationManagerCreated() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertNotNull(this.context.getBean(AuthenticationManager.class));
	}

	@Test
	public void testEventPublisherInjected() throws Exception {
		testAuthenticationManagerCreated();
		pingAuthenticationListener();
	}

	private void pingAuthenticationListener() {
		AuthenticationListener listener = new AuthenticationListener();
		this.context.addApplicationListener(listener);
		AuthenticationManager manager = this.context.getBean(AuthenticationManager.class);
		try {
			manager.authenticate(new UsernamePasswordAuthenticationToken("foo", "wrong"));
			fail("Expected BadCredentialsException");
		}
		catch (BadCredentialsException e) {
			// expected
		}
		assertTrue("Wrong event type: " + listener.event,
				listener.event instanceof AuthenticationFailureBadCredentialsEvent);
	}

	@Test
	public void testOverrideAuthenticationManager() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(TestAuthenticationConfiguration.class,
				SecurityAutoConfiguration.class, ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertEquals(
				this.context.getBean(
						TestAuthenticationConfiguration.class).authenticationManager,
				this.context.getBean(AuthenticationManager.class));
	}

	@Test
	public void testDefaultAuthenticationManagerMakesUserDetailsAvailable()
			throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(UserDetailsSecurityCustomizer.class,
				SecurityAutoConfiguration.class, ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertNotNull(this.context.getBean(UserDetailsSecurityCustomizer.class)
				.getUserDetails().loadUserByUsername("user"));
	}

	@Test
	public void testOverrideAuthenticationManagerAndInjectIntoSecurityFilter()
			throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(TestAuthenticationConfiguration.class,
				SecurityCustomizer.class, SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		assertEquals(
				this.context.getBean(
						TestAuthenticationConfiguration.class).authenticationManager,
				this.context.getBean(AuthenticationManager.class));
	}

	@Test
	public void testOverrideAuthenticationManagerWithBuilderAndInjectIntoSecurityFilter()
			throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(AuthenticationManagerCustomizer.class,
				SecurityCustomizer.class, SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		UsernamePasswordAuthenticationToken user = new UsernamePasswordAuthenticationToken(
				"foo", "bar",
				AuthorityUtils.commaSeparatedStringToAuthorityList("ROLE_USER"));
		assertNotNull(
				this.context.getBean(AuthenticationManager.class).authenticate(user));
		pingAuthenticationListener();
	}

	@Test
	public void testOverrideAuthenticationManagerWithBuilderAndInjectBuilderIntoSecurityFilter()
			throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(AuthenticationManagerCustomizer.class,
				WorkaroundSecurityCustomizer.class, SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		UsernamePasswordAuthenticationToken user = new UsernamePasswordAuthenticationToken(
				"foo", "bar",
				AuthorityUtils.commaSeparatedStringToAuthorityList("ROLE_USER"));
		assertNotNull(
				this.context.getBean(AuthenticationManager.class).authenticate(user));
	}

	@Test
	public void testJpaCoexistsHappily() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.datasource.url:jdbc:hsqldb:mem:testsecdb");
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.datasource.initialize:false");
		this.context.register(EntityConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class,
				DataSourceAutoConfiguration.class, HibernateJpaAutoConfiguration.class,
				SecurityAutoConfiguration.class, ServerPropertiesAutoConfiguration.class);
		// This can fail if security @Conditionals force early instantiation of the
		// HibernateJpaAutoConfiguration (e.g. the EntityManagerFactory is not found)
		this.context.refresh();
		assertNotNull(this.context.getBean(JpaTransactionManager.class));
	}

	@Test
	public void testDefaultUsernamePassword() throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());

		this.context.register(SecurityAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class);
		this.context.refresh();

		SecurityProperties security = this.context.getBean(SecurityProperties.class);
		AuthenticationManager manager = this.context.getBean(AuthenticationManager.class);

		UsernamePasswordAuthenticationToken token = new UsernamePasswordAuthenticationToken(
				security.getUser().getName(), security.getUser().getPassword());
		assertNotNull(manager.authenticate(token));
	}

	@Test
	public void testCustomAuthenticationDoesNotAuthenticateWithBootSecurityUser()
			throws Exception {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());

		this.context.register(AuthenticationManagerCustomizer.class,
				SecurityAutoConfiguration.class, ServerPropertiesAutoConfiguration.class);
		this.context.refresh();

		SecurityProperties security = this.context.getBean(SecurityProperties.class);
		AuthenticationManager manager = this.context.getBean(AuthenticationManager.class);

		UsernamePasswordAuthenticationToken token = new UsernamePasswordAuthenticationToken(
				security.getUser().getName(), security.getUser().getPassword());
		try {
			manager.authenticate(token);
			fail("Expected Exception");
		}
		catch (AuthenticationException success) {
			// Expected
		}

		token = new UsernamePasswordAuthenticationToken("foo", "bar");
		assertNotNull(manager.authenticate(token));
	}

	@Test
	public void testSecurityEvaluationContextExtensionSupport() {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(AuthenticationManagerCustomizer.class,
				SecurityAutoConfiguration.class, ServerPropertiesAutoConfiguration.class);
		this.context.refresh();
		assertNotNull(this.context.getBean(SecurityEvaluationContextExtension.class));
	}

	@Test
	public void defaultFilterDispatcherTypes() {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		this.context.refresh();
		DelegatingFilterProxyRegistrationBean bean = this.context.getBean(
				"securityFilterChainRegistration",
				DelegatingFilterProxyRegistrationBean.class);
		@SuppressWarnings("unchecked")
		EnumSet<DispatcherType> dispatcherTypes = (EnumSet<DispatcherType>) ReflectionTestUtils
				.getField(bean, "dispatcherTypes");
		assertThat(dispatcherTypes, is(nullValue()));
	}

	@Test
	public void customFilterDispatcherTypes() {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				SecurityFilterAutoConfiguration.class,
				ServerPropertiesAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"security.filter-dispatcher-types:INCLUDE,ERROR");
		this.context.refresh();
		DelegatingFilterProxyRegistrationBean bean = this.context.getBean(
				"securityFilterChainRegistration",
				DelegatingFilterProxyRegistrationBean.class);
		@SuppressWarnings("unchecked")
		EnumSet<DispatcherType> dispatcherTypes = (EnumSet<DispatcherType>) ReflectionTestUtils
				.getField(bean, "dispatcherTypes");
		assertThat(dispatcherTypes,
				is(EnumSet.of(DispatcherType.INCLUDE, DispatcherType.ERROR)));
	}

	private static final class AuthenticationListener
			implements ApplicationListener<AbstractAuthenticationEvent> {

		private ApplicationEvent event;

		@Override
		public void onApplicationEvent(AbstractAuthenticationEvent event) {
			this.event = event;
		}
	}

	@Configuration
	@TestAutoConfigurationPackage(City.class)
	protected static class EntityConfiguration {

	}

	@Configuration
	protected static class TestAuthenticationConfiguration {

		private AuthenticationManager authenticationManager;

		@Bean
		public AuthenticationManager myAuthenticationManager() {
			this.authenticationManager = new AuthenticationManager() {

				@Override
				public Authentication authenticate(Authentication authentication)
						throws AuthenticationException {
					return new TestingAuthenticationToken("foo", "bar");
				}
			};
			return this.authenticationManager;
		}

	}

	@Configuration
	protected static class SecurityCustomizer extends WebSecurityConfigurerAdapter {

		@Autowired
		AuthenticationManager authenticationManager;

	}

	@Configuration
	protected static class WorkaroundSecurityCustomizer
			extends WebSecurityConfigurerAdapter {

		@Autowired
		private AuthenticationManagerBuilder builder;

		@SuppressWarnings("unused")
		private AuthenticationManager authenticationManager;

		@Override
		protected void configure(HttpSecurity http) throws Exception {
			this.authenticationManager = new AuthenticationManager() {
				@Override
				public Authentication authenticate(Authentication authentication)
						throws AuthenticationException {
					return WorkaroundSecurityCustomizer.this.builder.getOrBuild()
							.authenticate(authentication);
				}
			};
		}

	}

	@Configuration
	@Order(-1)
	protected static class AuthenticationManagerCustomizer
			extends GlobalAuthenticationConfigurerAdapter {

		@Override
		public void init(AuthenticationManagerBuilder auth) throws Exception {
			auth.inMemoryAuthentication().withUser("foo").password("bar").roles("USER");
		}

	}

	@Configuration
	protected static class UserDetailsSecurityCustomizer
			extends WebSecurityConfigurerAdapter {

		private UserDetailsService userDetails;

		@Override
		protected void configure(HttpSecurity http) throws Exception {
			this.userDetails = http.getSharedObject(UserDetailsService.class);
		}

		public UserDetailsService getUserDetails() {
			return this.userDetails;
		}

	}

	@Configuration
	@EnableWebSecurity
	static class WebSecurity extends WebSecurityConfigurerAdapter {

	}

}
