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
package org.neo4j.causalclustering.routing.load_balancing.filters;

import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * Filters the set through each filter of the chain in order.
 */
public class FilterChain<T> implements Filter<T>
{
    private List<Filter<T>> chain;

    public FilterChain( List<Filter<T>> chain )
    {
        this.chain = chain;
    }

    @Override
    public Set<T> apply( Set<T> data )
    {
        for ( Filter<T> filter : chain )
        {
            data = filter.apply( data );
        }
        return data;
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
        FilterChain<?> that = (FilterChain<?>) o;
        return Objects.equals( chain, that.chain );
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( chain );
    }

    @Override
    public String toString()
    {
        return "FilterChain{" +
               "chain=" + chain +
               '}';
    }
}
