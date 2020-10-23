/*
 * Copyright 2012-2019 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.autoconfigure.transaction;

import java.util.List;
import java.util.Map;

import javax.sql.DataSource;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import org.springframework.boot.autoconfigure.jdbc.DataSourceAutoConfiguration;
import org.springframework.boot.autoconfigure.jdbc.DataSourceTransactionManagerAutoConfiguration;
import org.springframework.boot.jdbc.DataSourceBuilder;
import org.springframework.boot.test.util.TestPropertyValues;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.jdbc.datasource.DataSourceTransactionManager;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.annotation.EnableTransactionManagement;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.transaction.support.TransactionSynchronizationManager;
import org.springframework.transaction.support.TransactionTemplate;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;

/**
 * Tests for {@link TransactionAutoConfiguration}.
 *
 * @author Stephane Nicoll
 * @author Phillip Webb
 */
class TransactionAutoConfigurationTests {

	private AnnotationConfigApplicationContext context;

	@AfterEach
	void tearDown() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	void noTransactionManager() {
		load(EmptyConfiguration.class);
		assertThat(this.context.getBeansOfType(TransactionTemplate.class)).isEmpty();
	}

	@Test
	void singleTransactionManager() {
		load(new Class<?>[] { DataSourceAutoConfiguration.class, DataSourceTransactionManagerAutoConfiguration.class },
				"spring.datasource.initialization-mode:never");
		PlatformTransactionManager transactionManager = this.context.getBean(PlatformTransactionManager.class);
		TransactionTemplate transactionTemplate = this.context.getBean(TransactionTemplate.class);
		assertThat(transactionTemplate.getTransactionManager()).isSameAs(transactionManager);
	}

	@Test
	void severalTransactionManagers() {
		load(SeveralTransactionManagersConfiguration.class);
		assertThat(this.context.getBeansOfType(TransactionTemplate.class)).isEmpty();
	}

	@Test
	void customTransactionManager() {
		load(CustomTransactionManagerConfiguration.class);
		Map<String, TransactionTemplate> beans = this.context.getBeansOfType(TransactionTemplate.class);
		assertThat(beans).hasSize(1);
		assertThat(beans.containsKey("transactionTemplateFoo")).isTrue();
	}

	@Test
	void platformTransactionManagerCustomizers() {
		load(SeveralTransactionManagersConfiguration.class);
		TransactionManagerCustomizers customizers = this.context.getBean(TransactionManagerCustomizers.class);
		List<?> field = (List<?>) ReflectionTestUtils.getField(customizers, "customizers");
		assertThat(field).hasSize(1).first().isInstanceOf(TransactionProperties.class);
	}

	@Test
	void transactionNotManagedWithNoTransactionManager() {
		load(BaseConfiguration.class);
		assertThat(this.context.getBean(TransactionalService.class).isTransactionActive()).isFalse();
	}

	@Test
	void transactionManagerUsesCglibByDefault() {
		load(TransactionManagersConfiguration.class);
		assertThat(this.context.getBean(AnotherServiceImpl.class).isTransactionActive()).isTrue();
		assertThat(this.context.getBeansOfType(TransactionalServiceImpl.class)).hasSize(1);
	}

	@Test
	void transactionManagerCanBeConfiguredToJdkProxy() {
		load(TransactionManagersConfiguration.class, "spring.aop.proxy-target-class=false");
		assertThat(this.context.getBean(AnotherService.class).isTransactionActive()).isTrue();
		assertThat(this.context.getBeansOfType(AnotherServiceImpl.class)).hasSize(0);
		assertThat(this.context.getBeansOfType(TransactionalServiceImpl.class)).hasSize(0);
	}

	@Test
	void customEnableTransactionManagementTakesPrecedence() {
		load(new Class<?>[] { CustomTransactionManagementConfiguration.class, TransactionManagersConfiguration.class },
				"spring.aop.proxy-target-class=true");
		assertThat(this.context.getBean(AnotherService.class).isTransactionActive()).isTrue();
		assertThat(this.context.getBeansOfType(AnotherServiceImpl.class)).hasSize(0);
		assertThat(this.context.getBeansOfType(TransactionalServiceImpl.class)).hasSize(0);
	}

	private void load(Class<?> config, String... environment) {
		load(new Class<?>[] { config }, environment);
	}

	private void load(Class<?>[] configs, String... environment) {
		AnnotationConfigApplicationContext applicationContext = new AnnotationConfigApplicationContext();
		applicationContext.register(configs);
		applicationContext.register(TransactionAutoConfiguration.class);
		TestPropertyValues.of(environment).applyTo(applicationContext);
		applicationContext.refresh();
		this.context = applicationContext;
	}

	@Configuration(proxyBeanMethods = false)
	static class EmptyConfiguration {

	}

	@Configuration(proxyBeanMethods = false)
	static class SeveralTransactionManagersConfiguration {

		@Bean
		PlatformTransactionManager transactionManagerOne() {
			return mock(PlatformTransactionManager.class);
		}

		@Bean
		PlatformTransactionManager transactionManagerTwo() {
			return mock(PlatformTransactionManager.class);
		}

	}

	@Configuration(proxyBeanMethods = false)
	static class CustomTransactionManagerConfiguration {

		@Bean
		TransactionTemplate transactionTemplateFoo(PlatformTransactionManager transactionManager) {
			return new TransactionTemplate(transactionManager);
		}

		@Bean
		PlatformTransactionManager transactionManagerFoo() {
			return mock(PlatformTransactionManager.class);
		}

	}

	@Configuration(proxyBeanMethods = false)
	static class BaseConfiguration {

		@Bean
		TransactionalService transactionalService() {
			return new TransactionalServiceImpl();
		}

		@Bean
		AnotherServiceImpl anotherService() {
			return new AnotherServiceImpl();
		}

	}

	@Configuration(proxyBeanMethods = false)
	@Import(BaseConfiguration.class)
	static class TransactionManagersConfiguration {

		@Bean
		DataSourceTransactionManager transactionManager(DataSource dataSource) {
			return new DataSourceTransactionManager(dataSource);
		}

		@Bean
		DataSource dataSource() {
			return DataSourceBuilder.create().driverClassName("org.hsqldb.jdbc.JDBCDriver").url("jdbc:hsqldb:mem:tx")
					.username("sa").build();
		}

	}

	@Configuration(proxyBeanMethods = false)
	@EnableTransactionManagement(proxyTargetClass = false)
	static class CustomTransactionManagementConfiguration {

	}

	interface TransactionalService {

		@Transactional
		boolean isTransactionActive();

	}

	static class TransactionalServiceImpl implements TransactionalService {

		@Override
		public boolean isTransactionActive() {
			return TransactionSynchronizationManager.isActualTransactionActive();
		}

	}

	interface AnotherService {

		boolean isTransactionActive();

	}

	static class AnotherServiceImpl implements AnotherService {

		@Override
		@Transactional
		public boolean isTransactionActive() {
			return TransactionSynchronizationManager.isActualTransactionActive();
		}

	}

}
