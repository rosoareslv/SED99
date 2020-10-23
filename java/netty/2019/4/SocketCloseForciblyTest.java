/*
 * Copyright 2017 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License,
 * version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
package io.netty.testsuite.transport.socket;

import io.netty.bootstrap.Bootstrap;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.Channel;
import io.netty.channel.ChannelHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelInboundHandler;
import io.netty.channel.socket.SocketChannel;
import org.junit.Test;

public class SocketCloseForciblyTest extends AbstractSocketTest {

    @Test
    public void testCloseForcibly() throws Throwable {
        run();
    }

    public void testCloseForcibly(ServerBootstrap sb, Bootstrap cb) throws Throwable {
        sb.handler(new ChannelHandler() {
            @Override
            public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
                final SocketChannel childChannel = (SocketChannel) msg;
                // Dispatch on the EventLoop as all operation on Unsafe should be done while on the EventLoop.
                childChannel.eventLoop().execute(() -> {
                    childChannel.config().setSoLinger(0);
                    childChannel.unsafe().closeForcibly();
                });
            }
        }).childHandler(new ChannelHandler() { });

        cb.handler(new ChannelHandler() { });

        Channel sc = sb.bind().sync().channel();

        cb.connect(sc.localAddress()).channel().closeFuture().syncUninterruptibly();
        sc.close().sync();
    }
}
