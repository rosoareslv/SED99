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
package org.neo4j.server.modules;

import org.junit.Test;
import org.mockito.ArgumentCaptor;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.neo4j.configuration.Config;
import org.neo4j.logging.NullLogProvider;
import org.neo4j.server.configuration.ServerSettings;
import org.neo4j.server.web.WebServer;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.not;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

public class RESTApiModuleTest
{
    @SuppressWarnings( "unchecked" )
    @Test
    public void shouldRegisterASingleUri()
    {
        // Given
        WebServer webServer = mock( WebServer.class );

        Map<String, String> params = new HashMap<>();
        String path = "/db/data";
        params.put( ServerSettings.rest_api_path.name(), path );
        Config config = Config.defaults( params );

        // When
        RESTApiModule module = new RESTApiModule( webServer, config, NullLogProvider.getInstance() );
        module.start();

        // Then
        ArgumentCaptor<List<Class<?>>> captor = ArgumentCaptor.forClass( List.class );
        verify( webServer ).addJAXRSClasses( captor.capture(), anyString(), any() );
        assertThat( captor.getValue(), not( empty() ) );
    }
}
