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
package org.neo4j.causalclustering.scenarios;

import org.neo4j.causalclustering.discovery.Cluster;
import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Label;
import org.neo4j.graphdb.Node;
import org.neo4j.graphdb.Relationship;
import org.neo4j.graphdb.RelationshipType;

import static org.neo4j.graphdb.Label.label;

public class SampleData
{
    private static final Label LABEL = label( "ExampleNode" );
    private static final String PROPERTY_KEY = "prop";

    private SampleData()
    {
    }

    public static void createSomeData( int items, Cluster<?> cluster ) throws Exception
    {
        for ( int i = 0; i < items; i++ )
        {
            cluster.coreTx( ( db, tx ) ->
            {
                Node node = db.createNode( LABEL );
                node.setProperty( "foobar", "baz_bat" );
                tx.success();
            } );
        }
    }

    static void createData( GraphDatabaseService db, int size )
    {
        for ( int i = 0; i < size; i++ )
        {
            Node node1 = db.createNode( LABEL );
            Node node2 = db.createNode( LABEL );

            node1.setProperty( PROPERTY_KEY, "svej" + i );
            node2.setProperty( "tjabba", "tjena" );
            node1.setProperty( "foobar", "baz_bat" );
            node2.setProperty( "foobar", "baz_bat" );

            Relationship rel = node1.createRelationshipTo( node2, RelationshipType.withName( "halla" ) );
            rel.setProperty( "this", "that" );
        }
    }

    static void createSchema( GraphDatabaseService db )
    {
        db.schema().constraintFor( LABEL ).assertPropertyIsUnique( PROPERTY_KEY ).create();
    }
}
