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
package org.neo4j.causalclustering.discovery;

import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;

import org.neo4j.causalclustering.core.CausalClusteringSettings;
import org.neo4j.helpers.AdvertisedSocketAddress;
import org.neo4j.kernel.configuration.Config;

import static java.util.Collections.emptySet;

public class CoreServerInfo implements DiscoveryServerInfo
{
    private final AdvertisedSocketAddress raftServer;
    private final AdvertisedSocketAddress catchupServer;
    private final ClientConnectorAddresses clientConnectorAddresses;
    private final Set<String> groups;
    private final String dbName;
    private final boolean refuseToBeLeader;

    public CoreServerInfo( AdvertisedSocketAddress raftServer, AdvertisedSocketAddress catchupServer,
            ClientConnectorAddresses clientConnectors, String dbName, boolean refuseToBeLeader )
    {
        this( raftServer, catchupServer, clientConnectors, emptySet(), dbName, refuseToBeLeader );
    }

    public CoreServerInfo( AdvertisedSocketAddress raftServer, AdvertisedSocketAddress catchupServer,
            ClientConnectorAddresses clientConnectorAddresses, Set<String> groups, String dbName, boolean refuseToBeLeader )
    {
        this.raftServer = raftServer;
        this.catchupServer = catchupServer;
        this.clientConnectorAddresses = clientConnectorAddresses;
        this.groups = groups;
        this.dbName = dbName;
        this.refuseToBeLeader = refuseToBeLeader;
    }

    @Override
    public String getDatabaseName()
    {
        return dbName;
    }

    public AdvertisedSocketAddress getRaftServer()
    {
        return raftServer;
    }

    @Override
    public AdvertisedSocketAddress getCatchupServer()
    {
        return catchupServer;
    }

    @Override
    public ClientConnectorAddresses connectors()
    {
        return clientConnectorAddresses;
    }

    @Override
    public Set<String> groups()
    {
        return groups;
    }

    public boolean refusesToBeLeader()
    {
        return refuseToBeLeader;
    }

    @Override
    public String toString()
    {
        return "CoreServerInfo{" +
               "raftServer=" + raftServer +
               ", catchupServer=" + catchupServer +
               ", clientConnectorAddresses=" + clientConnectorAddresses +
               ", groups=" + groups +
               ", database=" + dbName +
               ", refuseToBeLeader=" + refuseToBeLeader +
               '}';
    }

    public static CoreServerInfo from( Config config )
    {
        AdvertisedSocketAddress raftAddress = config.get( CausalClusteringSettings.raft_advertised_address );
        AdvertisedSocketAddress transactionSource = config.get( CausalClusteringSettings.transaction_advertised_address );
        ClientConnectorAddresses clientConnectorAddresses = ClientConnectorAddresses.extractFromConfig( config );
        String dbName = config.get( CausalClusteringSettings.database );
        List<String> groupList = config.get( CausalClusteringSettings.server_groups );
        Set<String> groups = new HashSet<>( groupList );
        boolean refuseToBeLeader = config.get( CausalClusteringSettings.refuse_to_be_leader );

        return new CoreServerInfo( raftAddress, transactionSource, clientConnectorAddresses, groups, dbName, refuseToBeLeader );
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
        CoreServerInfo that = (CoreServerInfo) o;
        return refuseToBeLeader == that.refuseToBeLeader && Objects.equals( raftServer, that.raftServer ) &&
                Objects.equals( catchupServer, that.catchupServer ) && Objects.equals( clientConnectorAddresses, that.clientConnectorAddresses ) &&
                Objects.equals( groups, that.groups ) && Objects.equals( dbName, that.dbName );
    }

    @Override
    public int hashCode()
    {

        return Objects.hash( raftServer, catchupServer, clientConnectorAddresses, groups, dbName, refuseToBeLeader );
    }
}
