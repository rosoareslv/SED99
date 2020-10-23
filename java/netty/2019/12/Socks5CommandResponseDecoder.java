/*
 * Copyright 2014 The Netty Project
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

package io.netty.handler.codec.socksx.v5;

import static java.util.Objects.requireNonNull;

import io.netty.buffer.ByteBuf;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.DecoderException;
import io.netty.handler.codec.DecoderResult;
import io.netty.handler.codec.ReplayingDecoder;
import io.netty.handler.codec.socksx.SocksVersion;
import io.netty.handler.codec.socksx.v5.Socks5CommandResponseDecoder.State;

/**
 * Decodes a single {@link Socks5CommandResponse} from the inbound {@link ByteBuf}s.
 * On successful decode, this decoder will forward the received data to the next handler, so that
 * other handler can remove or replace this decoder later.  On failed decode, this decoder will
 * discard the received data, so that other handler closes the connection later.
 */
public class Socks5CommandResponseDecoder extends ReplayingDecoder<State> {

    enum State {
        INIT,
        SUCCESS,
        FAILURE
    }

    private final Socks5AddressDecoder addressDecoder;

    public Socks5CommandResponseDecoder() {
        this(Socks5AddressDecoder.DEFAULT);
    }

    public Socks5CommandResponseDecoder(Socks5AddressDecoder addressDecoder) {
        super(State.INIT);
        requireNonNull(addressDecoder, "addressDecoder");

        this.addressDecoder = addressDecoder;
    }

    @Override
    protected void decode(ChannelHandlerContext ctx, ByteBuf in) throws Exception {
        try {
            switch (state()) {
            case INIT: {
                final byte version = in.readByte();
                if (version != SocksVersion.SOCKS5.byteValue()) {
                    throw new DecoderException(
                            "unsupported version: " + version + " (expected: " + SocksVersion.SOCKS5.byteValue() + ')');
                }
                final Socks5CommandStatus status = Socks5CommandStatus.valueOf(in.readByte());
                in.skipBytes(1); // Reserved
                final Socks5AddressType addrType = Socks5AddressType.valueOf(in.readByte());
                final String addr = addressDecoder.decodeAddress(addrType, in);
                final int port = in.readUnsignedShort();

                ctx.fireChannelRead(new DefaultSocks5CommandResponse(status, addrType, addr, port));
                checkpoint(State.SUCCESS);
            }
            case SUCCESS: {
                int readableBytes = actualReadableBytes();
                if (readableBytes > 0) {
                    ctx.fireChannelRead(in.readRetainedSlice(readableBytes));
                }
                break;
            }
            case FAILURE: {
                in.skipBytes(actualReadableBytes());
                break;
            }
            }
        } catch (Exception e) {
            fail(ctx, e);
        }
    }

    private void fail(ChannelHandlerContext ctx, Exception cause) {
        if (!(cause instanceof DecoderException)) {
            cause = new DecoderException(cause);
        }

        checkpoint(State.FAILURE);

        Socks5Message m = new DefaultSocks5CommandResponse(
                Socks5CommandStatus.FAILURE, Socks5AddressType.IPv4, null, 0);
        m.setDecoderResult(DecoderResult.failure(cause));
        ctx.fireChannelRead(m);
    }
}
