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

package org.springframework.boot.autoconfigure.amqp;

import java.util.LinkedHashSet;
import java.util.Set;

import org.springframework.amqp.core.AcknowledgeMode;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.util.StringUtils;

/**
 * Configuration properties for Rabbit.
 *
 * @author Greg Turnquist
 * @author Dave Syer
 * @author Stephane Nicoll
 * @author Andy Wilkinson
 * @author Josh Thornhill
 */
@ConfigurationProperties(prefix = "spring.rabbitmq")
public class RabbitProperties {

	/**
	 * RabbitMQ host.
	 */
	private String host = "localhost";

	/**
	 * RabbitMQ port.
	 */
	private int port = 5672;

	/**
	 * Login user to authenticate to the broker.
	 */
	private String username;

	/**
	 * Login to authenticate against the broker.
	 */
	private String password;

	/**
	 * SSL configuration.
	 */
	private final Ssl ssl = new Ssl();

	/**
	 * Virtual host to use when connecting to the broker.
	 */
	private String virtualHost;

	/**
	 * Comma-separated list of addresses to which the client should connect to.
	 */
	private String addresses;

	/**
	 * Requested heartbeat timeout, in seconds; zero for none.
	 */
	private Integer requestedHeartbeat;

	/**
	 * Listener container configuration.
	 */
	private final Listener listener = new Listener();

	public String getHost() {
		if (this.addresses == null) {
			return this.host;
		}
		String[] hosts = StringUtils.delimitedListToStringArray(this.addresses, ":");
		if (hosts.length == 2) {
			return hosts[0];
		}
		return null;
	}

	public void setHost(String host) {
		this.host = host;
	}

	public int getPort() {
		if (this.addresses == null) {
			return this.port;
		}
		String[] hosts = StringUtils.delimitedListToStringArray(this.addresses, ":");
		if (hosts.length >= 2) {
			return Integer
					.valueOf(StringUtils.commaDelimitedListToStringArray(hosts[1])[0]);
		}
		return this.port;
	}

	public void setAddresses(String addresses) {
		this.addresses = parseAddresses(addresses);
	}

	public String getAddresses() {
		return (this.addresses == null ? this.host + ":" + this.port : this.addresses);
	}

	private String parseAddresses(String addresses) {
		Set<String> result = new LinkedHashSet<String>();
		for (String address : StringUtils.commaDelimitedListToStringArray(addresses)) {
			address = address.trim();
			if (address.startsWith("amqp://")) {
				address = address.substring("amqp://".length());
			}
			if (address.contains("@")) {
				String[] split = StringUtils.split(address, "@");
				String creds = split[0];
				address = split[1];
				split = StringUtils.split(creds, ":");
				this.username = split[0];
				if (split.length > 0) {
					this.password = split[1];
				}
			}
			int index = address.indexOf("/");
			if (index >= 0 && index < address.length()) {
				setVirtualHost(address.substring(index + 1));
				address = address.substring(0, index);
			}
			if (!address.contains(":")) {
				address = address + ":" + this.port;
			}
			result.add(address);
		}
		return (result.isEmpty() ? null
				: StringUtils.collectionToCommaDelimitedString(result));
	}

	public void setPort(int port) {
		this.port = port;
	}

	public String getUsername() {
		return this.username;
	}

	public void setUsername(String username) {
		this.username = username;
	}

	public String getPassword() {
		return this.password;
	}

	public void setPassword(String password) {
		this.password = password;
	}

	public Ssl getSsl() {
		return this.ssl;
	}

	public String getVirtualHost() {
		return this.virtualHost;
	}

	public void setVirtualHost(String virtualHost) {
		this.virtualHost = ("".equals(virtualHost) ? "/" : virtualHost);
	}

	public Integer getRequestedHeartbeat() {
		return this.requestedHeartbeat;
	}

	public void setRequestedHeartbeat(Integer requestedHeartbeat) {
		this.requestedHeartbeat = requestedHeartbeat;
	}

	public Listener getListener() {
		return this.listener;
	}

	public static class Ssl {

		/**
		 * Enable SSL support.
		 */
		private boolean enabled;

		/**
		 * Path to the key store that holds the SSL certificate.
		 */
		private String keyStore;

		/**
		 * Password used to access the key store.
		 */
		private String keyStorePassword;

		/**
		 * Trust store that holds SSL certificates.
		 */
		private String trustStore;

		/**
		 * Password used to access the trust store.
		 */
		private String trustStorePassword;

		public boolean isEnabled() {
			return this.enabled;
		}

		public void setEnabled(boolean enabled) {
			this.enabled = enabled;
		}

		public String getKeyStore() {
			return this.keyStore;
		}

		public void setKeyStore(String keyStore) {
			this.keyStore = keyStore;
		}

		public String getKeyStorePassword() {
			return this.keyStorePassword;
		}

		public void setKeyStorePassword(String keyStorePassword) {
			this.keyStorePassword = keyStorePassword;
		}

		public String getTrustStore() {
			return this.trustStore;
		}

		public void setTrustStore(String trustStore) {
			this.trustStore = trustStore;
		}

		public String getTrustStorePassword() {
			return this.trustStorePassword;
		}

		public void setTrustStorePassword(String trustStorePassword) {
			this.trustStorePassword = trustStorePassword;
		}

	}

	public static class Listener {

		/**
		 * Start the container automatically on startup.
		 */
		private boolean autoStartup = true;

		/**
		 * Acknowledge mode of container.
		 */
		private AcknowledgeMode acknowledgeMode;

		/**
		 * Minimum number of consumers.
		 */
		private Integer concurrency;

		/**
		 * Maximum number of consumers.
		 */
		private Integer maxConcurrency;

		/**
		 * Number of messages to be handled in a single request. It should be greater than
		 * or equal to the transaction size (if used).
		 */
		private Integer prefetch;

		/**
		 * Number of messages to be processed in a transaction. For best results it should
		 * be less than or equal to the prefetch count.
		 */
		private Integer transactionSize;

		public boolean isAutoStartup() {
			return this.autoStartup;
		}

		public void setAutoStartup(boolean autoStartup) {
			this.autoStartup = autoStartup;
		}

		public AcknowledgeMode getAcknowledgeMode() {
			return this.acknowledgeMode;
		}

		public void setAcknowledgeMode(AcknowledgeMode acknowledgeMode) {
			this.acknowledgeMode = acknowledgeMode;
		}

		public Integer getConcurrency() {
			return this.concurrency;
		}

		public void setConcurrency(Integer concurrency) {
			this.concurrency = concurrency;
		}

		public Integer getMaxConcurrency() {
			return this.maxConcurrency;
		}

		public void setMaxConcurrency(Integer maxConcurrency) {
			this.maxConcurrency = maxConcurrency;
		}

		public Integer getPrefetch() {
			return this.prefetch;
		}

		public void setPrefetch(Integer prefetch) {
			this.prefetch = prefetch;
		}

		public Integer getTransactionSize() {
			return this.transactionSize;
		}

		public void setTransactionSize(Integer transactionSize) {
			this.transactionSize = transactionSize;
		}
	}

}
