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

package org.springframework.boot.endpoint;

import java.util.Collection;

/**
 * Information describing an endpoint.
 *
 * @param <T> the type of the endpoint's operations
 * @author Andy Wilkinson
 * @since 2.0.0
 */
public class EndpointInfo<T extends Operation> {

	private final String id;

	private final boolean enabledByDefault;

	private final Collection<T> operations;

	/**
	 * Creates a new {@code EndpointInfo} describing an endpoint with the given {@code id}
	 * and {@code operations}.
	 * @param id the id of the endpoint
	 * @param enabledByDefault whether or not the endpoint is enabled by default
	 * @param operations the operations of the endpoint
	 */
	public EndpointInfo(String id, boolean enabledByDefault, Collection<T> operations) {
		this.id = id;
		this.enabledByDefault = enabledByDefault;
		this.operations = operations;
	}

	/**
	 * Returns the id of the endpoint.
	 * @return the id
	 */
	public String getId() {
		return this.id;
	}

	/**
	 * Returns whether or not this endpoint is enabled by default.
	 * @return {@code true} if it is enabled by default, otherwise {@code false}
	 */
	public boolean isEnabledByDefault() {
		return this.enabledByDefault;
	}

	/**
	 * Returns the operations of the endpoint.
	 * @return the operations
	 */
	public Collection<T> getOperations() {
		return this.operations;
	}

}
