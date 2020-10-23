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
package org.neo4j.causalclustering.messaging;

import java.util.Collection;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;

import org.neo4j.causalclustering.protocol.handshake.ProtocolStack;
import org.neo4j.helpers.AdvertisedSocketAddress;
import org.neo4j.helpers.collection.Pair;
import org.neo4j.stream.Streams;

public class ReconnectingChannels
{
    private final ConcurrentHashMap<AdvertisedSocketAddress,ReconnectingChannel> lazyChannelMap =
            new ConcurrentHashMap<>();

    public int size()
    {
        return lazyChannelMap.size();
    }

    public ReconnectingChannel get( AdvertisedSocketAddress to )
    {
        return lazyChannelMap.get( to );
    }

    public ReconnectingChannel putIfAbsent( AdvertisedSocketAddress to, ReconnectingChannel timestampedLazyChannel )
    {
        return lazyChannelMap.putIfAbsent( to, timestampedLazyChannel );
    }

    public Collection<ReconnectingChannel> values()
    {
        return lazyChannelMap.values();
    }

    public void remove( AdvertisedSocketAddress address )
    {
        lazyChannelMap.remove( address );
    }

    public Stream<Pair<AdvertisedSocketAddress,ProtocolStack>> installedProtocols()
    {
        return lazyChannelMap.entrySet().stream()
                .map( this::installedProtocolOpt )
                .flatMap( Streams::ofOptional );
    }

    private Optional<Pair<AdvertisedSocketAddress,ProtocolStack>> installedProtocolOpt( Map.Entry<AdvertisedSocketAddress,ReconnectingChannel> entry )
    {
        return entry.getValue().installedProtocolStack()
                .map( protocols -> Pair.of( entry.getKey(), protocols ) );
    }
}
