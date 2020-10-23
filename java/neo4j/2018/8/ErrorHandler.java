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

import java.util.ArrayList;
import java.util.List;

public class ErrorHandler implements AutoCloseable
{
    private final List<Throwable> throwables = new ArrayList<>();
    private final String message;

    /**
     * Ensures each action is executed. Any throwables will be saved and thrown after all actions have been executed. The first caught throwable will be cause
     * and any other will be added as suppressed.
     *
     * @param description The exception message if any are thrown.
     * @param actions Throwing runnables to execute.
     * @throws RuntimeException thrown if any action throws after all have been executed.
     */
    public static void runAll( String description, ThrowingRunnable... actions ) throws RuntimeException
    {
        try ( ErrorHandler errorHandler = new ErrorHandler( description ) )
        {
            for ( ThrowingRunnable action : actions )
            {
                try
                {
                    action.run();
                }
                catch ( Throwable e )
                {
                    errorHandler.add( e );
                }
            }
        }
    }

    public ErrorHandler( String message )
    {
        this.message = message;
    }

    public void add( Throwable throwable )
    {
        throwables.add( throwable );
    }

    @Override
    public void close() throws RuntimeException
    {
        throwIfException();
    }

    private void throwIfException()
    {
        if ( !throwables.isEmpty() )
        {
            RuntimeException runtimeException = null;
            for ( Throwable throwable : throwables )
            {
                if ( runtimeException == null )
                {
                    runtimeException = new RuntimeException( message, throwable );
                }
                else
                {
                    runtimeException.addSuppressed( throwable );
                }
            }
            throw runtimeException;
        }
    }

    public interface ThrowingRunnable
    {
        void run() throws Throwable;
    }
}
