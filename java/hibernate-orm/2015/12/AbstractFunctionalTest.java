/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.test.cache.infinispan.functional;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.hibernate.Session;
import org.hibernate.boot.Metadata;
import org.hibernate.boot.spi.MetadataImplementor;
import org.hibernate.cache.spi.RegionFactory;
import org.hibernate.cache.spi.access.AccessType;
import org.hibernate.cfg.AvailableSettings;
import org.hibernate.cfg.Environment;
import org.hibernate.engine.jdbc.connections.spi.ConnectionProvider;
import org.hibernate.engine.transaction.jta.platform.spi.JtaPlatform;
import org.hibernate.mapping.Column;
import org.hibernate.mapping.PersistentClass;
import org.hibernate.mapping.Property;
import org.hibernate.mapping.RootClass;
import org.hibernate.mapping.SimpleValue;
import org.hibernate.resource.transaction.TransactionCoordinatorBuilder;
import org.hibernate.resource.transaction.backend.jdbc.internal.JdbcResourceLocalTransactionCoordinatorBuilderImpl;
import org.hibernate.resource.transaction.backend.jta.internal.JtaTransactionCoordinatorBuilderImpl;

import org.hibernate.test.cache.infinispan.tm.XaConnectionProvider;
import org.hibernate.test.cache.infinispan.util.InfinispanTestingSetup;
import org.hibernate.test.cache.infinispan.util.TestInfinispanRegionFactory;
import org.hibernate.test.cache.infinispan.util.TxUtil;
import org.hibernate.testing.BeforeClassOnce;
import org.hibernate.testing.junit4.BaseNonConfigCoreFunctionalTestCase;
import org.hibernate.test.cache.infinispan.tm.JtaPlatformImpl;

import org.hibernate.testing.junit4.CustomParameterized;
import org.infinispan.configuration.cache.CacheMode;
import org.junit.ClassRule;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;

/**
 * @author Galder Zamarreño
 * @since 3.5
 */
@RunWith(CustomParameterized.class)
public abstract class AbstractFunctionalTest extends BaseNonConfigCoreFunctionalTestCase {
	protected static final Object[] TRANSACTIONAL = new Object[]{"transactional", JtaPlatformImpl.class, JtaTransactionCoordinatorBuilderImpl.class, XaConnectionProvider.class, AccessType.TRANSACTIONAL, true, CacheMode.INVALIDATION_SYNC, false };
	protected static final Object[] READ_WRITE_INVALIDATION = new Object[]{"read-write", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_WRITE, false, CacheMode.INVALIDATION_SYNC, false };
	protected static final Object[] READ_ONLY_INVALIDATION = new Object[]{"read-only", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_ONLY, false, CacheMode.INVALIDATION_SYNC, false };
	protected static final Object[] READ_WRITE_REPLICATED = new Object[]{"read-write", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_WRITE, false, CacheMode.REPL_SYNC, false };
	protected static final Object[] READ_ONLY_REPLICATED = new Object[]{"read-only", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_ONLY, false, CacheMode.REPL_SYNC, false };
	protected static final Object[] READ_WRITE_DISTRIBUTED = new Object[]{"read-write", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_WRITE, false, CacheMode.DIST_SYNC, false };
	protected static final Object[] READ_ONLY_DISTRIBUTED = new Object[]{"read-only", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.READ_ONLY, false, CacheMode.DIST_SYNC, false };
	protected static final Object[] NONSTRICT_REPLICATED = new Object[]{"nonstrict", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.NONSTRICT_READ_WRITE, false, CacheMode.REPL_SYNC, true };
	protected static final Object[] NONSTRICT_DISTRIBUTED = new Object[]{"nonstrict", null, JdbcResourceLocalTransactionCoordinatorBuilderImpl.class, null, AccessType.NONSTRICT_READ_WRITE, false, CacheMode.DIST_SYNC, true };

	// We need to use @ClassRule here since in @BeforeClassOnce startUp we're preparing the session factory,
	// constructing CacheManager along - and there we check that the test has the name already set
	@ClassRule
	public static final InfinispanTestingSetup infinispanTestIdentifier = new InfinispanTestingSetup();

	@Parameterized.Parameter(value = 0)
	public String mode;

	@Parameterized.Parameter(value = 1)
	public Class<? extends JtaPlatform> jtaPlatformClass;

	@Parameterized.Parameter(value = 2)
	public Class<? extends TransactionCoordinatorBuilder> transactionCoordinatorBuilderClass;

	@Parameterized.Parameter(value = 3)
	public Class<? extends ConnectionProvider> connectionProviderClass;

	@Parameterized.Parameter(value = 4)
	public AccessType accessType;

	@Parameterized.Parameter(value = 5)
	public boolean useTransactionalCache;

	@Parameterized.Parameter(value = 6)
	public CacheMode cacheMode;

	@Parameterized.Parameter(value = 7)
	public boolean addVersions;

	protected boolean useJta;

	@CustomParameterized.Order(0)
	@Parameterized.Parameters(name = "{0}, {6}")
	public abstract List<Object[]> getParameters();

	public List<Object[]> getParameters(boolean tx, boolean rw, boolean ro, boolean nonstrict) {
		ArrayList<Object[]> parameters = new ArrayList<>();
		if (tx) {
			parameters.add(TRANSACTIONAL);
		}
		if (rw) {
			parameters.add(READ_WRITE_INVALIDATION);
			parameters.add(READ_WRITE_REPLICATED);
			parameters.add(READ_WRITE_DISTRIBUTED);
		}
		if (ro) {
			parameters.add(READ_ONLY_INVALIDATION);
			parameters.add(READ_ONLY_REPLICATED);
			parameters.add(READ_ONLY_DISTRIBUTED);
		}
		if (nonstrict) {
			parameters.add(NONSTRICT_REPLICATED);
			parameters.add(NONSTRICT_DISTRIBUTED);
		}
		return parameters;
	}

	@BeforeClassOnce
	public void setUseJta() {
		useJta = jtaPlatformClass != null;
	}

	@Override
	public String[] getMappings() {
		return new String[] {
				"cache/infinispan/functional/entities/Item.hbm.xml",
				"cache/infinispan/functional/entities/Customer.hbm.xml",
				"cache/infinispan/functional/entities/Contact.hbm.xml"
		};
	}

	@Override
	protected void afterMetadataBuilt(Metadata metadata) {
		if (addVersions) {
			for (PersistentClass clazz : metadata.getEntityBindings()) {
				if (clazz.getVersion() != null) {
					continue;
				}
				try {
					clazz.getMappedClass().getMethod("getVersion");
					clazz.getMappedClass().getMethod("setVersion", long.class);
				} catch (NoSuchMethodException e) {
					continue;
				}
				RootClass rootClazz = clazz.getRootClass();
				Property versionProperty = new Property();
				versionProperty.setName("version");
				SimpleValue value = new SimpleValue((MetadataImplementor) metadata, rootClazz.getTable());
				value.setTypeName("long");
				Column column = new Column();
				column.setValue(value);
				column.setName("version");
				value.addColumn(column);
				rootClazz.getTable().addColumn(column);
				versionProperty.setValue(value);
				rootClazz.setVersion(versionProperty);
				rootClazz.addProperty(versionProperty);
			}
		}
	}

	@Override
	public String getCacheConcurrencyStrategy() {
		return accessType.getExternalName();
	}

	protected Class<? extends RegionFactory> getRegionFactoryClass() {
		return TestInfinispanRegionFactory.class;
	}

	protected boolean getUseQueryCache() {
		return true;
	}

	@Override
	@SuppressWarnings("unchecked")
	protected void addSettings(Map settings) {
		super.addSettings( settings );

		settings.put( Environment.USE_SECOND_LEVEL_CACHE, "true" );
		settings.put( Environment.GENERATE_STATISTICS, "true" );
		settings.put( Environment.USE_QUERY_CACHE, String.valueOf( getUseQueryCache() ) );
		settings.put( Environment.CACHE_REGION_FACTORY, getRegionFactoryClass().getName() );
		settings.put( TestInfinispanRegionFactory.TRANSACTIONAL, useTransactionalCache );
		settings.put( TestInfinispanRegionFactory.CACHE_MODE, cacheMode);

		if ( jtaPlatformClass != null ) {
			settings.put( AvailableSettings.JTA_PLATFORM, jtaPlatformClass.getName() );
		}
		settings.put( Environment.TRANSACTION_COORDINATOR_STRATEGY, transactionCoordinatorBuilderClass.getName() );
		if ( connectionProviderClass != null) {
			settings.put(Environment.CONNECTION_PROVIDER, connectionProviderClass.getName());
		}
	}

	protected void markRollbackOnly(Session session) {
		TxUtil.markRollbackOnly(useJta, session);
	}
}