/*
 * Copyright 2012-2014 the original author or authors.
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

package org.springframework.boot.autoconfigure.jms;

import org.springframework.boot.context.properties.ConfigurationProperties;

/**
 * Configuration properties for JMS.
 *
 * @author Greg Turnquist
 * @author Phillip Webb
 */
@ConfigurationProperties(prefix = "spring.jms")
public class JmsProperties {

	/**
	 * Specify if the default destination type is topic.
	 */
	private boolean pubSubDomain = false;

	/**
	 * Connection factory JNDI name. When set, takes precedence to others connection
	 * factory auto-configurations.
	 */
	private String jndiName;

	public boolean isPubSubDomain() {
		return this.pubSubDomain;
	}

	public void setPubSubDomain(boolean pubSubDomain) {
		this.pubSubDomain = pubSubDomain;
	}

	public String getJndiName() {
		return this.jndiName;
	}

	public void setJndiName(String jndiName) {
		this.jndiName = jndiName;
	}

}
