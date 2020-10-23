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
package org.neo4j.kernel.stresstests.transaction.checkpoint.workload;

import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Transaction;
import org.neo4j.kernel.DeadlockDetectedException;
import org.neo4j.kernel.stresstests.transaction.checkpoint.mutation.RandomMutation;

class Worker implements Runnable
{
    interface Monitor
    {
        void transactionCompleted();
        boolean stop();
        void done();
    }

    private final GraphDatabaseService db;
    private final RandomMutation randomMutation;
    private final Monitor monitor;
    private final int numOpsPerTx;

    Worker( GraphDatabaseService db, RandomMutation randomMutation, Monitor monitor, int numOpsPerTx )
    {
        this.db = db;
        this.randomMutation = randomMutation;
        this.monitor = monitor;
        this.numOpsPerTx = numOpsPerTx;
    }

    @Override
    public void run()
    {
        do
        {
            try ( Transaction tx = db.beginTx() )
            {
                for ( int i = 0; i < numOpsPerTx; i++ )
                {
                    randomMutation.perform();
                }
                tx.success();
            }
            catch ( DeadlockDetectedException ignore )
            {
                // simply give up
            }
            catch ( Exception e )
            {
                // ignore and go on
                e.printStackTrace();
            }

            monitor.transactionCompleted();
        }
        while ( !monitor.stop() );

        monitor.done();
    }
}
