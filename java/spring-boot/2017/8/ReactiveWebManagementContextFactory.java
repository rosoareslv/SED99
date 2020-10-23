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

package org.springframework.boot.actuate.autoconfigure;

import java.lang.reflect.Modifier;

import org.springframework.beans.FatalBeanException;
import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.beans.factory.support.BeanDefinitionRegistry;
import org.springframework.beans.factory.support.RootBeanDefinition;
import org.springframework.boot.autoconfigure.web.reactive.ReactiveWebServerAutoConfiguration;
import org.springframework.boot.web.reactive.context.GenericReactiveWebApplicationContext;
import org.springframework.boot.web.reactive.context.ReactiveWebServerApplicationContext;
import org.springframework.boot.web.reactive.server.ReactiveWebServerFactory;
import org.springframework.context.ApplicationContext;
import org.springframework.context.ConfigurableApplicationContext;

/**
 * A {@link ManagementContextFactory} for WebFlux-based web applications.
 *
 * @author Andy Wilkinson
 */
class ReactiveWebManagementContextFactory implements ManagementContextFactory {

	@Override
	public ConfigurableApplicationContext createManagementContext(
			ApplicationContext parent, Class<?>... configClasses) {
		ReactiveWebServerApplicationContext child = new ReactiveWebServerApplicationContext();
		child.setParent(parent);
		child.register(configClasses);
		child.register(ReactiveWebServerAutoConfiguration.class);
		registerReactiveWebServerFactory(parent, child);
		return child;
	}

	private void registerReactiveWebServerFactory(ApplicationContext parent,
			GenericReactiveWebApplicationContext childContext) {
		try {
			ConfigurableListableBeanFactory beanFactory = childContext.getBeanFactory();
			if (beanFactory instanceof BeanDefinitionRegistry) {
				BeanDefinitionRegistry registry = (BeanDefinitionRegistry) beanFactory;
				registry.registerBeanDefinition("ReactiveWebServerFactory",
						new RootBeanDefinition(
								determineReactiveWebServerFactoryClass(parent)));
			}
		}
		catch (NoSuchBeanDefinitionException ex) {
			// Ignore and assume auto-configuration
		}
	}

	private Class<?> determineReactiveWebServerFactoryClass(ApplicationContext parent)
			throws NoSuchBeanDefinitionException {
		Class<?> factoryClass = parent.getBean(ReactiveWebServerFactory.class).getClass();
		if (cannotBeInstantiated(factoryClass)) {
			throw new FatalBeanException("ReactiveWebServerFactory implementation "
					+ factoryClass.getName() + " cannot be instantiated. "
					+ "To allow a separate management port to be used, a top-level class "
					+ "or static inner class should be used instead");
		}
		return factoryClass;
	}

	private boolean cannotBeInstantiated(Class<?> clazz) {
		return clazz.isLocalClass()
				|| (clazz.isMemberClass() && !Modifier.isStatic(clazz.getModifiers()))
				|| clazz.isAnonymousClass();
	}

}
