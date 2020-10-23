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
package org.neo4j.metrics.source.db;

import com.codahale.metrics.Gauge;
import com.codahale.metrics.MetricRegistry;

import java.util.TreeMap;
import java.util.function.Supplier;

import org.neo4j.kernel.impl.annotations.Documented;
import org.neo4j.kernel.impl.transaction.log.checkpoint.CheckPointerMonitor;
import org.neo4j.kernel.impl.transaction.log.checkpoint.DefaultCheckPointerTracer;
import org.neo4j.kernel.lifecycle.LifecycleAdapter;
import org.neo4j.kernel.monitoring.Monitors;
import org.neo4j.metrics.output.EventReporter;

import static com.codahale.metrics.MetricRegistry.name;
import static java.util.Collections.emptySortedMap;

@Documented( ".Database Checkpointing Metrics" )
public class CheckPointingMetrics extends LifecycleAdapter
{
    private static final String CHECK_POINT_PREFIX = "neo4j.check_point";

    @Documented( "The total number of check point events executed so far" )
    public static final String CHECK_POINT_EVENTS = name( CHECK_POINT_PREFIX, "events" );
    @Documented( "The total time spent in check pointing so far" )
    public static final String CHECK_POINT_TOTAL_TIME = name( CHECK_POINT_PREFIX, "total_time" );
    @Documented( "The duration of the check point event" )
    public static final String CHECK_POINT_DURATION = name( CHECK_POINT_PREFIX, "check_point_duration" );

    private final MetricRegistry registry;
    private final Monitors monitors;
    private final Supplier<CheckPointerMonitor> checkPointerMonitorSupplier;
    private final DefaultCheckPointerTracer.Monitor listener;

    public CheckPointingMetrics( EventReporter reporter, MetricRegistry registry,
            Monitors monitors, Supplier<CheckPointerMonitor> checkPointerMonitorSupplier )
    {
        this.registry = registry;
        this.monitors = monitors;
        this.checkPointerMonitorSupplier = checkPointerMonitorSupplier;
        this.listener = durationMillis ->
        {
            TreeMap<String,Gauge> gauges = new TreeMap<>();
            gauges.put( CHECK_POINT_DURATION, () -> durationMillis );
            reporter.report( gauges, emptySortedMap(), emptySortedMap(), emptySortedMap(), emptySortedMap() );
        };
    }

    @Override
    public void start()
    {
        monitors.addMonitorListener( listener );

        CheckPointerMonitor checkPointerMonitor = checkPointerMonitorSupplier.get();
        registry.register( CHECK_POINT_EVENTS, (Gauge<Long>) checkPointerMonitor::numberOfCheckPointEvents );
        registry.register( CHECK_POINT_TOTAL_TIME,
                (Gauge<Long>) checkPointerMonitor::checkPointAccumulatedTotalTimeMillis );
    }

    @Override
    public void stop()
    {
        monitors.removeMonitorListener( listener );

        registry.remove( CHECK_POINT_EVENTS );
        registry.remove( CHECK_POINT_TOTAL_TIME );
    }
}
