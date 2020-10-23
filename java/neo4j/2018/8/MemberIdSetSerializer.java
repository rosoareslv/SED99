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
package org.neo4j.causalclustering.core.consensus.membership;

import java.io.IOException;
import java.util.HashSet;
import java.util.Set;

import org.neo4j.causalclustering.identity.MemberId;
import org.neo4j.causalclustering.messaging.EndOfStreamException;
import org.neo4j.storageengine.api.ReadableChannel;
import org.neo4j.storageengine.api.WritableChannel;

/**
 * Format:
 * ┌────────────────────────────────────────────┐
 * │ memberCount                        4 bytes │
 * │ member 0   ┌──────────────────────────────┐│
 * │            │mostSignificantBits    8 bytes││
 * │            │leastSignificantBits   8 bytes││
 * │            └──────────────────────────────┘│
 * │ ...                                        │
 * │ member n   ┌──────────────────────────────┐│
 * │            │mostSignificantBits    8 bytes││
 * │            │leastSignificantBits   8 bytes││
 * │            └──────────────────────────────┘│
 * └────────────────────────────────────────────┘
 */
public class MemberIdSetSerializer
{
    private MemberIdSetSerializer()
    {
    }

    public static void marshal( MemberIdSet memberSet, WritableChannel channel ) throws IOException
    {
        Set<MemberId> members = memberSet.getMembers();
        channel.putInt( members.size() );

        MemberId.Marshal memberIdMarshal = new MemberId.Marshal();

        for ( MemberId member : members )
        {
            memberIdMarshal.marshal( member, channel );
        }
    }

    public static MemberIdSet unmarshal( ReadableChannel channel ) throws IOException, EndOfStreamException
    {
        HashSet<MemberId> members = new HashSet<>();
        int memberCount = channel.getInt();

        MemberId.Marshal memberIdMarshal = new MemberId.Marshal();

        for ( int i = 0; i < memberCount; i++ )
        {
            members.add( memberIdMarshal.unmarshal( channel ) );
        }

        return new MemberIdSet( members );
    }
}
