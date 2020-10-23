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
package org.neo4j.bolt.transport;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;

import java.util.Collection;

import org.neo4j.bolt.testing.TransportTestUtil;
import org.neo4j.bolt.testing.client.SocketConnection;
import org.neo4j.bolt.testing.client.TransportConnection;
import org.neo4j.bolt.testing.client.WebSocketConnection;
import org.neo4j.configuration.connectors.BoltConnector;
import org.neo4j.function.Factory;
import org.neo4j.internal.helpers.HostnamePort;

import static java.util.Arrays.asList;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.neo4j.bolt.testing.TransportTestUtil.eventuallyDisconnects;
import static org.neo4j.configuration.connectors.BoltConnector.EncryptionLevel.REQUIRED;

@RunWith( Parameterized.class )
public class RequiredTransportEncryptionIT
{
    @Rule
    public Neo4jWithSocket server = new Neo4jWithSocket( getClass(),
            settings -> settings.put( BoltConnector.encryption_level, REQUIRED ) );

    @Parameterized.Parameter( 0 )
    public Factory<TransportConnection> cf;

    private HostnamePort address;
    private TransportConnection client;
    private TransportTestUtil util;

    @Parameterized.Parameters
    public static Collection<Factory<TransportConnection>> transports()
    {
        return asList( SocketConnection::new, WebSocketConnection::new );
    }

    @Before
    public void setup()
    {
        this.client = cf.newInstance();
        this.address = server.lookupDefaultConnector();
        this.util = new TransportTestUtil();
    }

    @After
    public void teardown() throws Exception
    {
        if ( client != null )
        {
            client.disconnect();
        }
    }

    @Test
    public void shouldCloseUnencryptedConnectionOnHandshakeWhenEncryptionIsRequired() throws Throwable
    {
        // When
        client.connect( address ).send( util.defaultAcceptedVersions() );

        assertThat( client, eventuallyDisconnects() );
    }
}
