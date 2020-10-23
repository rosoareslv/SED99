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

import java.util.SortedMap;
import java.util.TreeMap;
import java.util.function.Supplier;

import org.neo4j.kernel.impl.annotations.Documented;
import org.neo4j.kernel.impl.api.DefaultTransactionTracer;
import org.neo4j.kernel.impl.api.LogRotationMonitor;
import org.neo4j.kernel.lifecycle.LifecycleAdapter;
import org.neo4j.kernel.monitoring.Monitors;
import org.neo4j.metrics.output.EventReporter;

import static com.codahale.metrics.MetricRegistry.name;
import static java.util.Collections.emptySortedMap;

@Documented( ".Database LogRotation Metrics" )
public class LogRotationMetrics extends LifecycleAdapter
{
    private static final String LOG_ROTATION_PREFIX = "neo4j.log_rotation";

    @Documented( "The total number of transaction log rotations executed so far" )
    public static final String LOG_ROTATION_EVENTS = name( LOG_ROTATION_PREFIX, "events" );
    @Documented( "The total time spent in rotating transaction logs so far" )
    public static final String LOG_ROTATION_TOTAL_TIME = name( LOG_ROTATION_PREFIX, "total_time" );
    @Documented( "The duration of the log rotation event" )
    public static final String LOG_ROTATION_DURATION = name( LOG_ROTATION_PREFIX, "log_rotation_duration" );

    private final MetricRegistry registry;
    private final Monitors monitors;
    private final Supplier<LogRotationMonitor> logRotationMonitorSupplier;
    private final DefaultTransactionTracer.Monitor listener;

    public LogRotationMetrics( EventReporter reporter, MetricRegistry registry,
            Monitors monitors, Supplier<LogRotationMonitor> logRotationMonitorSupplier )
    {
        this.registry = registry;
        this.monitors = monitors;
        this.logRotationMonitorSupplier = logRotationMonitorSupplier;
        this.listener = durationMillis ->
        {
            final SortedMap<String,Gauge> gauges = new TreeMap<>();
            gauges.put( LOG_ROTATION_DURATION, () -> durationMillis );
            reporter.report( gauges, emptySortedMap(), emptySortedMap(), emptySortedMap(), emptySortedMap() );
        };
    }

    @Override
    public void start()
    {
        monitors.addMonitorListener( listener );

        LogRotationMonitor monitor = this.logRotationMonitorSupplier.get();
        registry.register( LOG_ROTATION_EVENTS, (Gauge<Long>) monitor::numberOfLogRotationEvents );
        registry.register( LOG_ROTATION_TOTAL_TIME,
                (Gauge<Long>) monitor::logRotationAccumulatedTotalTimeMillis );
    }

    @Override
    public void stop()
    {
        monitors.removeMonitorListener( listener );

        registry.remove( LOG_ROTATION_EVENTS );
        registry.remove( LOG_ROTATION_TOTAL_TIME );
    }
}
