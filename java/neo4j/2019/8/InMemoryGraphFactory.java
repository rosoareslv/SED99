/*
 * Copyright (c) 2002-2019 "Neo4j,"
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
package org.neo4j.server.database;

import java.io.File;

import org.neo4j.collection.Dependencies;
import org.neo4j.common.DependencyResolver;
import org.neo4j.configuration.Config;
import org.neo4j.configuration.GraphDatabaseSettings;
import org.neo4j.configuration.connectors.BoltConnector;
import org.neo4j.configuration.helpers.SocketAddress;
import org.neo4j.dbms.api.DatabaseManagementService;
import org.neo4j.graphdb.facade.ExternalDependencies;
import org.neo4j.test.TestDatabaseManagementServiceBuilder;

import static org.neo4j.configuration.GraphDatabaseSettings.DEFAULT_DATABASE_NAME;

public class InMemoryGraphFactory implements GraphFactory
{
    @Override
    public DatabaseManagementService newDatabaseManagementService( Config config, ExternalDependencies dependencies )
    {
        File storeDir = new File( config.get( GraphDatabaseSettings.databases_root_path ).toFile(), DEFAULT_DATABASE_NAME );
        DependencyResolver externalDependencies = dependencies.dependencies() != null ? dependencies.dependencies() : new Dependencies();
        return new TestDatabaseManagementServiceBuilder( storeDir )
                .setExtensions( dependencies.extensions() )
                .setMonitors( dependencies.monitors() )
                .noOpSystemGraphInitializer()
                .setExternalDependencies( externalDependencies )
                .impermanent()
                .setConfig( BoltConnector.listen_address, new SocketAddress( "localhost", 0 ) )
                .setConfig( BoltConnector.enabled, true )
                .setConfig( config )
                .build();
    }
}
