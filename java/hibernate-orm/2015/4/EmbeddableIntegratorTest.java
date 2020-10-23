/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * Copyright (c) 2012, Red Hat Inc. or third-party contributors as
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
package org.hibernate.test.annotations.embeddables;

import java.math.BigDecimal;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

import org.hibernate.JDBCException;
import org.hibernate.Session;
import org.hibernate.SessionFactory;
import org.hibernate.cfg.Configuration;
import org.hibernate.dialect.H2Dialect;
import org.hibernate.exception.GenericJDBCException;

import org.hibernate.testing.RequiresDialect;
import org.hibernate.testing.junit4.BaseUnitTestCase;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

/**
 * @author Chris Pheby
 */
@RequiresDialect(H2Dialect.class)
public class EmbeddableIntegratorTest extends BaseUnitTestCase {

	/**
	 * Throws a mapping exception because DollarValue is not mapped
	 */
	@Test(expected = JDBCException.class)
	public void testWithoutIntegrator() {
		SessionFactory sf = new Configuration().addAnnotatedClass( Investor.class )
				.setProperty( "hibernate.hbm2ddl.auto", "create-drop" )
				.buildSessionFactory();

		try {
			Session sess = sf.openSession();
			Investor myInv = getInvestor();
			myInv.setId( 1L );

			sess.save( myInv );
			sess.flush();
			sess.clear();

			Investor inv = (Investor) sess.get( Investor.class, 1L );
			assertEquals( new BigDecimal( "100" ), inv.getInvestments().get( 0 ).getAmount().getAmount() );

			sess.close();
		}
		finally {
			sf.close();
		}
	}

	@Test
	public void testWithTypeContributor() {
		SessionFactory sf = new Configuration().addAnnotatedClass( Investor.class )
				.registerTypeContributor( new InvestorTypeContributor() )
				.setProperty( "hibernate.hbm2ddl.auto", "create-drop" )
				.buildSessionFactory();

		try {
			Session sess = sf.openSession();
			Investor myInv = getInvestor();
			myInv.setId( 2L );

			sess.save( myInv );
			sess.flush();
			sess.clear();

			Investor inv = (Investor) sess.get( Investor.class, 2L );
			assertEquals( new BigDecimal( "100" ), inv.getInvestments().get( 0 ).getAmount().getAmount() );

			sess.close();
		}
		finally {
			sf.close();
		}
	}

	private Investor getInvestor() {
		Investor i = new Investor();
		List<Investment> investments = new ArrayList<Investment>();
		Investment i1 = new Investment();
		i1.setAmount( new DollarValue( new BigDecimal( "100" ) ) );
		i1.setDate( new MyDate( new Date() ) );
		i1.setDescription( "Test Investment" );
		investments.add( i1 );
		i.setInvestments( investments );

		return i;
	}
}
