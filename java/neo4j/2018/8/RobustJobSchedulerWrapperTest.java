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
package org.neo4j.causalclustering.helper;

import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;

import java.util.concurrent.atomic.AtomicInteger;

import org.neo4j.scheduler.Group;
import org.neo4j.scheduler.JobScheduler;
import org.neo4j.scheduler.JobHandle;
import org.neo4j.kernel.impl.scheduler.CentralJobScheduler;
import org.neo4j.kernel.lifecycle.LifeRule;
import org.neo4j.logging.Log;

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.timeout;
import static org.mockito.Mockito.verify;
import static org.neo4j.test.assertion.Assert.assertEventually;

public class RobustJobSchedulerWrapperTest
{
    private final int DEFAULT_TIMEOUT_MS = 5000;

    @Rule
    public LifeRule schedulerLife = new LifeRule( true );
    private final JobScheduler actualScheduler = new CentralJobScheduler();

    private final Log log = mock( Log.class );

    @Before
    public void setup()
    {
        schedulerLife.add( actualScheduler );
    }

    @Test
    public void oneOffJobWithExceptionShouldLog() throws Exception
    {
        // given
        Log log = mock( Log.class );
        RobustJobSchedulerWrapper robustWrapper = new RobustJobSchedulerWrapper( actualScheduler, log );

        AtomicInteger count = new AtomicInteger();
        IllegalStateException e = new IllegalStateException();

        // when
        JobHandle jobHandle = robustWrapper.schedule( Group.TOPOLOGY_HEALTH, 100, () ->
        {
            count.incrementAndGet();
            throw e;
        } );

        // then
        assertEventually( "run count", count::get, Matchers.equalTo( 1 ), DEFAULT_TIMEOUT_MS, MILLISECONDS );
        jobHandle.waitTermination();
        verify( log, timeout( DEFAULT_TIMEOUT_MS ).times( 1 ) ).warn( "Uncaught exception", e );
    }

    @Test
    public void recurringJobWithExceptionShouldKeepRunning() throws Exception
    {
        // given
        RobustJobSchedulerWrapper robustWrapper = new RobustJobSchedulerWrapper( actualScheduler, log );

        AtomicInteger count = new AtomicInteger();
        IllegalStateException e = new IllegalStateException();

        // when
        int nRuns = 100;
        JobHandle jobHandle = robustWrapper.scheduleRecurring( Group.TOPOLOGY_REFRESH, 1, () ->
        {
            if ( count.get() < nRuns )
            {
                count.incrementAndGet();
                throw e;
            }
        } );

        // then
        assertEventually( "run count", count::get, Matchers.equalTo( nRuns ), DEFAULT_TIMEOUT_MS, MILLISECONDS );
        jobHandle.cancel( true );
        verify( log, timeout( DEFAULT_TIMEOUT_MS ).times( nRuns ) ).warn( "Uncaught exception", e );
    }

    @Test
    public void recurringJobWithErrorShouldStop() throws Exception
    {
        // given
        RobustJobSchedulerWrapper robustWrapper = new RobustJobSchedulerWrapper( actualScheduler, log );

        AtomicInteger count = new AtomicInteger();
        Error e = new Error();

        // when
        JobHandle jobHandle = robustWrapper.scheduleRecurring( Group.TOPOLOGY_REFRESH, 1, () ->
        {
            count.incrementAndGet();
            throw e;
        } );

        // when
        Thread.sleep( 50 ); // should not keep increasing during this time

        // then
        assertEventually( "run count", count::get, Matchers.equalTo( 1 ), DEFAULT_TIMEOUT_MS, MILLISECONDS );
        jobHandle.cancel( true );
        verify( log, timeout( DEFAULT_TIMEOUT_MS ).times( 1 ) ).error( "Uncaught error rethrown", e );
    }
}
