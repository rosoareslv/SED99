/*
 * Copyright (c) 2002-2018 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j Enterprise Edition. The included source
 * code can be redistributed and/or modified under the terms of the
 * GNU AFFERO GENERAL PUBLIC LICENSE Version 3
 * (http://www.fsf.org/licensing/licenses/agpl-3.0.html) with the
 * Commons Clause, as found in the associated LICENSE.txt file.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * Neo4j object code can be licensed independently from the source
 * under separate terms from the AGPL. Inquiries can be directed to:
 * licensing@neo4j.com
 *
 * More information is also available at:
 * https://neo4j.com/licensing/
 */
package org.neo4j.graphdb;

import org.junit.Rule;
import org.junit.Test;

import org.neo4j.graphdb.factory.EnterpriseGraphDatabaseFactory;
import org.neo4j.helpers.Exceptions;
import org.neo4j.kernel.impl.constraints.StandardConstraintSemantics;
import org.neo4j.test.TestGraphDatabaseFactory;
import org.neo4j.test.rule.TestDirectory;

import static org.hamcrest.Matchers.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.fail;

public class StartupConstraintSemanticsTest
{
    @Rule
    public final TestDirectory dir = TestDirectory.testDirectory();

    @Test
    public void shouldNotAllowOpeningADatabaseWithPECInCommunityEdition()
    {
        assertThatCommunityCannotStartOnEnterpriseOnlyConstraint( "CREATE CONSTRAINT ON (n:Draconian) ASSERT exists(n.required)",
                StandardConstraintSemantics.ERROR_MESSAGE_EXISTS );
    }

    @Test
    public void shouldNotAllowOpeningADatabaseWithNodeKeyInCommunityEdition()
    {
        assertThatCommunityCannotStartOnEnterpriseOnlyConstraint( "CREATE CONSTRAINT ON (n:Draconian) ASSERT (n.required) IS NODE KEY",
                StandardConstraintSemantics.ERROR_MESSAGE_NODE_KEY );
    }

    @Test
    public void shouldAllowOpeningADatabaseWithUniqueConstraintInCommunityEdition()
    {
        assertThatCommunityCanStartOnNormalConstraint( "CREATE CONSTRAINT ON (n:Draconian) ASSERT (n.required) IS UNIQUE" );
    }

    private void assertThatCommunityCanStartOnNormalConstraint( String constraintCreationQuery )
    {
        // given
        GraphDatabaseService graphDb = new EnterpriseGraphDatabaseFactory().newEmbeddedDatabase( dir.storeDir() );
        try
        {
            graphDb.execute( constraintCreationQuery );
        }
        finally
        {
            graphDb.shutdown();
        }
        graphDb = null;

        // when
        try
        {
            graphDb = new TestGraphDatabaseFactory().newEmbeddedDatabase( dir.storeDir() );
            // Should not get exception
        }
        finally
        {
            if ( graphDb != null )
            {
                graphDb.shutdown();
            }
        }
    }

    private void assertThatCommunityCannotStartOnEnterpriseOnlyConstraint( String constraintCreationQuery, String errorMessage )
    {
        // given
        GraphDatabaseService graphDb = new EnterpriseGraphDatabaseFactory().newEmbeddedDatabase( dir.storeDir() );
        try
        {
            graphDb.execute( constraintCreationQuery );
        }
        finally
        {
            graphDb.shutdown();
        }
        graphDb = null;

        // when
        try
        {
            graphDb = new TestGraphDatabaseFactory().newEmbeddedDatabase( dir.storeDir() );
            fail( "should have failed to start!" );
        }
        // then
        catch ( Exception e )
        {
            Throwable error = Exceptions.rootCause( e );
            assertThat( error, instanceOf( IllegalStateException.class ) );
            assertEquals( errorMessage, error.getMessage() );
        }
        finally
        {
            if ( graphDb != null )
            {
                graphDb.shutdown();
            }
        }
    }
}
