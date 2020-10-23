/*
 * Copyright 2015 The Netty Project
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
package io.netty.channel.epoll;

import io.netty.channel.Channel;
import io.netty.channel.ChannelConfig;
import io.netty.channel.ChannelOutboundBuffer;
import io.netty.channel.ChannelPipeline;
import io.netty.channel.unix.DomainSocketAddress;
import io.netty.channel.unix.DomainSocketChannel;
import io.netty.channel.unix.FileDescriptor;
import io.netty.util.internal.OneTimeTask;

import java.net.SocketAddress;

public final class EpollDomainSocketChannel extends AbstractEpollStreamChannel implements DomainSocketChannel {
    private final EpollDomainSocketChannelConfig config = new EpollDomainSocketChannelConfig(this);

    private volatile DomainSocketAddress local;
    private volatile DomainSocketAddress remote;

    public EpollDomainSocketChannel() {
        super(Native.socketDomainFd());
    }

    public EpollDomainSocketChannel(Channel parent, FileDescriptor fd) {
        super(parent, fd.intValue());
    }

    /**
     * Creates a new {@link EpollDomainSocketChannel} from an existing {@link FileDescriptor}
     */
    public EpollDomainSocketChannel(FileDescriptor fd) {
        super(fd);
    }

    EpollDomainSocketChannel(Channel parent, int fd) {
        super(parent, fd);
    }

    @Override
    protected AbstractEpollUnsafe newUnsafe() {
        return new EpollDomainUnsafe();
    }

    @Override
    protected DomainSocketAddress localAddress0() {
        return local;
    }

    @Override
    protected DomainSocketAddress remoteAddress0() {
        return remote;
    }

    @Override
    protected void doBind(SocketAddress localAddress) throws Exception {
        Native.bind(fd().intValue(), localAddress);
        local = (DomainSocketAddress) localAddress;
    }

    @Override
    public EpollDomainSocketChannelConfig config() {
        return config;
    }

    @Override
    protected boolean doConnect(SocketAddress remoteAddress, SocketAddress localAddress) throws Exception {
        if (super.doConnect(remoteAddress, localAddress)) {
            local = (DomainSocketAddress) localAddress;
            remote = (DomainSocketAddress) remoteAddress;
            return true;
        }
        return false;
    }

    @Override
    public DomainSocketAddress remoteAddress() {
        return (DomainSocketAddress) super.remoteAddress();
    }

    @Override
    public DomainSocketAddress localAddress() {
        return (DomainSocketAddress) super.localAddress();
    }

    @Override
    protected boolean doWriteSingle(ChannelOutboundBuffer in, int writeSpinCount) throws Exception {
        Object msg = in.current();
        if (msg instanceof FileDescriptor && Native.sendFd(fd().intValue(), ((FileDescriptor) msg).intValue()) > 0) {
            // File descriptor was written, so remove it.
            in.remove();
            return true;
        }
        return super.doWriteSingle(in, writeSpinCount);
    }

    @Override
    protected Object filterOutboundMessage(Object msg) {
        if (msg instanceof FileDescriptor) {
            return msg;
        }
        return super.filterOutboundMessage(msg);
    }

    private final class EpollDomainUnsafe extends EpollStreamUnsafe {
        @Override
        void epollInReady() {
            switch (config().getReadMode()) {
                case BYTES:
                    super.epollInReady();
                    break;
                case FILE_DESCRIPTORS:
                    epollInReadFd();
                    break;
                default:
                    throw new Error();
            }
        }

        private void epollInReadFd() {
            boolean edgeTriggered = isFlagSet(Native.EPOLLET);
            final ChannelConfig config = config();
            if (!readPending && !edgeTriggered && !config.isAutoRead()) {
                // ChannelConfig.setAutoRead(false) was called in the meantime
                clearEpollIn0();
                return;
            }

            final ChannelPipeline pipeline = pipeline();
            final EpollRecvByteAllocatorHandle allocHandle = recvBufAllocHandle();
            allocHandle.reset(config);

            try {
                do {
                    int socketFd = Native.recvFd(fd().intValue());
                    if (socketFd == 0) {
                        break;
                    }
                    if (socketFd == -1) {
                        close(voidPromise());
                        return;
                    }

                    readPending = false;
                    allocHandle.incMessagesRead(1);
                    pipeline.fireChannelRead(new FileDescriptor(socketFd));
                } while (allocHandle.continueReading());

                allocHandle.readComplete();
                pipeline.fireChannelReadComplete();
            } catch (Throwable t) {
                allocHandle.readComplete();
                pipeline.fireChannelReadComplete();
                pipeline.fireExceptionCaught(t);
                checkResetEpollIn(edgeTriggered);
            } finally {
                // Check if there is a readPending which was not processed yet.
                // This could be for two reasons:
                // * The user called Channel.read() or ChannelHandlerContext.read() in channelRead(...) method
                // * The user called Channel.read() or ChannelHandlerContext.read() in channelReadComplete(...) method
                //
                // See https://github.com/netty/netty/issues/2254
                if (!readPending && !config.isAutoRead()) {
                    clearEpollIn0();
                }
            }
        }
    }
}
