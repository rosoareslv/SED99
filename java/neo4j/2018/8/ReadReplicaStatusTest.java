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
package org.neo4j.server.rest.causalclustering;

import org.junit.Before;
import org.junit.Test;

import java.net.URI;
import javax.ws.rs.core.Response;

import org.neo4j.causalclustering.readreplica.ReadReplicaGraphDatabase;
import org.neo4j.server.rest.repr.OutputFormat;
import org.neo4j.server.rest.repr.formats.JsonFormat;

import static javax.ws.rs.core.Response.Status.NOT_FOUND;
import static javax.ws.rs.core.Response.Status.OK;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;

public class ReadReplicaStatusTest
{
    private CausalClusteringStatus status;

    @Before
    public void setup() throws Exception
    {
        OutputFormat output = new OutputFormat( new JsonFormat(), new URI( "http://base.local:1234/" ), null );
        status = CausalClusteringStatusFactory.build( output, mock( ReadReplicaGraphDatabase.class ) );
    }

    @Test
    public void testAnswers()
    {
        // when
        Response available = status.available();
        Response readonly = status.readonly();
        Response writable = status.writable();

        // then
        assertEquals( OK.getStatusCode(), available.getStatus() );
        assertEquals( "true", available.getEntity() );

        assertEquals( OK.getStatusCode(), readonly.getStatus() );
        assertEquals( "true", readonly.getEntity() );

        assertEquals( NOT_FOUND.getStatusCode(), writable.getStatus() );
        assertEquals( "false", writable.getEntity() );
    }
}
