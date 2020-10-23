/*
 * Copyright (c) 2002-2016 "Neo Technology,"
 * Network Engine for Objects in Lund AB [http://neotechnology.com]
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
package org.neo4j.server;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.IOException;
import java.util.Map;

import org.neo4j.bolt.v1.transport.socket.client.SecureSocketConnection;
import org.neo4j.helpers.HostnamePort;
import org.neo4j.server.rest.JaxRsResponse;
import org.neo4j.server.rest.RestRequest;
import org.neo4j.server.rest.domain.JsonHelper;
import org.neo4j.test.server.ExclusiveServerTestBase;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertFalse;
import static org.neo4j.graphdb.factory.GraphDatabaseSettings.boltConnector;
import static org.neo4j.server.helpers.CommunityServerBuilder.server;

public class BoltIT extends ExclusiveServerTestBase
{
    @Rule
    public TemporaryFolder tmpDir = new TemporaryFolder();

    private CommunityNeoServer server;

    @After
    public void stopTheServer()
    {
        if ( server != null )
        {
            server.stop();
        }
    }

    @Test
    public void shouldLaunchBolt() throws Throwable
    {
        // When I run Neo4j with Bolt enabled
        server = server().withProperty( boltConnector( "0" ).type.name(), "BOLT" )
                .withProperty( boltConnector( "0" ).enabled.name(), "true" )
                .withProperty( boltConnector( "0" ).encryption_level.name(), "REQUIRED" )
                .usingDataDir( tmpDir.getRoot().getAbsolutePath() ).build();
        server.start();

        // Then
        assertEventuallyServerResponds( "localhost", 7687 );
    }

    @Test
    public void shouldBeAbleToSpecifyHostAndPort() throws Throwable
    {
        // When
        startServerWithBoltEnabled();

        // Then
        assertEventuallyServerResponds( "localhost", 7687 );
    }

    @Test
    public void boltAddressShouldAppearToComeFromTheSameOriginAsTheHttpAddressEvenThoughThisIsMorallyHazardous()
            throws Throwable
    {
        // Given
        String host = "neo4j.com";
        startServerWithBoltEnabled();
        RestRequest request = new RestRequest( server.baseUri() ).host( host );

        // When
        JaxRsResponse response = request.get();

        // Then
        Map<String,Object> map = JsonHelper.jsonToMap( response.getEntity() );
        assertThat( String.valueOf( map.get( "bolt" ) ), containsString( "bolt://" + host ) );
        assertFalse( String.valueOf( map.get( "bolt" ) ).contains( "bolt://bolt://" ) );
    }

    @Test
    public void boltAddressShouldComeFromConfigWhenTheListenConfigIsNotLocalhost() throws Throwable
    {
        // Given
        String host = "neo4j.com";

        startServerWithBoltEnabled( host, 9999, "localhost", 7687 );
        RestRequest request = new RestRequest( server.baseUri() ).host( host );

        // When
        JaxRsResponse response = request.get();

        // Then
        Map<String,Object> map = JsonHelper.jsonToMap( response.getEntity() );
        assertThat( String.valueOf( map.get( "bolt" ) ), containsString( "bolt://" + host + ":" + 9999 ) );
    }

    @Test
    public void boltPortShouldComeFromConfigButHostShouldMatchHttpHostHeaderWhenConfigIsLocalhostOrEmptyEvenThoughThisIsMorallyHazardous()
            throws Throwable
    {
        // Given
        String host = "neo4j.com";
        startServerWithBoltEnabled( "localhost", 9999, "localhost", 7687 );
        RestRequest request = new RestRequest( server.baseUri() ).host( host );

        // When
        JaxRsResponse response = request.get();

        // Then
        Map<String,Object> map = JsonHelper.jsonToMap( response.getEntity() );
        assertThat( String.valueOf( map.get( "bolt" ) ), containsString( "bolt://" + host + ":9999" ) );
    }

    private void startServerWithBoltEnabled() throws IOException
    {
        startServerWithBoltEnabled( "localhost", 7687, "localhost", 7687 );
    }

    private void startServerWithBoltEnabled( String advertisedHost, int advertisedPort, String listenHost,
            int listenPort ) throws IOException
    {
        server = server().withProperty( boltConnector( "0" ).type.name(), "BOLT" )
                .withProperty( boltConnector( "0" ).enabled.name(), "true" )
                .withProperty( boltConnector( "0" ).encryption_level.name(), "REQUIRED" )
                .withProperty( boltConnector( "0" ).advertised_address.name(), advertisedHost + ":" + advertisedPort )
                .withProperty( boltConnector( "0" ).listen_address.name(), listenHost + ":" + listenPort )
                .usingDataDir( tmpDir.getRoot().getAbsolutePath() ).build();
        server.start();
    }

    private void assertEventuallyServerResponds( String host, int port ) throws Exception
    {
        SecureSocketConnection conn = new SecureSocketConnection();
        conn.connect( new HostnamePort( host, port ) );
        conn.send(
                new byte[]{(byte) 0x60, (byte) 0x60, (byte) 0xB0, (byte) 0x17, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        0, 0} );
        assertThat( conn.recv( 4 ), equalTo( new byte[]{0, 0, 0, 1} ) );
    }
}
