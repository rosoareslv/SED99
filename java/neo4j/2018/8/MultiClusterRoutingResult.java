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
package org.neo4j.causalclustering.routing.multi_cluster;

import java.util.List;
import java.util.Map;
import java.util.Objects;

import org.neo4j.causalclustering.routing.Endpoint;
import org.neo4j.causalclustering.routing.RoutingResult;

/**
 * Simple struct containing the the result of multi-cluster routing procedure execution.
 */
public class MultiClusterRoutingResult implements RoutingResult
{
    private final Map<String,List<Endpoint>> routers;
    private final long timeToLiveMillis;

    public MultiClusterRoutingResult( Map<String,List<Endpoint>> routers, long timeToLiveMillis )
    {
        this.routers = routers;
        this.timeToLiveMillis = timeToLiveMillis;
    }

    public Map<String,List<Endpoint>> routers()
    {
        return routers;
    }

    @Override
    public long ttlMillis()
    {
        return timeToLiveMillis;
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
        MultiClusterRoutingResult that = (MultiClusterRoutingResult) o;
        return timeToLiveMillis == that.timeToLiveMillis && Objects.equals( routers, that.routers );
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( routers, timeToLiveMillis );
    }
}

