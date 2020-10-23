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

import org.junit.Test;

import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

public class CompositeSuspendableTest
{

    @Test
    public void shouldEnableAllAndDisableAllEvenIfTheyThrow()
    {
        AtomicInteger count = new AtomicInteger();
        CompositeSuspendable compositeSuspendable = new CompositeSuspendable();
        int amountOfSuspendable = 3;
        for ( int i = 0; i < amountOfSuspendable; i++ )
        {
            compositeSuspendable.add( getSuspendable( count ) );
        }

        try
        {
            compositeSuspendable.enable();
            fail();
        }
        catch ( RuntimeException ignore )
        {

        }

        assertEquals( amountOfSuspendable, count.get() );

        try
        {
            compositeSuspendable.disable();
            fail();
        }
        catch ( RuntimeException ignore )
        {

        }

        assertEquals( 0, count.get() );
    }

    private Suspendable getSuspendable( AtomicInteger count )
    {
        return new Suspendable()
        {
            @Override
            public void enable()
            {
                count.incrementAndGet();
                fail();
            }

            @Override
            public void disable()
            {
                count.decrementAndGet();
                fail();
            }
        };
    }
}
