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

import org.neo4j.dbms.database.DatabaseContext;
import org.neo4j.dbms.database.DatabaseManager;
import org.neo4j.graphdb.DependencyResolver;
import org.neo4j.graphdb.factory.GraphDatabaseSettings;
import org.neo4j.internal.kernel.api.Kernel;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.database.Database;
import org.neo4j.kernel.impl.core.EmbeddedProxySPI;
import org.neo4j.kernel.impl.util.DefaultValueMapper;
import org.neo4j.kernel.monitoring.Monitors;
import org.neo4j.scheduler.JobScheduler;
import org.neo4j.values.ValueMapper;

public class DataCollector implements AutoCloseable
{
    private final DatabaseManager databaseManager;
    private final Config config;
    final JobScheduler jobScheduler;
    final QueryCollector queryCollector;

    DataCollector( DatabaseManager databaseManager,
                   Config config,
                   JobScheduler jobScheduler,
                   Monitors monitors )
    {
        this.databaseManager = databaseManager;
        this.config = config;
        this.jobScheduler = jobScheduler;
        this.queryCollector = new QueryCollector();
        monitors.addMonitorListener( queryCollector );
    }

    @Override
    public void close()
    {
        // intended to eventually be used to stop any ongoing collection
    }

    public Kernel getKernel()
    {
        return databaseManager.getDatabaseContext( config.get( GraphDatabaseSettings.active_database ) )
                .map( DatabaseContext::getDatabase )
                .map( Database::getKernel )
                .orElseThrow( () -> new IllegalStateException( "Active database not found." ) );
    }

    public ValueMapper.JavaMapper getValueMapper()
    {
        return databaseManager.getDatabaseContext( config.get( GraphDatabaseSettings.active_database ) )
                .map( DatabaseContext::getDatabase )
                .map( database -> {
                    EmbeddedProxySPI spi = database.getDependencyResolver()
                            .resolveDependency( EmbeddedProxySPI.class, DependencyResolver.SelectionStrategy.SINGLE );
                    return new DefaultValueMapper( spi );
                } )
                .orElseThrow( () -> new IllegalStateException( "Active database not found." ) );
    }
}
