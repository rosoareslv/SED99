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
package org.neo4j.com;

import org.jboss.netty.bootstrap.ServerBootstrap;
import org.jboss.netty.channel.Channel;
import org.jboss.netty.channel.ChannelException;

import java.net.InetSocketAddress;
import java.util.Objects;

import org.neo4j.helpers.HostnamePort;

public class PortRangeSocketBinder
{
    private ServerBootstrap bootstrap;
    private static final String ALL_INTERFACES_ADDRESS = "0.0.0.0";

    public PortRangeSocketBinder( ServerBootstrap bootstrap )
    {
        this.bootstrap = bootstrap;
    }

    public Connection bindToFirstAvailablePortInRange( HostnamePort serverAddress ) throws ChannelException
    {
        int[] ports = serverAddress.getPorts();
        String host = serverAddress.getHost();

        Channel channel;
        InetSocketAddress socketAddress;
        ChannelException lastException = null;

        PortIterator portIterator = new PortIterator( ports );
        while ( portIterator.hasNext() )
        {
            Integer port = portIterator.next();
            if ( host == null || host.equals( ALL_INTERFACES_ADDRESS ) )
            {
                socketAddress = new InetSocketAddress( port );
            }
            else
            {
                socketAddress = new InetSocketAddress( host, port );
            }
            try
            {
                channel = bootstrap.bind( socketAddress );
                return new Connection( socketAddress, channel );
            }
            catch ( ChannelException e )
            {
                if ( lastException != null )
                {
                    e.addSuppressed( lastException );
                }
                lastException = e;
            }
        }

        throw Objects.requireNonNull( lastException );
    }
}
