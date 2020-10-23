/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.cache.infinispan.access;

import org.hibernate.cache.infinispan.util.CacheCommandInitializer;
import org.hibernate.cache.infinispan.util.EndInvalidationCommand;
import org.infinispan.commands.VisitableCommand;
import org.infinispan.commands.tx.CommitCommand;
import org.infinispan.commands.tx.PrepareCommand;
import org.infinispan.commands.tx.RollbackCommand;
import org.infinispan.commands.write.InvalidateCommand;
import org.infinispan.commands.write.WriteCommand;
import org.infinispan.container.DataContainer;
import org.infinispan.context.impl.TxInvocationContext;
import org.infinispan.factories.annotations.Inject;
import org.infinispan.interceptors.base.BaseRpcInterceptor;
import org.infinispan.remoting.inboundhandler.DeliverOrder;
import org.infinispan.remoting.rpc.RpcManager;
import org.infinispan.util.logging.Log;
import org.infinispan.util.logging.LogFactory;

import java.util.Set;

/**
 * Intercepts transactions in Infinispan, calling {@link PutFromLoadValidator#beginInvalidatingKey(Object, Object)}
 * before locks are acquired (and the entry is invalidated) and sends {@link EndInvalidationCommand} to release
 * invalidation throught {@link PutFromLoadValidator#endInvalidatingKey(Object, Object)} after the transaction
 * is committed.
 *
 * @author Radim Vansa &lt;rvansa@redhat.com&gt;
 */
class TxPutFromLoadInterceptor extends BaseRpcInterceptor {
	private final static Log log = LogFactory.getLog(TxPutFromLoadInterceptor.class);
	private PutFromLoadValidator putFromLoadValidator;
	private final String cacheName;
	private RpcManager rpcManager;
	private CacheCommandInitializer cacheCommandInitializer;
	private DataContainer dataContainer;

	public TxPutFromLoadInterceptor(PutFromLoadValidator putFromLoadValidator, String cacheName) {
		this.putFromLoadValidator = putFromLoadValidator;
		this.cacheName = cacheName;
	}

	@Inject
	public void injectDependencies(RpcManager rpcManager, CacheCommandInitializer cacheCommandInitializer, DataContainer dataContainer) {
		this.rpcManager = rpcManager;
		this.cacheCommandInitializer = cacheCommandInitializer;
		this.dataContainer = dataContainer;
	}

	// We need to intercept PrepareCommand, not InvalidateCommand since the interception takes
	// place before EntryWrappingInterceptor and the PrepareCommand is multiplexed into InvalidateCommands
	// as part of EntryWrappingInterceptor
	@Override
	public Object visitPrepareCommand(TxInvocationContext ctx, PrepareCommand command) throws Throwable {
		if (ctx.isOriginLocal()) {
			// We can't wait to commit phase to remove the entry locally (invalidations are processed in 1pc
			// on remote nodes, so only local case matters here). The problem is that while the entry is locked
			// reads still can take place and we can read outdated collection after reading updated entity
			// owning this collection from DB; when this happens, the version lock on entity cannot protect
			// us against concurrent modification of the collection. Therefore, we need to remove the entry
			// here (even without lock!) and let possible update happen in commit phase.
			for (WriteCommand wc : command.getModifications()) {
				if (wc instanceof InvalidateCommand) {
					// ISPN-5605 InvalidateCommand does not correctly implement getAffectedKeys()
					for (Object key : ((InvalidateCommand) wc).getKeys()) {
						dataContainer.remove(key);
					}
				}
				else {
					for (Object key : wc.getAffectedKeys()) {
						dataContainer.remove(key);
					}
				}
			}
		}
		else {
			for (WriteCommand wc : command.getModifications()) {
				if (wc instanceof InvalidateCommand) {
					// ISPN-5605 InvalidateCommand does not correctly implement getAffectedKeys()
					for (Object key : ((InvalidateCommand) wc).getKeys()) {
						putFromLoadValidator.beginInvalidatingKey(ctx.getLockOwner(), key);
					}
				}
				else {
					for (Object key : wc.getAffectedKeys()) {
						putFromLoadValidator.beginInvalidatingKey(ctx.getLockOwner(), key);
					}
				}
			}
		}
		return invokeNextInterceptor(ctx, command);
	}

	@Override
	public Object visitCommitCommand(TxInvocationContext ctx, CommitCommand command) throws Throwable {
		return endInvalidationAndInvokeNextInterceptor(ctx, command);
	}

	@Override
	public Object visitRollbackCommand(TxInvocationContext ctx, RollbackCommand command) throws Throwable {
		return endInvalidationAndInvokeNextInterceptor(ctx, command);
	}

	protected Object endInvalidationAndInvokeNextInterceptor(TxInvocationContext ctx, VisitableCommand command) throws Throwable {
		try {
			if (ctx.isOriginLocal()) {
				// send async Commit
				Set<Object> affectedKeys = ctx.getAffectedKeys();
				if (!affectedKeys.isEmpty()) {
					EndInvalidationCommand commitCommand = cacheCommandInitializer.buildEndInvalidationCommand(
							cacheName, affectedKeys.toArray(), ctx.getGlobalTransaction());
					rpcManager.invokeRemotely(null, commitCommand, rpcManager.getDefaultRpcOptions(false, DeliverOrder.NONE));
				}
			}
		}
		finally {
			return invokeNextInterceptor(ctx, command);
		}
	}
}
