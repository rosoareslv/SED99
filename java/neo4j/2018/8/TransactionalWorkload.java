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
package org.neo4j.backup.stresstests;

import java.util.function.Supplier;

import org.neo4j.causalclustering.stresstests.Control;
import org.neo4j.graphdb.DatabaseShutdownException;
import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Label;
import org.neo4j.graphdb.Node;
import org.neo4j.graphdb.Transaction;
import org.neo4j.graphdb.TransactionFailureException;
import org.neo4j.graphdb.TransientFailureException;
import org.neo4j.helper.Workload;

class TransactionalWorkload extends Workload
{
    private static final Label label = Label.label( "Label" );
    private final Supplier<GraphDatabaseService> dbRef;

    TransactionalWorkload( Control control, Supplier<GraphDatabaseService> dbRef )
    {
        super( control );
        this.dbRef = dbRef;
    }

    @Override
    protected void doWork()
    {
        GraphDatabaseService db = dbRef.get();
        try ( Transaction tx = db.beginTx() )
        {
            Node node = db.createNode( label );
            for ( int i = 1; i <= 8; i++ )
            {
                node.setProperty( prop( i ), "let's add some data here so the transaction logs rotate more often..." );
            }
            tx.success();
        }
        catch ( DatabaseShutdownException | TransactionFailureException | TransientFailureException e )
        {
            // whatever let's go on with the workload
        }
    }

    static void setupIndexes( GraphDatabaseService db )
    {
        try ( Transaction tx = db.beginTx() )
        {
            for ( int i = 1; i <= 8; i++ )
            {
                db.schema().indexFor( label ).on( prop( i ) ).create();
            }
            tx.success();
        }
    }

    private static String prop( int i )
    {
        return "prop" + i;
    }
}
