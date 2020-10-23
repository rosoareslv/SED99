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
package org.neo4j.causalclustering.core.consensus;

import org.junit.Test;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Semaphore;
import java.util.concurrent.locks.LockSupport;

import org.neo4j.kernel.impl.scheduler.CentralJobScheduler;
import org.neo4j.kernel.lifecycle.Lifespan;
import org.neo4j.logging.NullLogProvider;
import org.neo4j.scheduler.Group;

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import static org.hamcrest.Matchers.lessThan;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

public class ContinuousJobTest
{
    private static final long DEFAULT_TIMEOUT_MS = 15_000;
    private final CentralJobScheduler scheduler = new CentralJobScheduler();

    @Test
    public void shouldRunJobContinuously() throws Throwable
    {
        // given
        CountDownLatch latch = new CountDownLatch( 10 );
        Runnable task = latch::countDown;

        ContinuousJob continuousJob =
                new ContinuousJob( scheduler.threadFactory( Group.RAFT_BATCH_HANDLER ), task, NullLogProvider.getInstance() );

        // when
        try ( Lifespan ignored = new Lifespan( scheduler, continuousJob ) )
        {
            //then
            assertTrue( latch.await( DEFAULT_TIMEOUT_MS, MILLISECONDS ) );
        }
    }

    @Test
    public void shouldTerminateOnStop() throws Exception
    {
        // given: this task is gonna take >20 ms total
        Semaphore semaphore = new Semaphore( -20 );

        Runnable task = () ->
        {
            LockSupport.parkNanos( 1_000_000 ); // 1 ms
            semaphore.release();
        };

        ContinuousJob continuousJob =
                new ContinuousJob( scheduler.threadFactory( Group.RAFT_BATCH_HANDLER ), task, NullLogProvider.getInstance() );

        // when
        long startTime = System.currentTimeMillis();
        try ( Lifespan ignored = new Lifespan( scheduler, continuousJob ) )
        {
            semaphore.acquireUninterruptibly();
        }
        long runningTime = System.currentTimeMillis() - startTime;

        // then
        assertThat( runningTime, lessThan( DEFAULT_TIMEOUT_MS ) );

        //noinspection StatementWithEmptyBody
        while ( semaphore.tryAcquire() )
        {
            // consume all outstanding permits
        }

        // no more permits should be granted
        semaphore.tryAcquire( 10, MILLISECONDS );
    }
}
