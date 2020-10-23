/*
 * Copyright 2012-2019 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.autoconfigure.rsocket;

import org.junit.Test;

import org.springframework.boot.autoconfigure.AutoConfigurations;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.codec.CharSequenceEncoder;
import org.springframework.core.codec.StringDecoder;
import org.springframework.messaging.rsocket.MessageHandlerAcceptor;
import org.springframework.messaging.rsocket.RSocketStrategies;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tests for {@link RSocketMessagingAutoConfiguration}.
 *
 * @author Brian Clozel
 */
public class RSocketMessagingAutoConfigurationTests {

	private ApplicationContextRunner contextRunner = new ApplicationContextRunner()
			.withConfiguration(
					AutoConfigurations.of(RSocketMessagingAutoConfiguration.class))
			.withUserConfiguration(BaseConfiguration.class);

	@Test
	public void shouldCreateDefaultBeans() {
		this.contextRunner.run((context) -> assertThat(context)
				.getBeans(MessageHandlerAcceptor.class).hasSize(1));
	}

	@Test
	public void shouldFailOnMissingStrategies() {
		new ApplicationContextRunner()
				.withConfiguration(
						AutoConfigurations.of(RSocketMessagingAutoConfiguration.class))
				.run((context) -> {
					assertThat(context).hasFailed();
					assertThat(context.getStartupFailure().getMessage())
							.contains("No qualifying bean of type "
									+ "'org.springframework.messaging.rsocket.RSocketStrategies' available");
				});
	}

	@Test
	public void shouldUseCustomMessageHandlerAcceptor() {
		this.contextRunner.withUserConfiguration(CustomMessageHandlerAcceptor.class)
				.run((context) -> assertThat(context)
						.getBeanNames(MessageHandlerAcceptor.class)
						.containsOnly("customMessageHandlerAcceptor"));
	}

	@Configuration
	static class BaseConfiguration {

		@Bean
		public RSocketStrategies rSocketStrategies() {
			return RSocketStrategies.builder()
					.encoder(CharSequenceEncoder.textPlainOnly())
					.decoder(StringDecoder.textPlainOnly()).build();
		}

	}

	@Configuration
	static class CustomMessageHandlerAcceptor {

		@Bean
		public MessageHandlerAcceptor customMessageHandlerAcceptor() {
			MessageHandlerAcceptor acceptor = new MessageHandlerAcceptor();
			RSocketStrategies strategies = RSocketStrategies.builder()
					.encoder(CharSequenceEncoder.textPlainOnly())
					.decoder(StringDecoder.textPlainOnly()).build();
			acceptor.setRSocketStrategies(strategies);
			return acceptor;
		}

	}

}
