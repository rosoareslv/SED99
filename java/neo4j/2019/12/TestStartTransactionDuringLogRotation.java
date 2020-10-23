/*
 * Copyright (c) 2002-2019 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j.
 *
 * Neo4j is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package synchronization;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicBoolean;

import org.neo4j.configuration.GraphDatabaseSettings;
import org.neo4j.dbms.api.DatabaseManagementServiceBuilder;
import org.neo4j.graphdb.Label;
import org.neo4j.graphdb.Transaction;
import org.neo4j.io.ByteUnit;
import org.neo4j.kernel.impl.transaction.log.rotation.LogRotation;
import org.neo4j.kernel.impl.transaction.log.rotation.monitor.LogRotationMonitor;
import org.neo4j.kernel.impl.transaction.log.rotation.monitor.LogRotationMonitorAdapter;
import org.neo4j.kernel.impl.transaction.tracing.LogAppendEvent;
import org.neo4j.kernel.internal.GraphDatabaseAPI;
import org.neo4j.monitoring.Monitors;
import org.neo4j.test.OtherThreadExecutor.WorkerCommand;
import org.neo4j.test.rule.DbmsRule;
import org.neo4j.test.rule.EmbeddedDbmsRule;
import org.neo4j.test.rule.OtherThreadRule;

public class TestStartTransactionDuringLogRotation
{
    @Rule
    public DbmsRule db = new EmbeddedDbmsRule()
    {
        @Override
        protected void configure( DatabaseManagementServiceBuilder databaseFactory )
        {
            super.configure( databaseFactory );
            databaseFactory.setConfig( GraphDatabaseSettings.logical_log_rotation_threshold, ByteUnit.mebiBytes( 1 ) );
        }
    };
    @Rule
    public final OtherThreadRule<Void> t2 = new OtherThreadRule<>( "T2-" + getClass().getName() );

    private ExecutorService executor;
    private CountDownLatch startLogRotationLatch;
    private CountDownLatch completeLogRotationLatch;
    private AtomicBoolean writerStopped;
    private Monitors monitors;
    private LogRotationMonitor rotationListener;
    private Label label;
    private Future<Void> rotationFuture;

    @Before
    public void setUp() throws InterruptedException
    {
        executor = Executors.newCachedThreadPool();
        startLogRotationLatch = new CountDownLatch( 1 );
        completeLogRotationLatch = new CountDownLatch( 1 );
        writerStopped = new AtomicBoolean();
        monitors = db.getDependencyResolver().resolveDependency( Monitors.class );

        rotationListener = new LogRotationMonitorAdapter()
        {
            @Override
            public void startRotation( long currentLogVersion )
            {
                startLogRotationLatch.countDown();
                try
                {
                    completeLogRotationLatch.await();
                }
                catch ( InterruptedException e )
                {
                    throw new RuntimeException( e );
                }
            }
        };

        monitors.addMonitorListener( rotationListener );
        label = Label.label( "Label" );

        rotationFuture = t2.execute( forceLogRotation( db ) );

        // Waiting for the writer task to start a log rotation
        startLogRotationLatch.await();

        // Then we should be able to start a transaction, though perhaps not be able to finish it.
        // This is what the individual test methods will be doing.
        // The test passes when transaction.close completes within the test timeout, that is, it didn't deadlock.
    }

    private WorkerCommand<Void,Void> forceLogRotation( GraphDatabaseAPI db )
    {
        return state ->
        {
            try ( Transaction tx = db.beginTx() )
            {
                tx.createNode( label ).setProperty( "a", 1 );
                tx.commit();
            }

            db.getDependencyResolver().resolveDependency( LogRotation.class ).rotateLogFile( LogAppendEvent.NULL );
            return null;
        };
    }

    @After
    public void tearDown() throws Exception
    {
        rotationFuture.get();
        writerStopped.set( true );
        executor.shutdown();
    }

    @Test( timeout = 10000 )
    public void logRotationMustNotObstructStartingReadTransaction()
    {
        try ( Transaction tx = db.beginTx() )
        {
            tx.getNodeById( 0 );
            completeLogRotationLatch.countDown();
            tx.commit();
        }
    }

    @Test( timeout = 10000 )
    public void logRotationMustNotObstructStartingWriteTransaction()
    {
        try ( Transaction tx = db.beginTx() )
        {
            tx.createNode();
            completeLogRotationLatch.countDown();
            tx.commit();
        }
    }
}
