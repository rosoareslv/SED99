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

package org.springframework.boot.autoconfigure.web.reactive;

import org.springframework.beans.BeansException;
import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.BeanFactoryAware;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.beans.factory.support.BeanDefinitionRegistry;
import org.springframework.beans.factory.support.RootBeanDefinition;
import org.springframework.boot.autoconfigure.AutoConfigureOrder;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnWebApplication;
import org.springframework.boot.autoconfigure.web.ServerProperties;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.boot.web.server.WebServerFactoryCustomizerBeanPostProcessor;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.ImportBeanDefinitionRegistrar;
import org.springframework.core.Ordered;
import org.springframework.core.type.AnnotationMetadata;
import org.springframework.util.ObjectUtils;

/**
 * {@link EnableAutoConfiguration Auto-configuration} for a reactive web server.
 *
 * @author Brian Clozel
 * @since 2.0.0
 */
@AutoConfigureOrder(Ordered.HIGHEST_PRECEDENCE)
@Configuration
@ConditionalOnWebApplication(type = ConditionalOnWebApplication.Type.REACTIVE)
@EnableConfigurationProperties(ServerProperties.class)
@Import({ ReactiveWebServerAutoConfiguration.BeanPostProcessorsRegistrar.class,
		ReactiveWebServerConfiguration.TomcatAutoConfiguration.class,
		ReactiveWebServerConfiguration.JettyAutoConfiguration.class,
		ReactiveWebServerConfiguration.ReactorNettyAutoConfiguration.class,
		ReactiveWebServerConfiguration.UndertowAutoConfiguration.class })
public class ReactiveWebServerAutoConfiguration {

	@ConditionalOnMissingBean
	@Bean
	public DefaultReactiveWebServerCustomizer defaultReactiveWebServerCustomizer(
			ServerProperties serverProperties) {
		return new DefaultReactiveWebServerCustomizer(serverProperties);
	}

	/**
	 * Registers a {@link WebServerFactoryCustomizerBeanPostProcessor}. Registered via
	 * {@link ImportBeanDefinitionRegistrar} for early registration.
	 */
	public static class BeanPostProcessorsRegistrar
			implements ImportBeanDefinitionRegistrar, BeanFactoryAware {

		private ConfigurableListableBeanFactory beanFactory;

		@Override
		public void setBeanFactory(BeanFactory beanFactory) throws BeansException {
			if (beanFactory instanceof ConfigurableListableBeanFactory) {
				this.beanFactory = (ConfigurableListableBeanFactory) beanFactory;
			}
		}

		@Override
		public void registerBeanDefinitions(AnnotationMetadata importingClassMetadata,
				BeanDefinitionRegistry registry) {
			if (this.beanFactory == null) {
				return;
			}
			if (ObjectUtils.isEmpty(this.beanFactory.getBeanNamesForType(
					WebServerFactoryCustomizerBeanPostProcessor.class, true, false))) {
				RootBeanDefinition beanDefinition = new RootBeanDefinition(
						WebServerFactoryCustomizerBeanPostProcessor.class);
				beanDefinition.setSynthetic(true);
				registry.registerBeanDefinition(
						"webServerFactoryCustomizerBeanPostProcessor", beanDefinition);

			}
		}

	}

}
