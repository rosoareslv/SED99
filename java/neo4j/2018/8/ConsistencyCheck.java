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
package org.neo4j.causalclustering.stresstests;

import org.neo4j.causalclustering.discovery.Cluster;
import org.neo4j.causalclustering.discovery.ClusterMember;
import org.neo4j.consistency.ConsistencyCheckService;
import org.neo4j.helpers.collection.Iterables;

import static org.neo4j.consistency.ConsistencyCheckTool.runConsistencyCheckTool;

/**
 * Check the consistency of all the cluster members' stores.
 */
public class ConsistencyCheck extends Validation
{
    private final Cluster<?> cluster;

    ConsistencyCheck( Resources resources )
    {
        super();
        cluster = resources.cluster();
    }

    @Override
    protected void validate() throws Exception
    {
        Iterable<ClusterMember> members = Iterables.concat( cluster.coreMembers(), cluster.readReplicas() );

        for ( ClusterMember member : members )
        {
            String databasePath = member.databaseDirectory().getAbsolutePath();
            ConsistencyCheckService.Result result = runConsistencyCheckTool( new String[]{databasePath}, System.out, System.err );
            if ( !result.isSuccessful() )
            {
                throw new RuntimeException( "Not consistent database in " + databasePath );
            }
        }
    }
}
