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

package org.springframework.boot.actuate.autoconfigure;

import java.net.InetAddress;

import javax.validation.constraints.NotNull;

import org.springframework.boot.autoconfigure.security.SecurityPrerequisite;
import org.springframework.boot.autoconfigure.security.SecurityProperties;
import org.springframework.boot.autoconfigure.web.ServerProperties;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.util.ClassUtils;
import org.springframework.util.StringUtils;

/**
 * Properties for the management server (e.g. port and path settings).
 *
 * @author Dave Syer
 * @author Stephane Nicoll
 * @see ServerProperties
 */
@ConfigurationProperties(prefix = "management", ignoreUnknownFields = true)
public class ManagementServerProperties implements SecurityPrerequisite {

	private static final String SECURITY_CHECK_CLASS = "org.springframework.security.config.http.SessionCreationPolicy";

	/**
	 * Order applied to the WebSecurityConfigurerAdapter that is used to configure basic
	 * authentication for management endpoints. If you want to add your own authentication
	 * for all or some of those endpoints the best thing to do is add your own
	 * WebSecurityConfigurerAdapter with lower order.
	 */
	public static final int BASIC_AUTH_ORDER = SecurityProperties.BASIC_AUTH_ORDER - 5;

	/**
	 * Order after the basic authentication access control provided automatically for the
	 * management endpoints. This is a useful place to put user-defined access rules if
	 * you want to override the default access rules.
	 */
	public static final int ACCESS_OVERRIDE_ORDER = ManagementServerProperties.BASIC_AUTH_ORDER
			- 1;

	/**
	 * Management endpoint HTTP port. Use the same port as the application by default.
	 */
	private Integer port;

	/**
	 * Network address that the management endpoints should bind to.
	 */
	private InetAddress address;

	/**
	 * Management endpoint context-path.
	 */
	@NotNull
	private String contextPath = "";

	/**
	 * Add the "X-Application-Context" HTTP header in each response.
	 */
	private boolean addApplicationContextHeader = true;

	private final Security security = maybeCreateSecurity();

	private Security maybeCreateSecurity() {
		if (ClassUtils.isPresent(SECURITY_CHECK_CLASS, null)) {
			return new Security();
		}
		return null;
	}

	/**
	 * Returns the management port or {@code null} if the
	 * {@link ServerProperties#getPort() server port} should be used.
	 * @return the port
	 * @see #setPort(Integer)
	 */
	public Integer getPort() {
		return this.port;
	}

	/**
	 * Sets the port of the management server, use {@code null} if the
	 * {@link ServerProperties#getPort() server port} should be used. To disable use 0.
	 * @param port the port
	 */
	public void setPort(Integer port) {
		this.port = port;
	}

	public InetAddress getAddress() {
		return this.address;
	}

	public void setAddress(InetAddress address) {
		this.address = address;
	}

	/**
	 * Return the context path with no trailing slash (i.e. the '/' root context is
	 * represented as the empty string).
	 * @return the context path (no trailing slash)
	 */
	public String getContextPath() {
		return this.contextPath;
	}

	public void setContextPath(String contextPath) {
		this.contextPath = cleanContextPath(contextPath);
	}

	private String cleanContextPath(String contextPath) {
		if (StringUtils.hasText(contextPath) && contextPath.endsWith("/")) {
			return contextPath.substring(0, contextPath.length() - 1);
		}
		return contextPath;
	}

	public Security getSecurity() {
		return this.security;
	}

	public boolean getAddApplicationContextHeader() {
		return this.addApplicationContextHeader;
	}

	public void setAddApplicationContextHeader(boolean addApplicationContextHeader) {
		this.addApplicationContextHeader = addApplicationContextHeader;
	}

	/**
	 * Security configuration.
	 */
	public static class Security {

		/**
		 * Enable security.
		 */
		private boolean enabled = true;

		/**
		 * Role required to access the management endpoint.
		 */
		private String role = "ADMIN";

		/**
		 * Session creating policy to use (always, never, if_required, stateless).
		 */
		private SessionCreationPolicy sessions = SessionCreationPolicy.STATELESS;

		public SessionCreationPolicy getSessions() {
			return this.sessions;
		}

		public void setSessions(SessionCreationPolicy sessions) {
			this.sessions = sessions;
		}

		public void setRole(String role) {
			this.role = role;
		}

		public String getRole() {
			return this.role;
		}

		public boolean isEnabled() {
			return this.enabled;
		}

		public void setEnabled(boolean enabled) {
			this.enabled = enabled;
		}

	}

}
