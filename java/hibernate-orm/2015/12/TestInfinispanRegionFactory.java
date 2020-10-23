package org.hibernate.test.cache.infinispan.util;

import org.hibernate.cache.infinispan.InfinispanRegionFactory;
import org.infinispan.commons.executors.CachedThreadPoolExecutorFactory;
import org.infinispan.configuration.cache.CacheMode;
import org.infinispan.configuration.cache.ConfigurationBuilder;
import org.infinispan.configuration.global.TransportConfigurationBuilder;
import org.infinispan.configuration.parsing.ConfigurationBuilderHolder;
import org.infinispan.manager.DefaultCacheManager;
import org.infinispan.manager.EmbeddedCacheManager;
import org.infinispan.test.fwk.TestResourceTracker;
import org.infinispan.transaction.TransactionMode;
import org.infinispan.util.TimeService;

import java.util.Map;
import java.util.Properties;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Factory that should be overridden in tests.
 *
 * @author Radim Vansa &lt;rvansa@redhat.com&gt;
 */
public class TestInfinispanRegionFactory extends InfinispanRegionFactory {
	protected static final String PREFIX = TestInfinispanRegionFactory.class.getName() + ".";
	public static final String TRANSACTIONAL = PREFIX + "transactional";
	public static final String CACHE_MODE = PREFIX + "cacheMode";
	public static final String TIME_SERVICE = PREFIX + "timeService";

	private final boolean transactional;
	private final CacheMode cacheMode;
	private final TimeService timeService;

	public TestInfinispanRegionFactory(Properties properties) {
		transactional = (boolean) properties.getOrDefault(TRANSACTIONAL, false);
		cacheMode = (CacheMode) properties.getOrDefault(CACHE_MODE, null);
		timeService = (TimeService) properties.getOrDefault(TIME_SERVICE, null);
	}

	@Override
	protected EmbeddedCacheManager createCacheManager(ConfigurationBuilderHolder holder) {
		amendConfiguration(holder);
		DefaultCacheManager cacheManager = new DefaultCacheManager(holder, true);
		if (timeService != null) {
			cacheManager.getGlobalComponentRegistry().registerComponent(timeService, TimeService.class);
			cacheManager.getGlobalComponentRegistry().rewire();
		}
		return cacheManager;
	}

	protected void amendConfiguration(ConfigurationBuilderHolder holder) {
		holder.getGlobalConfigurationBuilder().globalJmxStatistics().allowDuplicateDomains(true);
		TransportConfigurationBuilder transport = holder.getGlobalConfigurationBuilder().transport();
		transport.nodeName(TestResourceTracker.getNextNodeName());
		transport.clusterName(TestResourceTracker.getCurrentTestName());
		// minimize number of threads using unlimited cached thread pool
		transport.remoteCommandThreadPool().threadPoolFactory(CachedThreadPoolExecutorFactory.create());
		transport.transportThreadPool().threadPoolFactory(CachedThreadPoolExecutorFactory.create());
		for (Map.Entry<String, ConfigurationBuilder> cfg : holder.getNamedConfigurationBuilders().entrySet()) {
			amendCacheConfiguration(cfg.getKey(), cfg.getValue());
		}
	}

	protected void amendCacheConfiguration(String cacheName, ConfigurationBuilder configurationBuilder) {
		if (transactional) {
			if (!cacheName.endsWith("query") && !cacheName.equals(DEF_TIMESTAMPS_RESOURCE)) {
				configurationBuilder.transaction().transactionMode(TransactionMode.TRANSACTIONAL).useSynchronization(true);
			}
		} else {
			configurationBuilder.transaction().transactionMode(TransactionMode.NON_TRANSACTIONAL);
		}
		if (cacheMode != null) {
			if (configurationBuilder.clustering().cacheMode().isInvalidation()) {
				configurationBuilder.clustering().cacheMode(cacheMode);
			}
		}
	}

	@Override
	public long nextTimestamp() {
		if (timeService == null) {
			return super.nextTimestamp();
		} else {
			return timeService.wallClockTime();
		}
	}
}
