/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.testing.cache;

import java.util.Comparator;

import org.hibernate.cache.CacheException;
import org.hibernate.cache.internal.DefaultCacheKeysFactory;
import org.hibernate.cache.spi.NaturalIdRegion;
import org.hibernate.cache.spi.access.NaturalIdRegionAccessStrategy;
import org.hibernate.cache.spi.access.SoftLock;
import org.hibernate.engine.spi.SessionImplementor;
import org.hibernate.persister.entity.EntityPersister;

/**
 * @author Eric Dalquist
 */
class ReadWriteNaturalIdRegionAccessStrategy extends AbstractReadWriteAccessStrategy
		implements NaturalIdRegionAccessStrategy {

	private final NaturalIdRegionImpl region;

	ReadWriteNaturalIdRegionAccessStrategy(NaturalIdRegionImpl region) {
		this.region = region;
	}

	@Override
	public boolean insert(SessionImplementor session, Object key, Object value) throws CacheException {
		return false;
	}

	@Override
	public boolean update(SessionImplementor session, Object key, Object value) throws CacheException {
		return false;
	}

	@Override
	public boolean afterInsert(SessionImplementor session, Object key, Object value) throws CacheException {

		try {
			writeLock.lock();
			Lockable item = (Lockable) region.get( session, key );
			if ( item == null ) {
				region.put( session, key, new Item( value, null, region.nextTimestamp() ) );
				return true;
			}
			else {
				return false;
			}
		}
		finally {
			writeLock.unlock();
		}
	}


	@Override
	public boolean afterUpdate(SessionImplementor session, Object key, Object value, SoftLock lock) throws CacheException {
		try {
			writeLock.lock();
			Lockable item = (Lockable) region.get( session, key );

			if ( item != null && item.isUnlockable( lock ) ) {
				Lock lockItem = (Lock) item;
				if ( lockItem.wasLockedConcurrently() ) {
					decrementLock( session, key, lockItem );
					return false;
				}
				else {
					region.put( session, key, new Item( value, null, region.nextTimestamp() ) );
					return true;
				}
			}
			else {
				handleLockExpiry( session, key, item );
				return false;
			}
		}
		finally {
			writeLock.unlock();
		}
	}

	@Override
	Comparator getVersionComparator() {
		return region.getCacheDataDescription().getVersionComparator();
	}

	@Override
	protected BaseGeneralDataRegion getInternalRegion() {
		return region;
	}

	@Override
	protected boolean isDefaultMinimalPutOverride() {
		return region.getSettings().isMinimalPutsEnabled();
	}

	@Override
	public NaturalIdRegion getRegion() {
		return region;
	}

	@Override
	public Object generateCacheKey(Object[] naturalIdValues, EntityPersister persister, SessionImplementor session) {
		return DefaultCacheKeysFactory.createNaturalIdKey( naturalIdValues, persister, session );
	}

	@Override
	public Object[] getNaturalIdValues(Object cacheKey) {
		return DefaultCacheKeysFactory.getNaturalIdValues( cacheKey );
	}
}
