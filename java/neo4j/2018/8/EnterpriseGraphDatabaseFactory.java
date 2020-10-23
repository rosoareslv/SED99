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
package org.neo4j.graphdb.factory;

import java.io.File;

import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.configuration.Settings;
import org.neo4j.kernel.enterprise.EnterpriseGraphDatabase;
import org.neo4j.kernel.impl.factory.Edition;

/**
 * Factory for Neo4j database instances with Enterprise Edition features.
 *
 * @see GraphDatabaseFactory
 */
public class EnterpriseGraphDatabaseFactory extends GraphDatabaseFactory
{
    @Override
    protected GraphDatabaseBuilder.DatabaseCreator createDatabaseCreator( final File storeDir,
                                                                          final GraphDatabaseFactoryState state )
    {
        return new GraphDatabaseBuilder.DatabaseCreator()
        {
            @Override
            public GraphDatabaseService newDatabase( Config config )
            {
                File absoluteStoreDir = storeDir.getAbsoluteFile();
                File databasesRoot = absoluteStoreDir.getParentFile();
                config.augment( GraphDatabaseSettings.ephemeral, Settings.FALSE );
                config.augment( GraphDatabaseSettings.active_database, absoluteStoreDir.getName() );
                config.augment( GraphDatabaseSettings.databases_root_path, databasesRoot.getAbsolutePath() );
                return new EnterpriseGraphDatabase( databasesRoot, config, state.databaseDependencies() );
            }
        };
    }

    @Override
    public String getEdition()
    {
        return Edition.enterprise.toString();
    }
}
