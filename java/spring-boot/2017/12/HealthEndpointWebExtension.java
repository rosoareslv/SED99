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

package org.springframework.boot.actuate.health;

import org.springframework.boot.actuate.endpoint.annotation.ReadOperation;
import org.springframework.boot.actuate.endpoint.web.WebEndpointResponse;
import org.springframework.boot.actuate.endpoint.web.annotation.EndpointWebExtension;

/**
 * {@link EndpointWebExtension} for the {@link HealthEndpoint}.
 *
 * @author Christian Dupuis
 * @author Dave Syer
 * @author Andy Wilkinson
 * @author Phillip Webb
 * @author Eddú Meléndez
 * @author Madhura Bhave
 * @since 2.0.0
 */
@EndpointWebExtension(endpoint = HealthEndpoint.class)
public class HealthEndpointWebExtension {

	private final HealthIndicator delegate;

	private final HealthStatusHttpMapper statusHttpMapper;

	private final boolean showDetails;

	public HealthEndpointWebExtension(HealthIndicator delegate,
			HealthStatusHttpMapper statusHttpMapper, boolean showDetails) {
		this.delegate = delegate;
		this.statusHttpMapper = statusHttpMapper;
		this.showDetails = showDetails;
	}

	@ReadOperation
	public WebEndpointResponse<Health> getHealth() {
		return getHealth(this.showDetails);
	}

	public WebEndpointResponse<Health> getHealth(boolean showDetails) {
		Health health = this.delegate.health();
		Integer status = this.statusHttpMapper.mapStatus(health.getStatus());
		if (!showDetails) {
			health = Health.status(health.getStatus()).build();
		}
		return new WebEndpointResponse<>(health, status);
	}

}
