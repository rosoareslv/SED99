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
package org.neo4j.causalclustering.core.state.machines.id;

import java.io.IOException;

import org.neo4j.causalclustering.identity.MemberId;
import org.neo4j.causalclustering.messaging.EndOfStreamException;
import org.neo4j.kernel.impl.store.id.IdType;
import org.neo4j.storageengine.api.ReadableChannel;
import org.neo4j.storageengine.api.WritableChannel;

public class ReplicatedIdAllocationRequestSerializer
{

    private ReplicatedIdAllocationRequestSerializer()
    {
    }

    public static void marshal( ReplicatedIdAllocationRequest idRangeRequest, WritableChannel channel )
            throws IOException
    {
        new MemberId.Marshal().marshal( idRangeRequest.owner(), channel );
        channel.putInt( idRangeRequest.idType().ordinal() );
        channel.putLong( idRangeRequest.idRangeStart() );
        channel.putInt( idRangeRequest.idRangeLength() );
    }

    public static ReplicatedIdAllocationRequest unmarshal( ReadableChannel channel ) throws IOException, EndOfStreamException
    {
        MemberId owner = new MemberId.Marshal().unmarshal( channel );
        IdType idType = IdType.values()[ channel.getInt() ];
        long idRangeStart = channel.getLong();
        int idRangeLength = channel.getInt();

        return new ReplicatedIdAllocationRequest( owner, idType, idRangeStart, idRangeLength );
    }
}
