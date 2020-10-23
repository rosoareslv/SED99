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

import io.micrometer.core.instrument.binder.MeterBinder;
import io.micrometer.core.instrument.binder.jvm.JvmGcMetrics;
import io.micrometer.core.instrument.binder.jvm.JvmMemoryMetrics;
import io.micrometer.core.instrument.binder.jvm.JvmThreadMetrics;
import io.micrometer.core.instrument.binder.logging.LogbackMetrics;
import io.micrometer.core.instrument.binder.system.ProcessorMetrics;
import io.micrometer.core.instrument.binder.system.UptimeMetrics;

import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * Configuration for various {@link MeterBinder MeterBinders}.
 *
 * @author Jon Schneider
 */
@Configuration
class MeterBindersConfiguration {

	@Bean
	@ConditionalOnProperty(value = "management.metrics.binders.jvm.enabled", matchIfMissing = true)
	@ConditionalOnMissingBean
	public JvmGcMetrics jvmGcMetrics() {
		return new JvmGcMetrics();
	}

	@Bean
	@ConditionalOnProperty(value = "management.metrics.binders.jvm.enabled", matchIfMissing = true)
	@ConditionalOnMissingBean
	public JvmMemoryMetrics jvmMemoryMetrics() {
		return new JvmMemoryMetrics();
	}

	@Bean
	@ConditionalOnProperty(value = "management.metrics.binders.jvm.enabled", matchIfMissing = true)
	@ConditionalOnMissingBean
	public JvmThreadMetrics jvmThreadMetrics() {
		return new JvmThreadMetrics();
	}

	@Bean
	@ConditionalOnMissingBean(LogbackMetrics.class)
	@ConditionalOnProperty(value = "management.metrics.binders.logback.enabled", matchIfMissing = true)
	@ConditionalOnClass(name = "ch.qos.logback.classic.Logger")
	public LogbackMetrics logbackMetrics() {
		return new LogbackMetrics();
	}

	@Bean
	@ConditionalOnProperty(value = "management.metrics.binders.uptime.enabled", matchIfMissing = true)
	@ConditionalOnMissingBean
	public UptimeMetrics uptimeMetrics() {
		return new UptimeMetrics();
	}

	@Bean
	@ConditionalOnProperty(value = "management.metrics.binders.processor.enabled", matchIfMissing = true)
	@ConditionalOnMissingBean
	public ProcessorMetrics processorMetrics() {
		return new ProcessorMetrics();
	}

}
