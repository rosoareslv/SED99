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
package org.neo4j.backup.impl;

import org.jboss.netty.buffer.ChannelBuffer;
import org.jboss.netty.buffer.ChannelBuffers;
import org.jboss.netty.channel.Channel;
import org.jboss.netty.channel.ChannelFuture;
import org.junit.Test;

import org.neo4j.function.Factory;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class BufferReusingChunkingChannelBufferTest
{
    @Test
    @SuppressWarnings( "unchecked" )
    public void newBuffersAreCreatedIfNoFreeBuffersAreAvailable()
    {
        CountingChannelBufferFactory bufferFactory = new CountingChannelBufferFactory();
        BufferReusingChunkingChannelBuffer buffer = newBufferReusingChunkingChannelBuffer( 10, bufferFactory );

        buffer.writeLong( 1 );
        buffer.writeLong( 2 );
        buffer.writeLong( 3 );

        assertEquals( 3, bufferFactory.instancesCreated );
    }

    @Test
    @SuppressWarnings( "unchecked" )
    public void freeBuffersAreReused() throws Exception
    {
        CountingChannelBufferFactory bufferFactory = new CountingChannelBufferFactory();
        BufferReusingChunkingChannelBuffer buffer = newBufferReusingChunkingChannelBuffer( 10, bufferFactory );

        buffer.writeLong( 1 );
        buffer.writeLong( 2 );

        // return 2 buffers to the pool
        ChannelBuffer reusedBuffer1 = triggerOperationCompleteCallback( buffer );
        ChannelBuffer reusedBuffer2 = triggerOperationCompleteCallback( buffer );

        buffer.writeLong( 3 );
        buffer.writeLong( 4 );

        // 2 buffers were created
        assertEquals( 2, bufferFactory.instancesCreated );

        // and 2 buffers were reused
        verify( reusedBuffer1 ).writeLong( 3 );
        verify( reusedBuffer2 ).writeLong( 4 );
    }

    private static BufferReusingChunkingChannelBuffer newBufferReusingChunkingChannelBuffer( int capacity,
            CountingChannelBufferFactory bufferFactory )
    {
        ChannelBuffer buffer = ChannelBuffers.dynamicBuffer();

        Channel channel = mock( Channel.class );
        ChannelFuture channelFuture = mock( ChannelFuture.class );
        when( channel.isOpen() ).thenReturn( true );
        when( channel.isConnected() ).thenReturn( true );
        when( channel.isBound() ).thenReturn( true );
        when( channel.write( any() ) ).thenReturn( channelFuture );

        return new BufferReusingChunkingChannelBuffer( buffer, bufferFactory, channel, capacity, (byte) 1, (byte) 1 );
    }

    private static ChannelBuffer triggerOperationCompleteCallback( BufferReusingChunkingChannelBuffer buffer )
            throws Exception
    {
        ChannelBuffer reusedBuffer = spy( ChannelBuffers.dynamicBuffer() );

        ChannelFuture channelFuture = mock( ChannelFuture.class );
        when( channelFuture.isDone() ).thenReturn( true );
        when( channelFuture.isSuccess() ).thenReturn( true );

        buffer.newChannelFutureListener( reusedBuffer ).operationComplete( channelFuture );
        return reusedBuffer;
    }

    private static class CountingChannelBufferFactory implements Factory<ChannelBuffer>
    {
        int instancesCreated;

        @Override
        public ChannelBuffer newInstance()
        {
            instancesCreated++;
            return ChannelBuffers.dynamicBuffer();
        }
    }
}
