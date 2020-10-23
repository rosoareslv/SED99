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

package org.springframework.boot.actuate.autoconfigure.metrics;

import io.micrometer.core.instrument.MeterRegistry;
import org.junit.Test;

import org.springframework.boot.autoconfigure.AutoConfigurations;
import org.springframework.boot.context.annotation.UserConfigurations;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.context.annotation.Bean;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tests for applying {@link MeterRegistryConfigurer MeterRegistryConfigurers}.
 *
 * @author Jon Schneider
 * @author Andy Wilkinson
 */
public class MeterRegistryConfigurerTests {

	@Test
	public void commonTagsAreAppliedToAutoConfiguredBinders() {
		new ApplicationContextRunner()
				.withConfiguration(AutoConfigurations.of(MetricsAutoConfiguration.class))
				.withConfiguration(
						UserConfigurations.of(MeterRegistryConfigurerConfiguration.class))
				.withPropertyValues("metrics.use-global-registry=false")
				.run((context) -> assertThat(context.getBean(MeterRegistry.class)
						.find("jvm.memory.used").tags("region", "us-east-1").gauge())
								.isPresent());
	}

	@Test
	public void commonTagsAreAppliedBeforeRegistryIsInjectableElsewhere() {
		new ApplicationContextRunner()
				.withConfiguration(AutoConfigurations.of(MetricsAutoConfiguration.class))
				.withConfiguration(
						UserConfigurations.of(MeterRegistryConfigurerConfiguration.class))
				.withPropertyValues("metrics.use-global-registry=false")
				.run((context) -> assertThat(context.getBean(MeterRegistry.class)
						.find("my.thing").tags("region", "us-east-1").gauge())
								.isPresent());
	}

	static class MeterRegistryConfigurerConfiguration {

		@Bean
		public MeterRegistryConfigurer registryConfigurer() {
			return (registry) -> registry.config().commonTags("region", "us-east-1");
		}

		@Bean
		public MyThing myThing(MeterRegistry registry) {
			registry.gauge("my.thing", 0);
			return new MyThing();
		}

		class MyThing {
		}

	}

}
