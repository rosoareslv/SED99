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
package org.neo4j.causalclustering.routing.load_balancing;

import java.util.List;
import java.util.Objects;

import org.neo4j.causalclustering.routing.Endpoint;

/**
 * The outcome of applying a load balancing plugin, which will be used by client
 * software for scheduling work at the endpoints.
 */
public class LoadBalancingResult implements LoadBalancingProcessor.Result
{
    private final List<Endpoint> routeEndpoints;
    private final List<Endpoint> writeEndpoints;
    private final List<Endpoint> readEndpoints;
    private final long timeToLiveMillis;

    public LoadBalancingResult( List<Endpoint> routeEndpoints, List<Endpoint> writeEndpoints,
            List<Endpoint> readEndpoints, long timeToLiveMillis )
    {
        this.routeEndpoints = routeEndpoints;
        this.writeEndpoints = writeEndpoints;
        this.readEndpoints = readEndpoints;
        this.timeToLiveMillis = timeToLiveMillis;
    }

    @Override
    public long ttlMillis()
    {
        return timeToLiveMillis;
    }

    @Override
    public List<Endpoint> routeEndpoints()
    {
        return routeEndpoints;
    }

    @Override
    public List<Endpoint> writeEndpoints()
    {
        return writeEndpoints;
    }

    @Override
    public List<Endpoint> readEndpoints()
    {
        return readEndpoints;
    }

    @Override
    public boolean equals( Object o )
    {
        if ( this == o )
        {
            return true;
        }
        if ( o == null || getClass() != o.getClass() )
        {
            return false;
        }
        LoadBalancingResult that = (LoadBalancingResult) o;
        return timeToLiveMillis == that.timeToLiveMillis &&
               Objects.equals( routeEndpoints, that.routeEndpoints ) &&
               Objects.equals( writeEndpoints, that.writeEndpoints ) &&
               Objects.equals( readEndpoints, that.readEndpoints );
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( routeEndpoints, writeEndpoints, readEndpoints, timeToLiveMillis );
    }

    @Override
    public String toString()
    {
        return "LoadBalancingResult{" +
               "routeEndpoints=" + routeEndpoints +
               ", writeEndpoints=" + writeEndpoints +
               ", readEndpoints=" + readEndpoints +
               ", timeToLiveMillis=" + timeToLiveMillis +
               '}';
    }
}
