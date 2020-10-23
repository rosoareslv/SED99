/*
 * Copyright (c) 2002-2018 "Neo4j,"
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
package org.neo4j.internal.collector;

import org.junit.jupiter.api.Test;

import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Supplier;

import static org.junit.jupiter.api.Assertions.*;

class CollectorStateMachineTest
{
    @Test
    void shouldHandleStress() throws ExecutionException, InterruptedException
    {
        // given
        int n = 1000;
        TestStateMachine stateMachine = new TestStateMachine();
        ExecutorService executor = Executors.newFixedThreadPool( 3 );

        // when
        Future<?> collect = executor.submit( stress( n, stateMachine::collect ) );
        Future<?> stop = executor.submit( stress( n, stateMachine::stop ) );
        Future<?> clear = executor.submit( stress( n, stateMachine::clear ) );
        Future<?> status = executor.submit( stress( n, stateMachine::status ) );

        // then without illegal transitions or exceptions
        collect.get();
        stop.get();
        clear.get();
        status.get();
        executor.shutdown();
    }

    private <T> Runnable stress( int n, Supplier<T> action )
    {
        return () -> {
            for ( int i = 0; i < n; i++ )
            {
                action.get();
            }
        };
    }

    static class TestStateMachine extends CollectorStateMachine<String>
    {
        enum State
        {
            IDLE,
            COLLECTING
        }

        volatile State state = State.IDLE;

        @Override
        Result doCollect()
        {
            assertSame( state, State.IDLE );
            state = State.COLLECTING;
            return null;
        }

        @Override
        Result doStop()
        {
            assertSame( state, State.COLLECTING );
            state = State.IDLE;
            return null;
        }

        @Override
        Result doClear()
        {
            assertSame( state, State.IDLE );
            return null;
        }

        @Override
        String doGetData()
        {
            assertSame( state, State.IDLE );
            return "Data";
        }
    }
}
