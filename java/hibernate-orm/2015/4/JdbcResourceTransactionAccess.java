/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * Copyright (c) 2013, Red Hat Inc. or third-party contributors as
 * indicated by the @author tags or express copyright attribution
 * statements applied by the authors.  All third-party contributions are
 * distributed under license by Red Hat Inc.
 *
 * This copyrighted material is made available to anyone wishing to use, modify,
 * copy, or redistribute it subject to the terms and conditions of the GNU
 * Lesser General Public License, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this distribution; if not, write to:
 * Free Software Foundation, Inc.
 * 51 Franklin Street, Fifth Floor
 * Boston, MA  02110-1301  USA
 */
package org.hibernate.resource.transaction.backend.jdbc.spi;

/**
 * Provides access to DataStoreTransaction (JDBC transaction stand-in) for use in building resource-local
 * TransactionCoordinator instances.
 *
 * @author Steve Ebersole
 */
public interface JdbcResourceTransactionAccess {
	/**
	 * Provides access to the resource local transaction of this data store, which is used by the TransactionCoordinator
	 * to manage transactions against the data store when not using JTA.
	 *
	 * @return The resource-local transaction
	 */
	public JdbcResourceTransaction getResourceLocalTransaction();
}
