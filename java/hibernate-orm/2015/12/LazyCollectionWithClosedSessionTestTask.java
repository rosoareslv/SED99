/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */

package org.hibernate.test.bytecode.enhancement.ondemandload;

import java.math.BigDecimal;

import org.hibernate.Hibernate;
import org.hibernate.Session;
import org.hibernate.cfg.Configuration;
import org.hibernate.cfg.Environment;

import org.hibernate.test.bytecode.enhancement.AbstractEnhancerTestTask;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

public class LazyCollectionWithClosedSessionTestTask extends AbstractEnhancerTestTask {

	public void prepare() {
		Configuration cfg = new Configuration();
		cfg.setProperty( Environment.ENABLE_LAZY_LOAD_NO_TRANS, "true" );
		cfg.setProperty( Environment.GENERATE_STATISTICS, "true" );
		cfg.setProperty( Environment.USE_SECOND_LEVEL_CACHE, "false" );
		super.prepare( cfg );


		Session s = getFactory().openSession();
		s.beginTransaction();

		Store store = new Store( 1 ).setName( "Acme Super Outlet" );
		s.persist( store );

		Product product = new Product( "007" ).setName( "widget" ).setDescription( "FooBar" );
		s.persist( product );

		store.addInventoryProduct( product ).setQuantity( 10L ).setStorePrice( new BigDecimal( 500 ) );

		s.getTransaction().commit();
		s.close();
	}

	public void cleanup() {
	}

	public void execute() {
		getFactory().getStatistics().clear();

		Session s = getFactory().openSession();
		s.beginTransaction();

		// first load the store, making sure collection is not initialized
		Store store = s.get( Store.class, 1 );
		assertNotNull( store );
		assertFalse( Hibernate.isInitialized( store.getInventories() ) );

		assertEquals( 1, getFactory().getStatistics().getSessionOpenCount() );
		assertEquals( 0, getFactory().getStatistics().getSessionCloseCount() );

		// close the session and try to initialize collection
		s.getTransaction().commit();
		s.close();

		assertEquals( 1, getFactory().getStatistics().getSessionOpenCount() );
		assertEquals( 1, getFactory().getStatistics().getSessionCloseCount() );

		store.getInventories().size();
		assertTrue( Hibernate.isInitialized( store.getInventories() ) );

		assertEquals( 2, getFactory().getStatistics().getSessionOpenCount() );
		assertEquals( 2, getFactory().getStatistics().getSessionCloseCount() );
	}

	protected void configure(Configuration cfg) {
	}

	public Class[] getAnnotatedClasses() {
		return new Class[] {
				Store.class,
				Inventory.class,
				Product.class
		};
	}

}
