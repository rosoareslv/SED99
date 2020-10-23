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

package org.springframework.boot.endpoint.jmx;

/**
 * Describes the parameters of an operation on a JMX endpoint.
 *
 * @author Stephane Nicoll
 * @since 2.0.0
 */
public class JmxEndpointOperationParameterInfo {

	private final String name;

	private final Class<?> type;

	private final String description;

	public JmxEndpointOperationParameterInfo(String name, Class<?> type,
			String description) {
		this.name = name;
		this.type = type;
		this.description = description;
	}

	public String getName() {
		return this.name;
	}

	public Class<?> getType() {
		return this.type;
	}

	/**
	 * Return the description of the parameter or {@code null} if none is available.
	 * @return the description or {@code null}
	 */
	public String getDescription() {
		return this.description;
	}

}
