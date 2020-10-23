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

package org.springframework.boot.context.config;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Set;

import org.apache.commons.logging.Log;

import org.springframework.beans.BeansException;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.context.event.ApplicationEnvironmentPreparedEvent;
import org.springframework.boot.context.event.ApplicationPreparedEvent;
import org.springframework.boot.context.properties.bind.Binder;
import org.springframework.boot.context.properties.bind.PropertySourcesPlaceholdersResolver;
import org.springframework.boot.context.properties.source.ConfigurationPropertySources;
import org.springframework.boot.env.EnvironmentPostProcessor;
import org.springframework.boot.env.PropertySourceLoader;
import org.springframework.boot.env.RandomValuePropertySource;
import org.springframework.boot.logging.DeferredLog;
import org.springframework.context.ApplicationEvent;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.ConfigurationClassPostProcessor;
import org.springframework.context.event.SmartApplicationListener;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.AnnotationAwareOrderComparator;
import org.springframework.core.env.ConfigurableEnvironment;
import org.springframework.core.env.Environment;
import org.springframework.core.env.MutablePropertySources;
import org.springframework.core.env.PropertySource;
import org.springframework.core.io.DefaultResourceLoader;
import org.springframework.core.io.Resource;
import org.springframework.core.io.ResourceLoader;
import org.springframework.core.io.support.SpringFactoriesLoader;
import org.springframework.util.Assert;
import org.springframework.util.ResourceUtils;
import org.springframework.util.StringUtils;

/**
 * {@link EnvironmentPostProcessor} that configures the context environment by loading
 * properties from well known file locations. By default properties will be loaded from
 * 'application.properties' and/or 'application.yml' files in the following locations:
 * <ul>
 * <li>classpath:</li>
 * <li>file:./</li>
 * <li>classpath:config/</li>
 * <li>file:./config/:</li>
 * </ul>
 * <p>
 * Alternative search locations and names can be specified using
 * {@link #setSearchLocations(String)} and {@link #setSearchNames(String)}.
 * <p>
 * Additional files will also be loaded based on active profiles. For example if a 'web'
 * profile is active 'application-web.properties' and 'application-web.yml' will be
 * considered.
 * <p>
 * The 'spring.config.name' property can be used to specify an alternative name to load
 * and the 'spring.config.location' property can be used to specify alternative search
 * locations or specific files.
 * <p>
 *
 * @author Dave Syer
 * @author Phillip Webb
 * @author Stephane Nicoll
 * @author Andy Wilkinson
 * @author Eddú Meléndez
 * @author Madhura Bhave
 */
public class ConfigFileApplicationListener
		implements EnvironmentPostProcessor, SmartApplicationListener, Ordered {

	private static final String DEFAULT_PROPERTIES = "defaultProperties";

	// Note the order is from least to most specific (last one wins)
	private static final String DEFAULT_SEARCH_LOCATIONS = "classpath:/,classpath:/config/,file:./,file:./config/";

	private static final String DEFAULT_NAMES = "application";

	/**
	 * The "active profiles" property name.
	 */
	public static final String ACTIVE_PROFILES_PROPERTY = "spring.profiles.active";

	/**
	 * The "includes profiles" property name.
	 */
	public static final String INCLUDE_PROFILES_PROPERTY = "spring.profiles.include";

	/**
	 * The "config name" property name.
	 */
	public static final String CONFIG_NAME_PROPERTY = "spring.config.name";

	/**
	 * The "config location" property name.
	 */
	public static final String CONFIG_LOCATION_PROPERTY = "spring.config.location";

	/**
	 * The default order for the processor.
	 */
	public static final int DEFAULT_ORDER = Ordered.HIGHEST_PRECEDENCE + 10;

	/**
	 * Name of the application configuration {@link PropertySource}.
	 */
	public static final String APPLICATION_CONFIGURATION_PROPERTY_SOURCE_NAME = "applicationConfigurationProperties";

	private final DeferredLog logger = new DeferredLog();

	private String searchLocations;

	private String names;

	private int order = DEFAULT_ORDER;

	@Override
	public boolean supportsEventType(Class<? extends ApplicationEvent> eventType) {
		return ApplicationEnvironmentPreparedEvent.class.isAssignableFrom(eventType)
				|| ApplicationPreparedEvent.class.isAssignableFrom(eventType);
	}

	@Override
	public boolean supportsSourceType(Class<?> aClass) {
		return true;
	}

	@Override
	public void onApplicationEvent(ApplicationEvent event) {
		if (event instanceof ApplicationEnvironmentPreparedEvent) {
			onApplicationEnvironmentPreparedEvent(
					(ApplicationEnvironmentPreparedEvent) event);
		}
		if (event instanceof ApplicationPreparedEvent) {
			onApplicationPreparedEvent(event);
		}
	}

	private void onApplicationEnvironmentPreparedEvent(
			ApplicationEnvironmentPreparedEvent event) {
		List<EnvironmentPostProcessor> postProcessors = loadPostProcessors();
		postProcessors.add(this);
		AnnotationAwareOrderComparator.sort(postProcessors);
		for (EnvironmentPostProcessor postProcessor : postProcessors) {
			postProcessor.postProcessEnvironment(event.getEnvironment(),
					event.getSpringApplication());
		}
	}

	List<EnvironmentPostProcessor> loadPostProcessors() {
		return SpringFactoriesLoader.loadFactories(EnvironmentPostProcessor.class,
				getClass().getClassLoader());
	}

	@Override
	public void postProcessEnvironment(ConfigurableEnvironment environment,
			SpringApplication application) {
		addPropertySources(environment, application.getResourceLoader());
	}

	private void onApplicationPreparedEvent(ApplicationEvent event) {
		this.logger.replayTo(ConfigFileApplicationListener.class);
		addPostProcessors(((ApplicationPreparedEvent) event).getApplicationContext());
	}

	/**
	 * Add config file property sources to the specified environment.
	 * @param environment the environment to add source to
	 * @param resourceLoader the resource loader
	 * @see #addPostProcessors(ConfigurableApplicationContext)
	 */
	protected void addPropertySources(ConfigurableEnvironment environment,
			ResourceLoader resourceLoader) {
		RandomValuePropertySource.addToEnvironment(environment);
		new Loader(environment, resourceLoader).load();
	}

	/**
	 * Add appropriate post-processors to post-configure the property-sources.
	 * @param context the context to configure
	 */
	protected void addPostProcessors(ConfigurableApplicationContext context) {
		context.addBeanFactoryPostProcessor(
				new PropertySourceOrderingPostProcessor(context));
	}

	public void setOrder(int order) {
		this.order = order;
	}

	@Override
	public int getOrder() {
		return this.order;
	}

	/**
	 * Set the search locations that will be considered as a comma-separated list. Each
	 * search location should be a directory path (ending in "/") and it will be prefixed
	 * by the file names constructed from {@link #setSearchNames(String) search names} and
	 * profiles (if any) plus file extensions supported by the properties loaders.
	 * Locations are considered in the order specified, with later items taking precedence
	 * (like a map merge).
	 * @param locations the search locations
	 */
	public void setSearchLocations(String locations) {
		Assert.hasLength(locations, "Locations must not be empty");
		this.searchLocations = locations;
	}

	/**
	 * Sets the names of the files that should be loaded (excluding file extension) as a
	 * comma-separated list.
	 * @param names the names to load
	 */
	public void setSearchNames(String names) {
		Assert.hasLength(names, "Names must not be empty");
		this.names = names;
	}

	/**
	 * {@link BeanFactoryPostProcessor} to re-order our property sources below any
	 * {@code @PropertySource} items added by the {@link ConfigurationClassPostProcessor}.
	 */
	private class PropertySourceOrderingPostProcessor
			implements BeanFactoryPostProcessor, Ordered {

		private ConfigurableApplicationContext context;

		PropertySourceOrderingPostProcessor(ConfigurableApplicationContext context) {
			this.context = context;
		}

		@Override
		public int getOrder() {
			return Ordered.HIGHEST_PRECEDENCE;
		}

		@Override
		public void postProcessBeanFactory(ConfigurableListableBeanFactory beanFactory)
				throws BeansException {
			reorderSources(this.context.getEnvironment());
		}

		private void reorderSources(ConfigurableEnvironment environment) {
			PropertySource<?> defaultProperties = environment.getPropertySources()
					.remove(DEFAULT_PROPERTIES);
			if (defaultProperties != null) {
				environment.getPropertySources().addLast(defaultProperties);
			}
		}

	}

	/**
	 * Loads candidate property sources and configures the active profiles.
	 */
	private class Loader {

		private final Log logger = ConfigFileApplicationListener.this.logger;

		private final ConfigurableEnvironment environment;

		private final ResourceLoader resourceLoader;

		private final List<PropertySourceLoader> propertySourceLoaders;

		private Queue<Profile> profiles;

		private List<Profile> processedProfiles;

		private boolean activatedProfiles;

		private Map<Profile, MutablePropertySources> loaded;

		Loader(ConfigurableEnvironment environment, ResourceLoader resourceLoader) {
			this.environment = environment;
			this.resourceLoader = resourceLoader == null ? new DefaultResourceLoader()
					: resourceLoader;
			this.propertySourceLoaders = SpringFactoriesLoader.loadFactories(
					PropertySourceLoader.class, getClass().getClassLoader());
		}

		public void load() {
			this.profiles = Collections.asLifoQueue(new LinkedList<Profile>());
			this.processedProfiles = new LinkedList<>();
			this.activatedProfiles = false;
			this.loaded = new LinkedHashMap<>();
			initializeProfiles();
			while (!this.profiles.isEmpty()) {
				Profile profile = this.profiles.poll();
				for (String location : getSearchLocations()) {
					if (!location.endsWith("/")) {
						// location is a filename already, so don't search for more
						// filenames
						load(profile, location, null);
					}
					else {
						for (String name : getSearchNames()) {
							load(profile, location, name);
						}
					}
				}
				this.processedProfiles.add(profile);
			}
			addLoadedPropertySources();
		}

		/**
		 * Initialize profile information from both the {@link Environment} active
		 * profiles and any {@code spring.profiles.active}/{@code spring.profiles.include}
		 * properties that are already set.
		 */
		private void initializeProfiles() {
			Set<Profile> initialActiveProfiles = initializeActiveProfiles();
			this.profiles.addAll(getUnprocessedActiveProfiles(initialActiveProfiles));
			if (this.profiles.isEmpty()) {
				for (String defaultProfileName : this.environment.getDefaultProfiles()) {
					Profile defaultProfile = new Profile(defaultProfileName, true);
					if (!this.profiles.contains(defaultProfile)) {
						this.profiles.add(defaultProfile);
					}
				}
			}
			// The default profile for these purposes is represented as null. We add it
			// last so that it is first out of the queue (active profiles will then
			// override any settings in the defaults when the list is reversed later).
			this.profiles.add(null);
		}

		private Set<Profile> initializeActiveProfiles() {
			if (!this.environment.containsProperty(ACTIVE_PROFILES_PROPERTY)
					&& !this.environment.containsProperty(INCLUDE_PROFILES_PROPERTY)) {
				return Collections.emptySet();
			}
			// Any pre-existing active profiles set via property sources (e.g. System
			// properties) take precedence over those added in config files.
			Binder binder = Binder.get(this.environment);
			Set<Profile> activeProfiles = new LinkedHashSet<>();
			activeProfiles.addAll(getProfiles(binder, ACTIVE_PROFILES_PROPERTY));
			activeProfiles.addAll(getProfiles(binder, INCLUDE_PROFILES_PROPERTY));
			maybeActivateProfiles(activeProfiles);
			return activeProfiles;
		}

		/**
		 * Return the active profiles that have not been processed yet. If a profile is
		 * enabled via both {@link #ACTIVE_PROFILES_PROPERTY} and
		 * {@link ConfigurableEnvironment#addActiveProfile(String)} it needs to be
		 * filtered so that the {@link #ACTIVE_PROFILES_PROPERTY} value takes precedence.
		 * <p>
		 * Concretely, if the "cloud" profile is enabled via the environment, it will take
		 * less precedence that any profile set via the {@link #ACTIVE_PROFILES_PROPERTY}.
		 * @param initialActiveProfiles the profiles that have been enabled via
		 * {@link #ACTIVE_PROFILES_PROPERTY}
		 * @return the unprocessed active profiles from the environment to enable
		 */
		private List<Profile> getUnprocessedActiveProfiles(
				Set<Profile> initialActiveProfiles) {
			List<Profile> unprocessedActiveProfiles = new ArrayList<>();
			for (String profileName : this.environment.getActiveProfiles()) {
				Profile profile = new Profile(profileName);
				if (!initialActiveProfiles.contains(profile)) {
					unprocessedActiveProfiles.add(profile);
				}
			}
			// Reverse them so the order is the same as from getProfilesForValue()
			// (last one wins when properties are eventually resolved)
			Collections.reverse(unprocessedActiveProfiles);
			return unprocessedActiveProfiles;
		}

		/**
		 * Load an actual property source file.
		 * @param profile the profile being loaded
		 * @param location the location of the resource
		 * @param name an optional name to be combined with the location
		 */
		private void load(Profile profile, String location, String name) {
			if (!StringUtils.hasText(name)) {
				for (PropertySourceLoader loader : this.propertySourceLoaders) {
					if (canLoadFileExtension(loader, location)) {
						load(loader, profile, location,
								(profile == null ? null : profile.getName()));
					}
				}
			}
			for (PropertySourceLoader loader : this.propertySourceLoaders) {
				for (String ext : loader.getFileExtensions()) {
					loadForFileExtension(loader, profile, location + name, "." + ext);
				}
			}
		}

		private boolean canLoadFileExtension(PropertySourceLoader loader, String name) {
			return Arrays.stream(loader.getFileExtensions()).map(String::toLowerCase)
					.anyMatch(name.toLowerCase()::endsWith);
		}

		private void loadForFileExtension(PropertySourceLoader loader, Profile profile,
				String prefix, String ext) {
			if (profile != null) {
				// Try the profile-specific file
				load(loader, profile, prefix + "-" + profile + ext, null);
				// Support profile section in profile file (gh-340)
				load(loader, profile, prefix + "-" + profile + ext, profile.getName());
				// Try profile specific sections in files we've already processed
				for (Profile processedProfile : this.processedProfiles) {
					if (processedProfile != null) {
						String previouslyLoaded = prefix + "-" + processedProfile + ext;
						load(loader, profile, previouslyLoaded, profile.getName());
					}
				}
			}
			// Also try the profile-specific section (if any) of the normal file
			load(loader, profile, prefix + ext,
					(profile == null ? null : profile.getName()));
		}

		private void load(PropertySourceLoader loader, Profile profile, String location,
				String loadProfile) {
			try {
				Resource resource = this.resourceLoader.getResource(location);
				String description = getDescription(profile, location, resource);
				if (profile != null) {
					description = description + " for profile " + profile;
				}
				if (resource == null || !resource.exists()) {
					this.logger.trace("Skipped missing config " + description);
					return;
				}
				if (!StringUtils.hasText(
						StringUtils.getFilenameExtension(resource.getFilename()))) {
					this.logger.trace("Skipped empty config extension " + description);
					return;
				}
				String name = "applicationConfig: [" + location + "]"
						+ (loadProfile == null ? "" : "#" + loadProfile);
				PropertySource<?> loaded = loader.load(name, resource, loadProfile);
				if (loaded == null) {
					this.logger.trace("Skipped unloaded config " + description);
					return;
				}
				handleProfileProperties(loaded);
				this.loaded.computeIfAbsent(profile, (k) -> new MutablePropertySources())
						.addLast(loaded);
				this.logger.debug("Loaded config file " + description);
			}
			catch (Exception ex) {
				throw new IllegalStateException("Failed to load property "
						+ "source from location '" + location + "'", ex);
			}
		}

		private String getDescription(Profile profile, String location,
				Resource resource) {
			try {
				if (resource != null) {
					String uri = resource.getURI().toASCIIString();
					return String.format("'%s' (%s)", uri, location);
				}
			}
			catch (IOException ex) {
			}
			return String.format("'%s'", location);
		}

		private void handleProfileProperties(PropertySource<?> propertySource) {
			Binder binder = new Binder(ConfigurationPropertySources.from(propertySource),
					new PropertySourcesPlaceholdersResolver(this.environment));
			Set<Profile> active = getProfiles(binder, "spring.profiles.active");
			Set<Profile> include = getProfiles(binder, "spring.profiles.include");
			maybeActivateProfiles(active);
			addProfiles(include);
		}

		private Set<Profile> getProfiles(Binder binder, String name) {
			return binder.bind(name, String[].class).map(this::asProfileSet)
					.orElse(Collections.emptySet());
		}

		private Set<Profile> asProfileSet(String[] profileNames) {
			List<Profile> profiles = new ArrayList<>();
			for (String profileName : profileNames) {
				profiles.add(new Profile(profileName));
			}
			Collections.reverse(profiles);
			return new LinkedHashSet<>(profiles);
		}

		private void maybeActivateProfiles(Set<Profile> profiles) {
			if (profiles.isEmpty()) {
				return;
			}
			if (this.activatedProfiles) {
				this.logger.debug("Profiles already activated, '" + profiles
						+ "' will not be applied");
				return;
			}
			addProfiles(profiles);
			this.logger.debug("Activated profiles "
					+ StringUtils.collectionToCommaDelimitedString(profiles));
			this.activatedProfiles = true;
			removeUnprocessedDefaultProfiles();
		}

		private void removeUnprocessedDefaultProfiles() {
			for (Iterator<Profile> iterator = this.profiles.iterator(); iterator
					.hasNext();) {
				if (iterator.next().isDefaultProfile()) {
					iterator.remove();
				}
			}
		}

		private void addProfiles(Set<Profile> profiles) {
			for (Profile profile : profiles) {
				this.profiles.add(profile);
				if (!environmentHasActiveProfile(profile.getName())) {
					// If it's already accepted we assume the order was set
					// intentionally
					prependProfile(this.environment, profile);
				}
			}
		}

		private boolean environmentHasActiveProfile(String profile) {
			for (String activeProfile : this.environment.getActiveProfiles()) {
				if (activeProfile.equals(profile)) {
					return true;
				}
			}
			return false;
		}

		private void prependProfile(ConfigurableEnvironment environment,
				Profile profile) {
			Set<String> profiles = new LinkedHashSet<>();
			environment.getActiveProfiles(); // ensure they are initialized
			// But this one should go first (last wins in a property key clash)
			profiles.add(profile.getName());
			profiles.addAll(Arrays.asList(environment.getActiveProfiles()));
			environment.setActiveProfiles(profiles.toArray(new String[profiles.size()]));
		}

		private Set<String> getSearchLocations() {
			Set<String> locations = new LinkedHashSet<>();
			// User-configured settings take precedence, so we do them first
			if (this.environment.containsProperty(CONFIG_LOCATION_PROPERTY)) {
				for (String path : asResolvedSet(
						this.environment.getProperty(CONFIG_LOCATION_PROPERTY), null)) {
					if (!path.contains("$")) {
						path = StringUtils.cleanPath(path);
						if (!ResourceUtils.isUrl(path)) {
							path = ResourceUtils.FILE_URL_PREFIX + path;
						}
					}
					locations.add(path);
				}
			}
			locations.addAll(
					asResolvedSet(ConfigFileApplicationListener.this.searchLocations,
							DEFAULT_SEARCH_LOCATIONS));
			return locations;
		}

		private Set<String> getSearchNames() {
			if (this.environment.containsProperty(CONFIG_NAME_PROPERTY)) {
				String property = this.environment.getProperty(CONFIG_NAME_PROPERTY);
				return asResolvedSet(property, null);
			}
			return asResolvedSet(ConfigFileApplicationListener.this.names, DEFAULT_NAMES);
		}

		private Set<String> asResolvedSet(String value, String fallback) {
			List<String> list = Arrays.asList(StringUtils.trimArrayElements(
					StringUtils.commaDelimitedListToStringArray(value != null
							? this.environment.resolvePlaceholders(value) : fallback)));
			Collections.reverse(list);
			return new LinkedHashSet<>(list);
		}

		private void addLoadedPropertySources() {
			MutablePropertySources destination = this.environment.getPropertySources();
			String lastAdded = null;
			List<MutablePropertySources> loaded = new ArrayList<>(this.loaded.values());
			Collections.reverse(loaded);
			for (MutablePropertySources sources : loaded) {
				for (PropertySource<?> source : sources) {
					if (lastAdded == null) {
						if (destination.contains(DEFAULT_PROPERTIES)) {
							destination.addBefore(DEFAULT_PROPERTIES, source);
						}
						else {
							destination.addLast(source);
						}
					}
					else {
						destination.addAfter(lastAdded, source);
					}
					lastAdded = source.getName();
				}
			}
		}

	}

	private static class Profile {

		private final String name;

		private final boolean defaultProfile;

		Profile(String name) {
			this(name, false);
		}

		Profile(String name, boolean defaultProfile) {
			Assert.notNull(name, "Name must not be null");
			this.name = name;
			this.defaultProfile = defaultProfile;
		}

		public String getName() {
			return this.name;
		}

		public boolean isDefaultProfile() {
			return this.defaultProfile;
		}

		@Override
		public String toString() {
			return this.name;
		}

		@Override
		public int hashCode() {
			return this.name.hashCode();
		}

		@Override
		public boolean equals(Object obj) {
			if (obj == this) {
				return true;
			}
			if (obj == null || obj.getClass() != getClass()) {
				return false;
			}
			return ((Profile) obj).name.equals(this.name);
		}

	}

}
