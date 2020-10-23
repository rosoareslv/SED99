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

package org.springframework.boot.actuate.autoconfigure.endpoint.infrastructure;

import java.util.function.Function;

import org.springframework.boot.endpoint.CachingConfiguration;
import org.springframework.core.env.Environment;

/**
 * A {@link CachingConfiguration} factory that use the {@link Environment} to extract the
 * caching settings of each endpoint.
 *
 * @author Stephane Nicoll
 */
class CachingConfigurationFactory implements Function<String, CachingConfiguration> {

	private final Environment environment;

	/**
	 * Create a new instance with the {@link Environment} to use.
	 * @param environment the environment
	 */
	CachingConfigurationFactory(Environment environment) {
		this.environment = environment;
	}

	@Override
	public CachingConfiguration apply(String endpointId) {
		String key = String.format("endpoints.%s.cache.time-to-live", endpointId);
		Long ttl = this.environment.getProperty(key, Long.class, 0L);
		return new CachingConfiguration(ttl);
	}
}
