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

package org.springframework.boot.autoconfigure.security;

import java.util.List;
import java.util.regex.Pattern;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.security.authentication.AuthenticationManager;
import org.springframework.security.authentication.AuthenticationProvider;
import org.springframework.security.config.annotation.ObjectPostProcessor;
import org.springframework.security.core.userdetails.User;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.provisioning.InMemoryUserDetailsManager;

/**
 * Configuration for a Spring Security in-memory {@link AuthenticationManager}. Adds an
 * {@link InMemoryUserDetailsManager} with a default user and generated password. This can
 * be disabled by providing a bean of type {@link AuthenticationManager},
 * {@link AuthenticationProvider} or {@link UserDetailsService}.
 *
 * @author Dave Syer
 * @author Rob Winch
 * @author Madhura Bhave
 */
@Configuration
@ConditionalOnBean(ObjectPostProcessor.class)
@ConditionalOnMissingBean({ AuthenticationManager.class, AuthenticationProvider.class,
		UserDetailsService.class })
public class AuthenticationManagerConfiguration {

	private static final String NOOP_PASSWORD_PREFIX = "{noop}";

	private static final Pattern PASSWORD_ALGORITHM_PATTERN = Pattern
			.compile("^\\{.+}.*$");

	private static final Log logger = LogFactory
			.getLog(AuthenticationManagerConfiguration.class);

	@Bean
	public InMemoryUserDetailsManager inMemoryUserDetailsManager(
			SecurityProperties properties,
			ObjectProvider<PasswordEncoder> passwordEncoder) throws Exception {
		SecurityProperties.User user = properties.getUser();
		List<String> roles = user.getRoles();
		return new InMemoryUserDetailsManager(
				User.withUsername(user.getName())
						.password(getOrDeducePassword(user,
								passwordEncoder.getIfAvailable()))
				.roles(roles.toArray(new String[roles.size()])).build());
	}

	public String getOrDeducePassword(SecurityProperties.User user,
			PasswordEncoder encoder) {
		String password = user.getPassword();
		if (user.isPasswordGenerated()) {
			logger.info(String.format("%n%nUsing generated security password: %s%n",
					user.getPassword()));
		}
		if (encoder != null || PASSWORD_ALGORITHM_PATTERN.matcher(password).matches()) {
			return password;
		}
		return NOOP_PASSWORD_PREFIX + password;
	}

}
