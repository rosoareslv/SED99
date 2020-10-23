/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.cache.infinispan.access;

import org.hibernate.cache.infinispan.impl.BaseTransactionalDataRegion;
import org.hibernate.cache.infinispan.util.InvocationAfterCompletion;
import org.hibernate.cache.infinispan.util.VersionedEntry;
import org.hibernate.resource.transaction.TransactionCoordinator;
import org.infinispan.AdvancedCache;

import java.util.concurrent.TimeUnit;

/**
 * @author Radim Vansa &lt;rvansa@redhat.com&gt;
 */
public class RemovalSynchronization extends InvocationAfterCompletion {
	private final BaseTransactionalDataRegion region;
	private final Object key;

	public RemovalSynchronization(TransactionCoordinator tc, AdvancedCache cache, boolean requiresTransaction, BaseTransactionalDataRegion region, Object key) {
		super(tc, cache, requiresTransaction);
		this.region = region;
		this.key = key;
	}

	@Override
	protected void invoke(boolean success, AdvancedCache cache) {
		if (success) {
			cache.put(key, new VersionedEntry(null, null, region.nextTimestamp()), region.getTombstoneExpiration(), TimeUnit.MILLISECONDS);
		}
	}
}
