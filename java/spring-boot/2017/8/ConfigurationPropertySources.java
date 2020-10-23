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

package org.springframework.boot.context.properties.source;

import java.util.Collections;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

import org.springframework.core.env.ConfigurableEnvironment;
import org.springframework.core.env.Environment;
import org.springframework.core.env.MutablePropertySources;
import org.springframework.core.env.PropertySource;
import org.springframework.core.env.PropertySource.StubPropertySource;
import org.springframework.core.env.PropertySourcesPropertyResolver;
import org.springframework.util.Assert;

/**
 * Provides access to {@link ConfigurationPropertySource ConfigurationPropertySources}.
 *
 * @author Phillip Webb
 * @since 2.0.0
 */
public final class ConfigurationPropertySources {

	/**
	 * The name of the {@link PropertySource} {@link #adapt adapter}.
	 */
	private static final String ATTACHED_PROPERTY_SOURCE_NAME = "configurationProperties";

	private ConfigurationPropertySources() {
	}

	/**
	 * Attach a {@link ConfigurationPropertySource} support to the specified
	 * {@link Environment}. Adapts each {@link PropertySource} managed by the environment
	 * to a {@link ConfigurationPropertySource} and allows classic
	 * {@link PropertySourcesPropertyResolver} calls to resolve using
	 * {@link ConfigurationPropertyName configuration property names}.
	 * <p>
	 * The attached resolver will dynamically track any additions or removals from the
	 * underlying {@link Environment} property sources.
	 * @param environment the source environment (must be an instance of
	 * {@link ConfigurableEnvironment})
	 * @see #get(Environment)
	 */
	public static void attach(Environment environment) {
		Assert.isInstanceOf(ConfigurableEnvironment.class, environment);
		MutablePropertySources sources = ((ConfigurableEnvironment) environment)
				.getPropertySources();
		if (!sources.contains(ATTACHED_PROPERTY_SOURCE_NAME)) {
			sources.addFirst(new ConfigurationPropertySourcesPropertySource(
					ATTACHED_PROPERTY_SOURCE_NAME,
					new SpringConfigurationPropertySources(sources)));
		}
	}

	/**
	 * Return a set of {@link ConfigurationPropertySource} instances that have previously
	 * been {@link #attach(Environment) attached} to the {@link Environment}.
	 * @param environment the source environment (must be an instance of
	 * {@link ConfigurableEnvironment})
	 * @return an iterable set of configuration property sources
	 * @throws IllegalStateException if not configuration property sources have been
	 * attached
	 */
	public static Iterable<ConfigurationPropertySource> get(Environment environment) {
		Assert.isInstanceOf(ConfigurableEnvironment.class, environment);
		MutablePropertySources sources = ((ConfigurableEnvironment) environment)
				.getPropertySources();
		ConfigurationPropertySourcesPropertySource attached = (ConfigurationPropertySourcesPropertySource) sources
				.get(ATTACHED_PROPERTY_SOURCE_NAME);
		if (attached == null) {
			return from(sources);
		}
		return attached.getSource();
	}

	/**
	 * Return {@link Iterable} containing a single new {@link ConfigurationPropertySource}
	 * adapted from the given Spring {@link PropertySource}.
	 * @param source the Spring property source to adapt
	 * @return an {@link Iterable} containing a single newly adapted
	 * {@link SpringConfigurationPropertySource}
	 */
	public static Iterable<ConfigurationPropertySource> from(PropertySource<?> source) {
		return Collections.singleton(SpringConfigurationPropertySource.from(source));
	}

	/**
	 * Return {@link Iterable} containing new {@link ConfigurationPropertySource}
	 * instances adapted from the given Spring {@link MutablePropertySources}.
	 * <p>
	 * This method will flatten any nested property sources and will filter all
	 * {@link StubPropertySource stub property sources}. Updates to the underlying source
	 * will be automatically tracked.
	 * @param sources the Spring property sources to adapt
	 * @return an {@link Iterable} containing newly adapted
	 * {@link SpringConfigurationPropertySource} instances
	 */
	public static Iterable<ConfigurationPropertySource> from(
			MutablePropertySources sources) {
		return new SpringConfigurationPropertySources(sources);
	}

	/**
	 * Return {@link Iterable} containing new {@link ConfigurationPropertySource}
	 * instances adapted from the given Spring {@link PropertySource PropertySources}.
	 * <p>
	 * This method will flatten any nested property sources and will filter all
	 * {@link StubPropertySource stub property sources}.
	 * @param sources the Spring property sources to adapt
	 * @return an {@link Iterable} containing newly adapted
	 * {@link SpringConfigurationPropertySource} instances
	 */
	public static Iterable<ConfigurationPropertySource> from(
			Iterable<PropertySource<?>> sources) {
		return streamPropertySources(sources).map(SpringConfigurationPropertySource::from)
				.collect(Collectors.toList());
	}

	private static Stream<PropertySource<?>> streamPropertySources(
			Iterable<PropertySource<?>> sources) {
		return StreamSupport.stream(sources.spliterator(), false)
				.flatMap(ConfigurationPropertySources::flatten)
				.filter(ConfigurationPropertySources::isIncluded);
	}

	private static Stream<PropertySource<?>> flatten(PropertySource<?> source) {
		if (source.getSource() instanceof ConfigurableEnvironment) {
			return streamPropertySources(
					((ConfigurableEnvironment) source.getSource()).getPropertySources());
		}
		return Stream.of(source);
	}

	private static boolean isIncluded(PropertySource<?> source) {
		return !(source instanceof StubPropertySource)
				&& !(source instanceof ConfigurationPropertySourcesPropertySource);
	}

}
