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

import java.time.Duration;
import java.util.concurrent.TimeUnit;

/**
 * Exponential backoff strategy helper class. Exponent is always 2.
 */
public class ExponentialBackoffStrategy implements TimeoutStrategy
{
    private final long initialBackoffTimeMillis;
    private final long upperBoundBackoffTimeMillis;

    public ExponentialBackoffStrategy( long initialBackoffTime, long upperBoundBackoffTime, TimeUnit timeUnit )
    {
        assert initialBackoffTime <= upperBoundBackoffTime;

        this.initialBackoffTimeMillis = timeUnit.toMillis( initialBackoffTime );
        this.upperBoundBackoffTimeMillis = timeUnit.toMillis( upperBoundBackoffTime );
    }

    public ExponentialBackoffStrategy( Duration initialBackoffTime, Duration upperBoundBackoffTime )
    {
        this( initialBackoffTime.toMillis(), upperBoundBackoffTime.toMillis(), TimeUnit.MILLISECONDS );
    }

    @Override
    public Timeout newTimeout()
    {
        return new Timeout()
        {
            private long backoffTimeMillis = initialBackoffTimeMillis;

            @Override
            public long getMillis()
            {
                return backoffTimeMillis;
            }

            @Override
            public void increment()
            {
                backoffTimeMillis = Math.min( backoffTimeMillis * 2, upperBoundBackoffTimeMillis );
            }
        };
    }
}
