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

package org.springframework.boot.autoconfigure.flyway;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import javax.sql.DataSource;

import org.flywaydb.core.Flyway;
import org.flywaydb.core.api.MigrationVersion;
import org.hibernate.engine.transaction.jta.platform.internal.NoJtaPlatform;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;

import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.autoconfigure.PropertyPlaceholderAutoConfiguration;
import org.springframework.boot.autoconfigure.jdbc.DataSourceBuilder;
import org.springframework.boot.autoconfigure.jdbc.EmbeddedDataSourceConfiguration;
import org.springframework.boot.orm.jpa.EntityManagerFactoryBuilder;
import org.springframework.boot.test.EnvironmentTestUtils;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.Ordered;
import org.springframework.orm.jpa.LocalContainerEntityManagerFactoryBean;
import org.springframework.orm.jpa.vendor.HibernateJpaVendorAdapter;
import org.springframework.stereotype.Component;

import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;

/**
 * Tests for {@link FlywayAutoConfiguration}.
 *
 * @author Dave Syer
 * @author Phillip Webb
 * @author Andy Wilkinson
 * @author Vedran Pavic
 */
public class FlywayAutoConfigurationTests {

	@Rule
	public ExpectedException thrown = ExpectedException.none();

	private AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext();

	@Before
	public void init() {
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.datasource.name:flywaytest");
	}

	@After
	public void close() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	public void noDataSource() throws Exception {
		registerAndRefresh(FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		assertEquals(0, this.context.getBeanNamesForType(Flyway.class).length);
	}

	@Test
	public void createDataSource() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context,
				"flyway.url:jdbc:hsqldb:mem:flywaytest", "flyway.user:sa");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertNotNull(flyway.getDataSource());
	}

	@Test
	public void flywayDataSource() throws Exception {
		registerAndRefresh(FlywayDataSourceConfiguration.class,
				EmbeddedDataSourceConfiguration.class, FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertNotNull(flyway.getDataSource());
	}

	@Test
	public void defaultFlyway() throws Exception {
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertEquals("[classpath:db/migration]",
				Arrays.asList(flyway.getLocations()).toString());
	}

	@Test
	public void overrideLocations() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context,
				"flyway.locations:classpath:db/changelog,classpath:db/migration");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertEquals("[classpath:db/changelog, classpath:db/migration]",
				Arrays.asList(flyway.getLocations()).toString());
	}

	@Test
	public void overrideSchemas() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context, "flyway.schemas:public");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertEquals("[public]", Arrays.asList(flyway.getSchemas()).toString());
	}

	@Test
	public void changeLogDoesNotExist() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context,
				"flyway.locations:file:no-such-dir");
		this.thrown.expect(BeanCreationException.class);
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
	}

	@Test
	public void checkLocationsAllMissing() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context,
				"flyway.locations:classpath:db/missing1,classpath:db/migration2",
				"flyway.check-location:true");
		this.thrown.expect(BeanCreationException.class);
		this.thrown.expectMessage("Cannot find migrations location in");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
	}

	@Test
	public void checkLocationsAllExist() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context,
				"flyway.locations:classpath:db/changelog,classpath:db/migration",
				"flyway.check-location:true");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
	}

	@Test
	public void customFlywayMigrationStrategy() throws Exception {
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class, PropertyPlaceholderAutoConfiguration.class,
				MockFlywayMigrationStrategy.class);
		assertNotNull(this.context.getBean(Flyway.class));
		this.context.getBean(MockFlywayMigrationStrategy.class).assertCalled();
	}

	@Test
	public void customFlywayMigrationInitializer() throws Exception {
		registerAndRefresh(CustomFlywayMigrationInitializer.class,
				EmbeddedDataSourceConfiguration.class, FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		assertNotNull(this.context.getBean(Flyway.class));
		FlywayMigrationInitializer initializer = this.context
				.getBean(FlywayMigrationInitializer.class);
		assertThat(initializer.getOrder(), equalTo(Ordered.HIGHEST_PRECEDENCE));
	}

	@Test
	public void customFlywayWithJpa() throws Exception {
		registerAndRefresh(CustomFlywayWithJpaConfiguration.class,
				EmbeddedDataSourceConfiguration.class, FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
	}

	@Test
	public void overrideBaselineVersion() throws Exception {
		EnvironmentTestUtils.addEnvironment(this.context, "flyway.baseline-version=0");
		registerAndRefresh(EmbeddedDataSourceConfiguration.class,
				FlywayAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class);
		Flyway flyway = this.context.getBean(Flyway.class);
		assertThat(flyway.getBaselineVersion(),
				equalTo(MigrationVersion.fromVersion("0")));
	}

	private void registerAndRefresh(Class<?>... annotatedClasses) {
		this.context.register(annotatedClasses);
		this.context.refresh();

	}

	@Configuration
	protected static class FlywayDataSourceConfiguration {

		@FlywayDataSource
		@Bean
		public DataSource flywayDataSource() {
			return DataSourceBuilder.create().url("jdbc:hsqldb:mem:flywaytest")
					.username("sa").build();
		}

	}

	@Configuration
	protected static class CustomFlywayMigrationInitializer {

		@Bean
		public FlywayMigrationInitializer flywayMigrationInitializer(Flyway flyway) {
			FlywayMigrationInitializer initializer = new FlywayMigrationInitializer(
					flyway);
			initializer.setOrder(Ordered.HIGHEST_PRECEDENCE);
			return initializer;
		}
	}

	@Configuration
	protected static class CustomFlywayWithJpaConfiguration {

		@Autowired
		private DataSource dataSource;

		@Bean
		public Flyway flyway() {
			return new Flyway();
		}

		@Bean
		public LocalContainerEntityManagerFactoryBean entityManagerFactoryBean() {
			Map<String, Object> properties = new HashMap<String, Object>();
			properties.put("configured", "manually");
			properties.put("hibernate.transaction.jta.platform", NoJtaPlatform.INSTANCE);
			return new EntityManagerFactoryBuilder(new HibernateJpaVendorAdapter(),
					properties, null).dataSource(this.dataSource).build();
		}

	}

	@Component
	protected static class MockFlywayMigrationStrategy
			implements FlywayMigrationStrategy {

		private boolean called = false;

		@Override
		public void migrate(Flyway flyway) {
			this.called = true;
		}

		public void assertCalled() {
			assertThat(this.called, equalTo(true));
		}
	}

}
