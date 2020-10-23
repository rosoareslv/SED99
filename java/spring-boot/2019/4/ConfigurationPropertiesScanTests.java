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

package org.springframework.boot.context.properties;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mockito;

import org.springframework.boot.context.properties.scan.valid.a.AScanConfiguration;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.core.io.ByteArrayResource;
import org.springframework.core.io.DefaultResourceLoader;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.BDDMockito.given;
import static org.mockito.BDDMockito.willCallRealMethod;

/**
 * Integration tests for {@link ConfigurationPropertiesScan}.
 *
 * @author Madhura Bhave
 */
public class ConfigurationPropertiesScanTests {

	private AnnotationConfigApplicationContext context;

	@Before
	public void setup() {
		this.context = new AnnotationConfigApplicationContext();
	}

	@After
	public void teardown() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	public void scanImportBeanRegistrarShouldBeEnvironmentAware() {
		this.context.getEnvironment().addActiveProfile("test");
		load(TestConfiguration.class);
		assertThat(this.context.containsBean(
				"profile-org.springframework.boot.context.properties.scan.valid.a.AScanConfiguration$MyProfileProperties"))
						.isTrue();
	}

	@Test
	public void scanImportBeanRegistrarShouldBeResourceLoaderAware() {
		DefaultResourceLoader resourceLoader = Mockito.mock(DefaultResourceLoader.class);
		this.context.setResourceLoader(resourceLoader);
		willCallRealMethod().given(resourceLoader).getClassLoader();
		given(resourceLoader.getResource("test"))
				.willReturn(new ByteArrayResource("test".getBytes()));
		load(TestConfiguration.class);
		assertThat(this.context.containsBean(
				"resource-org.springframework.boot.context.properties.scan.valid.a.AScanConfiguration$MyResourceProperties"))
						.isTrue();
	}

	private void load(Class<?>... classes) {
		this.context.register(classes);
		this.context.refresh();
	}

	@ConfigurationPropertiesScan(basePackageClasses = AScanConfiguration.class)
	static class TestConfiguration {

	}

}
