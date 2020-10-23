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

import org.neo4j.function.ThrowingAction;
import org.neo4j.logging.Log;
import org.neo4j.scheduler.Group;
import org.neo4j.scheduler.JobHandle;
import org.neo4j.scheduler.JobScheduler;

import static java.util.concurrent.TimeUnit.MILLISECONDS;

/**
 * A robust job catches and logs any exceptions, but keeps running if the job
 * is a recurring one. Any exceptions also end up in the supplied log instead
 * of falling through to syserr. Remaining Throwables (generally errors) are
 * logged but fall through, so that recurring jobs are stopped and the error
 * gets double visibility.
 */
public class RobustJobSchedulerWrapper
{
    private final JobScheduler delegate;
    private final Log log;

    public RobustJobSchedulerWrapper( JobScheduler delegate, Log log )
    {
        this.delegate = delegate;
        this.log = log;
    }

    public JobHandle schedule( Group group, long delayMillis, ThrowingAction<Exception> action )
    {
        return delegate.schedule( group, () -> withErrorHandling( action ), delayMillis, MILLISECONDS );
    }

    public JobHandle scheduleRecurring( Group group, long periodMillis, ThrowingAction<Exception> action )
    {
        return delegate.scheduleRecurring( group, () -> withErrorHandling( action ), periodMillis, MILLISECONDS );
    }

    /**
     * Last line of defense error handling.
     */
    private void withErrorHandling( ThrowingAction<Exception> action )
    {
        try
        {
            action.apply();
        }
        catch ( Exception e )
        {
            log.warn( "Uncaught exception", e );
        }
        catch ( Throwable t )
        {
            log.error( "Uncaught error rethrown", t );
            throw t;
        }
    }
}
