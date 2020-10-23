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
package org.neo4j.cypher.internal.codegen.profiling;

import org.opencypher.v9_0.util.attribution.Id;

import java.util.HashMap;
import java.util.Map;

import org.neo4j.cypher.internal.codegen.QueryExecutionTracer;
import org.neo4j.cypher.internal.planner.v3_5.spi.KernelStatisticProvider;
import org.neo4j.cypher.internal.runtime.compiled.codegen.QueryExecutionEvent;
import org.neo4j.cypher.result.OperatorProfile;
import org.neo4j.cypher.result.QueryProfile;

public class ProfilingTracer implements QueryExecutionTracer, QueryProfile
{
    public interface Clock
    {
        long nanoTime();

        Clock SYSTEM_TIMER = System::nanoTime;
    }

    private static final Data ZERO = new Data();

    private final Clock clock;
    private final KernelStatisticProvider statisticProvider;
    private final Map<Integer, Data> data = new HashMap<>();

    public ProfilingTracer( KernelStatisticProvider statisticProvider )
    {
        this( Clock.SYSTEM_TIMER, statisticProvider );
    }

    ProfilingTracer( Clock clock, KernelStatisticProvider statisticProvider )
    {
        this.clock = clock;
        this.statisticProvider = statisticProvider;
    }

    public OperatorProfile operatorProfile( int query )
    {
        Data value = data.get( query );
        return value == null ? ZERO : value;
    }

    public long timeOf( Id query )
    {
        return operatorProfile( query.x() ).time();
    }

    public long dbHitsOf( Id query )
    {
        return operatorProfile( query.x() ).dbHits();
    }

    public long rowsOf( Id query )
    {
        return operatorProfile( query.x() ).rows();
    }

    @Override
    public QueryExecutionEvent executeOperator( Id queryId )
    {
        Data queryData = this.data.get( queryId.x() );
        if ( queryData == null )
        {
            queryData = new Data();
            this.data.put( queryId.x(), queryData );
        }
        return new ExecutionEvent( clock, statisticProvider, queryData );
    }

    private static class ExecutionEvent implements QueryExecutionEvent
    {
        private final long start;
        private final Clock clock;
        private final KernelStatisticProvider statisticProvider;
        private final Data data;
        private long hitCount;
        private long rowCount;

        ExecutionEvent( Clock clock, KernelStatisticProvider statisticProvider, Data data )
        {
            this.clock = clock;
            this.statisticProvider = statisticProvider;
            this.data = data;
            this.start = clock.nanoTime();
        }

        @Override
        public void close()
        {
            long executionTime = clock.nanoTime() - start;
            long pageCacheHits = statisticProvider.getPageCacheHits();
            long pageCacheFaults = statisticProvider.getPageCacheMisses();
            if ( data != null )
            {
                data.update( executionTime, hitCount, rowCount, pageCacheHits, pageCacheFaults );
            }
        }

        @Override
        public void dbHit()
        {
            hitCount++;
        }

        @Override
        public void row()
        {
            rowCount++;
        }
    }

    private static class Data implements OperatorProfile
    {
        private long time;
        private long hits;
        private long rows;
        private long pageCacheHits;
        private long pageCacheMisses;

        public void update( long time, long hits, long rows, long pageCacheHits, long pageCacheMisses )
        {
            this.time += time;
            this.hits += hits;
            this.rows += rows;
            this.pageCacheHits += pageCacheHits;
            this.pageCacheMisses += pageCacheMisses;
        }

        @Override
        public long time()
        {
            return time;
        }

        @Override
        public long dbHits()
        {
            return hits;
        }

        @Override
        public long rows()
        {
            return rows;
        }

        @Override
        public long pageCacheHits()
        {
            return pageCacheHits;
        }

        @Override
        public long pageCacheMisses()
        {
            return pageCacheMisses;
        }
    }
}
