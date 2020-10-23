/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.cache.infinispan.access;

import org.infinispan.commands.write.DataWriteCommand;
import org.infinispan.context.Flag;
import org.infinispan.context.InvocationContext;
import org.infinispan.interceptors.locking.NonTransactionalLockingInterceptor;
import org.infinispan.util.concurrent.TimeoutException;
import org.infinispan.util.logging.Log;
import org.infinispan.util.logging.LogFactory;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;

/**
 * With regular {@link org.infinispan.interceptors.locking.NonTransactionalLockingInterceptor},
 * async replication does not work in combination with synchronous replication: sync replication
 * relies on locking to order writes on backup while async replication relies on FIFO-ordering
 * from primary to backup. If these two combine, there's a possibility that on backup two modifications
 * modifications will proceed concurrently.
 * Similar issue threatens consistency when the command has {@link org.infinispan.context.Flag#CACHE_MODE_LOCAL}
 * - these commands don't acquire locks either.
 *
 * Therefore, this interceptor locks the entry all the time. {@link UnorderedDistributionInterceptor} does not forward
 * the message from non-origin to any other node, and the distribution interceptor won't block on RPC but will return
 * {@link CompletableFuture} and we'll wait for it here.
 */
public class LockingInterceptor extends NonTransactionalLockingInterceptor {
	private static final Log log = LogFactory.getLog(LockingInterceptor.class);
	private static final boolean trace = log.isTraceEnabled();

	@Override
	protected Object visitDataWriteCommand(InvocationContext ctx, DataWriteCommand command) throws Throwable {
		Object returnValue = null;
		try {
			// Clear any metadata; we'll set them as appropriate in TombstoneCallInterceptor
			command.setMetadata(null);

			lockAndRecord(ctx, command.getKey(), getLockTimeoutMillis(command));

			returnValue = invokeNextInterceptor(ctx, command);
			return returnValue;
		}
		catch (TimeoutException e) {
			if (!ctx.isOriginLocal() && command.hasFlag(Flag.ZERO_LOCK_ACQUISITION_TIMEOUT)) {
				// FAIL_SILENTLY flag is not replicated to remote nodes and zero acquisition timeouts cause
				// very noisy logs.
				if (trace) {
					log.tracef("Silently ignoring exception", e);
				}
				return null;
			}
			else {
				throw e;
			}
		}
		finally {
			lockManager.unlockAll(ctx);
			if (returnValue instanceof CompletableFuture) {
				try {
					((CompletableFuture) returnValue).join();
				}
				catch (CompletionException e) {
					throw e.getCause();
				}
			}
		}
	}
}
