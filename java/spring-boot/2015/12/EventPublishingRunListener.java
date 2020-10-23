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

package org.springframework.boot.context.event;

import org.springframework.beans.factory.BeanFactoryAware;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.SpringApplicationRunListener;
import org.springframework.context.ApplicationContextAware;
import org.springframework.context.ApplicationListener;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.event.ApplicationEventMulticaster;
import org.springframework.context.event.SimpleApplicationEventMulticaster;
import org.springframework.context.support.AbstractApplicationContext;
import org.springframework.core.Ordered;
import org.springframework.core.env.ConfigurableEnvironment;

/**
 * {@link SpringApplicationRunListener} to publish {@link SpringApplicationEvent}s.
 *
 * @author Phillip Webb
 */
public class EventPublishingRunListener implements SpringApplicationRunListener, Ordered {

	private final ApplicationEventMulticaster multicaster;

	private SpringApplication application;

	private String[] args;

	public EventPublishingRunListener(SpringApplication application, String[] args) {
		this.application = application;
		this.args = args;
		this.multicaster = new SimpleApplicationEventMulticaster();
		for (ApplicationListener<?> listener : application.getListeners()) {
			this.multicaster.addApplicationListener(listener);
		}
	}

	@Override
	public int getOrder() {
		return 0;
	}

	@Override
	public void started() {
		publishEvent(new ApplicationStartedEvent(this.application, this.args));
	}

	@Override
	public void environmentPrepared(ConfigurableEnvironment environment) {
		publishEvent(new ApplicationEnvironmentPreparedEvent(this.application, this.args,
				environment));
	}

	@Override
	public void contextPrepared(ConfigurableApplicationContext context) {
		registerApplicationEventMulticaster(context);
	}

	private void registerApplicationEventMulticaster(
			ConfigurableApplicationContext context) {
		context.getBeanFactory().registerSingleton(
				AbstractApplicationContext.APPLICATION_EVENT_MULTICASTER_BEAN_NAME,
				this.multicaster);
		if (this.multicaster instanceof BeanFactoryAware) {
			((BeanFactoryAware) this.multicaster)
					.setBeanFactory(context.getBeanFactory());
		}
	}

	@Override
	public void contextLoaded(ConfigurableApplicationContext context) {
		for (ApplicationListener<?> listener : this.application.getListeners()) {
			if (listener instanceof ApplicationContextAware) {
				((ApplicationContextAware) listener).setApplicationContext(context);
			}
			context.addApplicationListener(listener);
		}
		publishEvent(new ApplicationPreparedEvent(this.application, this.args, context));
	}

	@Override
	public void finished(ConfigurableApplicationContext context, Throwable exception) {
		publishEvent(getFinishedEvent(context, exception));
	}

	private SpringApplicationEvent getFinishedEvent(
			ConfigurableApplicationContext context, Throwable exception) {
		if (exception != null) {
			return new ApplicationFailedEvent(this.application, this.args, context,
					exception);
		}
		return new ApplicationReadyEvent(this.application, this.args, context);
	}

	private void publishEvent(SpringApplicationEvent event) {
		this.multicaster.multicastEvent(event);
	}

}
