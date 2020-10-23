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

package org.springframework.boot.actuate.endpoint;

import java.util.Map;

import org.junit.Test;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tests for {@link ConfigurationPropertiesReportEndpoint} when used with bean methods.
 *
 * @author Dave Syer
 * @author Andy Wilkinson
 */
public class ConfigurationPropertiesReportEndpointMethodAnnotationsTests {

	@Test
	@SuppressWarnings("unchecked")
	public void testNaming() {
		ApplicationContextRunner contextRunner = new ApplicationContextRunner()
				.withUserConfiguration(Config.class)
				.withPropertyValues("other.name:foo", "first.name:bar");
		contextRunner.run((context) -> {
			ConfigurationPropertiesReportEndpoint endpoint = context
					.getBean(ConfigurationPropertiesReportEndpoint.class);
			Map<String, Object> properties = endpoint.configurationProperties();
			Map<String, Object> nestedProperties = (Map<String, Object>) properties
					.get("other");
			assertThat(nestedProperties).isNotNull();
			assertThat(nestedProperties.get("prefix")).isEqualTo("other");
			assertThat(nestedProperties.get("properties")).isNotNull();
		});
	}

	@Test
	@SuppressWarnings("unchecked")
	public void prefixFromBeanMethodConfigurationPropertiesCanOverridePrefixOnClass() {
		ApplicationContextRunner contextRunner = new ApplicationContextRunner()
				.withUserConfiguration(OverriddenPrefix.class)
				.withPropertyValues("other.name:foo");
		contextRunner.run((context) -> {
			ConfigurationPropertiesReportEndpoint endpoint = context
					.getBean(ConfigurationPropertiesReportEndpoint.class);
			Map<String, Object> properties = endpoint.configurationProperties();
			Map<String, Object> nestedProperties = (Map<String, Object>) properties
					.get("bar");
			assertThat(nestedProperties).isNotNull();
			assertThat(nestedProperties.get("prefix")).isEqualTo("other");
			assertThat(nestedProperties.get("properties")).isNotNull();
		});
	}

	@Configuration
	@EnableConfigurationProperties
	public static class Config {

		@Bean
		public ConfigurationPropertiesReportEndpoint endpoint() {
			return new ConfigurationPropertiesReportEndpoint();
		}

		@Bean
		@ConfigurationProperties(prefix = "first")
		public Foo foo() {
			return new Foo();
		}

		@Bean
		@ConfigurationProperties(prefix = "other")
		public Foo other() {
			return new Foo();
		}

	}

	@Configuration
	@EnableConfigurationProperties
	public static class OverriddenPrefix {

		@Bean
		public ConfigurationPropertiesReportEndpoint endpoint() {
			return new ConfigurationPropertiesReportEndpoint();
		}

		@Bean
		@ConfigurationProperties(prefix = "other")
		public Bar bar() {
			return new Bar();
		}

	}

	public static class Foo {

		private String name = "654321";

		public String getName() {
			return this.name;
		}

		public void setName(String name) {
			this.name = name;
		}

	}

	@ConfigurationProperties(prefix = "test")
	public static class Bar {

		private String name = "654321";

		public String getName() {
			return this.name;
		}

		public void setName(String name) {
			this.name = name;
		}

	}

}
