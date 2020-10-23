/*
 * Copyright 2016 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License, version 2.0 (the
 * "License"); you may not use this file except in compliance with the License. You may obtain a
 * copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */
package io.netty.handler.codec.http2;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufUtil;
import io.netty.buffer.Unpooled;
import io.netty.buffer.UnpooledByteBufAllocator;
import io.netty.channel.ChannelDuplexHandler;
import io.netty.channel.ChannelFuture;
import io.netty.channel.ChannelHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelPromise;
import io.netty.channel.embedded.EmbeddedChannel;
import io.netty.handler.codec.UnsupportedMessageTypeException;
import io.netty.handler.codec.http.HttpMethod;
import io.netty.handler.codec.http.HttpResponseStatus;
import io.netty.handler.codec.http.HttpScheme;
import io.netty.handler.codec.http2.Http2Exception.StreamException;
import io.netty.handler.codec.http2.Http2Stream.State;
import io.netty.util.AbstractReferenceCounted;
import io.netty.util.AsciiString;
import io.netty.util.ReferenceCountUtil;
import io.netty.util.ReferenceCounted;
import io.netty.util.internal.PlatformDependent;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

import java.net.InetSocketAddress;
import java.util.ArrayDeque;
import java.util.Iterator;
import java.util.Map.Entry;
import java.util.Queue;

import static io.netty.util.ReferenceCountUtil.releaseLater;
import static org.hamcrest.Matchers.instanceOf;
import static org.junit.Assert.*;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.*;

/**
 * Unit tests for {@link Http2FrameCodec}.
 */
public class Http2FrameCodecTest {

    // For verifying outbound frames
    private Http2FrameWriter frameWriter;
    private Http2FrameCodec framingCodec;
    private EmbeddedChannel channel;
    // For injecting inbound frames
    private Http2FrameListener frameListener;
    private ChannelHandlerContext http2HandlerCtx;
    private LastInboundHandler inboundHandler;

    private Http2Headers request = new DefaultHttp2Headers()
            .method(HttpMethod.GET.asciiName()).scheme(HttpScheme.HTTPS.name())
            .authority(new AsciiString("example.org")).path(new AsciiString("/foo"));
    private Http2Headers response = new DefaultHttp2Headers()
            .status(HttpResponseStatus.OK.codeAsText());

    @Before
    public void setUp() throws Exception {
        frameWriter = spy(new VerifiableHttp2FrameWriter());
        framingCodec = new Http2FrameCodec(true, frameWriter);
        frameListener = ((DefaultHttp2ConnectionDecoder) framingCodec.connectionHandler().decoder())
                .internalFrameListener();
        inboundHandler = new LastInboundHandler();

        channel = new EmbeddedChannel();
        channel.connect(new InetSocketAddress(0));
        channel.pipeline().addLast(framingCodec);
        channel.pipeline().addLast(inboundHandler);
        http2HandlerCtx = channel.pipeline().context(framingCodec.connectionHandler());

        // Handshake
        verify(frameWriter).writeSettings(eq(http2HandlerCtx),
                                          anyHttp2Settings(), anyChannelPromise());
        verifyNoMoreInteractions(frameWriter);
        channel.writeInbound(Http2CodecUtil.connectionPrefaceBuf());
        frameListener.onSettingsRead(http2HandlerCtx, new Http2Settings());
        verify(frameWriter).writeSettingsAck(eq(http2HandlerCtx), anyChannelPromise());
        frameListener.onSettingsAckRead(http2HandlerCtx);
    }

    @After
    public void tearDown() throws Exception {
        inboundHandler.finishAndReleaseAll();
        channel.finishAndReleaseAll();
    }

    @Test
    public void connectionHandlerShouldBeAddedBeforeFramingHandler() {
        Iterator<Entry<String, ChannelHandler>> iter = channel.pipeline().iterator();
        while (iter.hasNext()) {
            ChannelHandler handler = iter.next().getValue();
            if (handler instanceof Http2ConnectionHandler) {
                break;
            }
        }
        assertTrue(iter.hasNext());
        assertThat(iter.next().getValue(), instanceOf(Http2FrameCodec.class));
    }

    @Test
    public void headerRequestHeaderResponse() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 1, request, 31, true);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(1);
        assertNotNull(stream);
        assertEquals(State.HALF_CLOSED_REMOTE, stream.state());

        assertEquals(new DefaultHttp2HeadersFrame(request, true, 31).setStreamId(stream.id()),
                     inboundHandler.readInbound());
        assertNull(inboundHandler.readInbound());

        inboundHandler.writeOutbound(new DefaultHttp2HeadersFrame(response, true, 27).setStreamId(stream.id()));
        verify(frameWriter).writeHeaders(
                eq(http2HandlerCtx), eq(1), eq(response), anyInt(), anyShort(), anyBoolean(),
                eq(27), eq(true), anyChannelPromise());
        verify(frameWriter, never()).writeRstStream(
                any(ChannelHandlerContext.class), anyInt(), anyLong(), anyChannelPromise());

        assertEquals(State.CLOSED, stream.state());
        assertTrue(channel.isActive());
    }

    @Test
    public void entityRequestEntityResponse() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 1, request, 0, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(1);
        assertNotNull(stream);
        assertEquals(State.OPEN, stream.state());

        assertEquals(new DefaultHttp2HeadersFrame(request, false).setStreamId(stream.id()),
                     inboundHandler.readInbound());
        assertNull(inboundHandler.readInbound());

        ByteBuf hello = bb("hello");
        frameListener.onDataRead(http2HandlerCtx, 1, hello, 31, true);
        // Release hello to emulate ByteToMessageDecoder
        hello.release();
        Http2DataFrame inboundData = inboundHandler.readInbound();
        assertEquals(releaseLater(new DefaultHttp2DataFrame(bb("hello"), true, 31).setStreamId(stream.id())),
                     releaseLater(inboundData));
        assertEquals(1, inboundData.refCnt());
        assertNull(inboundHandler.readInbound());

        inboundHandler.writeOutbound(new DefaultHttp2HeadersFrame(response, false).setStreamId(stream.id()));
        verify(frameWriter).writeHeaders(eq(http2HandlerCtx), eq(1), eq(response), anyInt(),
                                         anyShort(), anyBoolean(), eq(0), eq(false), anyChannelPromise());

        inboundHandler.writeOutbound(releaseLater(new DefaultHttp2DataFrame(bb("world"), true, 27)
                                                          .setStreamId(stream.id())));
        ArgumentCaptor<ByteBuf> outboundData = ArgumentCaptor.forClass(ByteBuf.class);
        verify(frameWriter).writeData(eq(http2HandlerCtx), eq(1), outboundData.capture(), eq(27),
                                      eq(true), anyChannelPromise());
        assertEquals(releaseLater(bb("world")), outboundData.getValue());
        assertEquals(1, outboundData.getValue().refCnt());
        verify(frameWriter, never()).writeRstStream(
                any(ChannelHandlerContext.class), anyInt(), anyLong(), anyChannelPromise());
        assertTrue(channel.isActive());
    }

    @Test
    public void sendRstStream() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, true);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);
        assertEquals(State.HALF_CLOSED_REMOTE, stream.state());

        inboundHandler.writeOutbound(new DefaultHttp2ResetFrame(314 /* non-standard error */).setStreamId(stream.id()));
        verify(frameWriter).writeRstStream(
                eq(http2HandlerCtx), eq(3), eq(314L), anyChannelPromise());
        assertEquals(State.CLOSED, stream.state());
        assertTrue(channel.isActive());
    }

    @Test
    public void receiveRstStream() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);
        assertEquals(State.OPEN, stream.state());

        Http2StreamActiveEvent activeEvent = inboundHandler.readInboundMessagesAndEvents();
        assertNotNull(activeEvent);
        assertEquals(stream.id(), activeEvent.streamId());

        Http2HeadersFrame expectedHeaders = new DefaultHttp2HeadersFrame(request, false, 31).setStreamId(stream.id());
        Http2HeadersFrame actualHeaders = inboundHandler.readInboundMessagesAndEvents();
        assertEquals(expectedHeaders, actualHeaders);

        frameListener.onRstStreamRead(http2HandlerCtx, 3, Http2Error.NO_ERROR.code());

        Http2ResetFrame expectedRst = new DefaultHttp2ResetFrame(Http2Error.NO_ERROR).setStreamId(stream.id());
        Http2ResetFrame actualRst = inboundHandler.readInboundMessagesAndEvents();
        assertEquals(expectedRst, actualRst);

        Http2StreamClosedEvent closedEvent = inboundHandler.readInboundMessagesAndEvents();
        assertNotNull(closedEvent);
        assertEquals(stream.id(), closedEvent.streamId());

        assertNull(inboundHandler.readInboundMessagesAndEvents());
    }

    @Test
    public void sendGoAway() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);
        assertEquals(State.OPEN, stream.state());

        ByteBuf debugData = bb("debug");
        Http2GoAwayFrame goAwayFrame = new DefaultHttp2GoAwayFrame(Http2Error.NO_ERROR.code(), debugData.slice());
        goAwayFrame.setExtraStreamIds(2);

        inboundHandler.writeOutbound(releaseLater(goAwayFrame));
        verify(frameWriter).writeGoAway(
                eq(http2HandlerCtx), eq(7), eq(Http2Error.NO_ERROR.code()), eq(debugData), anyChannelPromise());
        assertEquals(1, debugData.refCnt());
        assertEquals(State.OPEN, stream.state());
        assertTrue(channel.isActive());
    }

    @Test
    public void receiveGoaway() throws Exception {
        frameListener.onGoAwayRead(http2HandlerCtx, 2, Http2Error.NO_ERROR.code(), bb("foo"));

        Http2GoAwayFrame expectedFrame = new DefaultHttp2GoAwayFrame(2, Http2Error.NO_ERROR.code(), bb("foo"));
        Http2GoAwayFrame actualFrame = inboundHandler.readInbound();

        assertEquals(releaseLater(expectedFrame), releaseLater(actualFrame));
        assertNull(inboundHandler.readInbound());
    }

    @Test
    public void unknownFrameTypeShouldThrowAndBeReleased() throws Exception {
        class UnknownHttp2Frame extends AbstractReferenceCounted implements Http2Frame {
            @Override
            public String name() {
                return "UNKNOWN";
            }

            @Override
            protected void deallocate() {
            }

            @Override
            public ReferenceCounted touch(Object hint) {
                return this;
            }
        }

        UnknownHttp2Frame frame = new UnknownHttp2Frame();
        assertEquals(1, frame.refCnt());

        ChannelFuture f = channel.write(frame);
        f.await();
        assertTrue(f.isDone());
        assertFalse(f.isSuccess());
        assertThat(f.cause(), instanceOf(UnsupportedMessageTypeException.class));
        assertEquals(0, frame.refCnt());
    }

    @Test
    public void incomingStreamActiveShouldFireUserEvent() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);

        Http2HeadersFrame frame = inboundHandler.readInbound();
        assertNotNull(frame);

        Http2StreamActiveEvent streamActiveEvent = inboundHandler.readUserEvent();
        assertEquals(stream.id(), streamActiveEvent.streamId());

        assertNull(inboundHandler.readInbound());
        assertNull(inboundHandler.readUserEvent());
    }

    @Test
    public void goAwayLastStreamIdOverflowed() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 5, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(5);
        assertNotNull(stream);
        assertEquals(State.OPEN, stream.state());

        ByteBuf debugData = bb("debug");
        Http2GoAwayFrame goAwayFrame = new DefaultHttp2GoAwayFrame(Http2Error.NO_ERROR.code(), debugData.slice());
        goAwayFrame.setExtraStreamIds(Integer.MAX_VALUE);

        inboundHandler.writeOutbound(releaseLater(goAwayFrame));
        // When the last stream id computation overflows, the last stream id should just be set to 2^31 - 1.
        verify(frameWriter).writeGoAway(eq(http2HandlerCtx), eq(Integer.MAX_VALUE), eq(Http2Error.NO_ERROR.code()),
                                        eq(debugData), anyChannelPromise());
        assertEquals(1, debugData.refCnt());
        assertEquals(State.OPEN, stream.state());
        assertTrue(channel.isActive());
    }

    @Test
    public void outgoingStreamActiveShouldFireUserEvent() throws Exception {
        Http2ConnectionEncoder encoder = framingCodec.connectionHandler().encoder();

        encoder.writeHeaders(http2HandlerCtx, 2, request, 31, false, channel.newPromise());

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(2);
        assertNotNull(stream);
        assertEquals(State.OPEN, stream.state());

        Http2StreamActiveEvent streamActiveEvent = inboundHandler.readUserEvent();
        assertEquals(stream.id(), streamActiveEvent.streamId());

        assertNull(inboundHandler.readInbound());
        assertNull(inboundHandler.readUserEvent());
    }

    @Test
    public void streamClosedShouldFireUserEvent() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);

        frameListener.onRstStreamRead(http2HandlerCtx, 3, Http2Error.INTERNAL_ERROR.code());

        assertThat(inboundHandler.readInbound(), instanceOf(Http2HeadersFrame.class));
        assertThat(inboundHandler.readInbound(), instanceOf(Http2ResetFrame.class));

        assertEquals(State.CLOSED, stream.state());

        Http2StreamActiveEvent activeEvent = inboundHandler.readUserEvent();
        assertEquals(stream.id(), activeEvent.streamId());

        Http2StreamClosedEvent closedEvent = inboundHandler.readUserEvent();
        assertEquals(stream.id(), closedEvent.streamId());

        assertNull(inboundHandler.readInbound());
        assertNull(inboundHandler.readUserEvent());
    }

    @Test
    public void streamErrorShouldFireUserEvent() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Stream stream = framingCodec.connectionHandler().connection().stream(3);
        assertNotNull(stream);

        Http2StreamActiveEvent activeEvent = inboundHandler.readInboundMessagesAndEvents();
        assertNotNull(activeEvent);
        assertEquals(stream.id(), activeEvent.streamId());

        StreamException streamEx = new StreamException(3, Http2Error.INTERNAL_ERROR, "foo");
        framingCodec.connectionHandler().onError(http2HandlerCtx, streamEx);

        Http2HeadersFrame headersFrame = inboundHandler.readInboundMessagesAndEvents();
        assertNotNull(headersFrame);

        try {
            inboundHandler.checkException();
            fail("stream exception expected");
        } catch (StreamException e) {
            assertEquals(streamEx, e);
        }

        Http2StreamClosedEvent closedEvent = inboundHandler.readInboundMessagesAndEvents();
        assertNotNull(closedEvent);
        assertEquals(stream.id(), closedEvent.streamId());

        assertNull(inboundHandler.readInboundMessagesAndEvents());
    }

    @Test
    public void windowUpdateFrameDecrementsConsumedBytes() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);

        Http2Connection connection = framingCodec.connectionHandler().connection();
        Http2Stream stream = connection.stream(3);
        assertNotNull(stream);

        ByteBuf data = Unpooled.buffer(100).writeZero(100);
        frameListener.onDataRead(http2HandlerCtx, 3, releaseLater(data), 0, true);

        int before = connection.local().flowController().unconsumedBytes(stream);
        ChannelFuture f = channel.write(new DefaultHttp2WindowUpdateFrame(100).setStreamId(stream.id()));
        int after = connection.local().flowController().unconsumedBytes(stream);
        assertEquals(100, before - after);
        assertTrue(f.isSuccess());
    }

    @Test
    public void windowUpdateMayFail() throws Exception {
        frameListener.onHeadersRead(http2HandlerCtx, 3, request, 31, false);
        Http2Connection connection = framingCodec.connectionHandler().connection();
        Http2Stream stream = connection.stream(3);
        assertNotNull(stream);

        // Fails, cause trying to return too many bytes to the flow controller
        ChannelFuture f = channel.write(new DefaultHttp2WindowUpdateFrame(100).setStreamId(stream.id()));
        assertTrue(f.isDone());
        assertFalse(f.isSuccess());
        assertThat(f.cause(), instanceOf(Http2Exception.class));
    }

    private static ChannelPromise anyChannelPromise() {
        return any(ChannelPromise.class);
    }

    private static Http2Settings anyHttp2Settings() {
        return any(Http2Settings.class);
    }

    private static ByteBuf bb(String s) {
        return ByteBufUtil.writeUtf8(UnpooledByteBufAllocator.DEFAULT, s);
    }

    static final class LastInboundHandler extends ChannelDuplexHandler {
        private final Queue<Object> inboundMessages = new ArrayDeque<Object>();
        private final Queue<Object> userEvents = new ArrayDeque<Object>();
        private final Queue<Object> inboundMessagesAndUserEvents = new ArrayDeque<Object>();
        private Throwable lastException;
        private ChannelHandlerContext ctx;

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) {
            this.ctx = ctx;
        }

        @Override
        public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
            inboundMessages.add(msg);
            inboundMessagesAndUserEvents.add(msg);
        }

        @Override
        public void userEventTriggered(ChannelHandlerContext ctx, Object evt) throws Exception {
            userEvents.add(evt);
            inboundMessagesAndUserEvents.add(evt);
        }

        @Override
        public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
            if (lastException != null) {
                cause.printStackTrace();
            } else {
                lastException = cause;
            }
        }

        public void checkException() throws Exception {
            if (lastException == null) {
                return;
            }
            Throwable t = lastException;
            lastException = null;
            PlatformDependent.throwException(t);
        }

        @SuppressWarnings("unchecked")
        public <T> T readInbound() {
            T message = (T) inboundMessages.poll();
            if (message == inboundMessagesAndUserEvents.peek()) {
                inboundMessagesAndUserEvents.poll();
            }
            return message;
        }

        @SuppressWarnings("unchecked")
        public <T> T readUserEvent() {
            T message = (T) userEvents.poll();
            if (message == inboundMessagesAndUserEvents.peek()) {
                inboundMessagesAndUserEvents.poll();
            }
            return message;
        }

        /**
         * Useful to test order of events and messages.
         */
        @SuppressWarnings("unchecked")
        public <T> T readInboundMessagesAndEvents() {
            T message = (T) inboundMessagesAndUserEvents.poll();

            if (message == inboundMessages.peek()) {
                inboundMessages.poll();
            } else if (message == userEvents.peek()) {
                userEvents.poll();
            }

            return message;
        }

        public void writeOutbound(Object... msgs) throws Exception {
            for (Object msg : msgs) {
                ctx.write(msg);
            }
            ctx.flush();
            EmbeddedChannel ch = (EmbeddedChannel) ctx.channel();
            ch.runPendingTasks();
            ch.checkException();
            checkException();
        }

        public void finishAndReleaseAll() throws Exception {
            checkException();
            Object o;
            while ((o = readInboundMessagesAndEvents()) != null) {
                ReferenceCountUtil.release(o);
            }
        }
    }

    public static class VerifiableHttp2FrameWriter extends DefaultHttp2FrameWriter {
        @Override
        public ChannelFuture writeData(ChannelHandlerContext ctx, int streamId, ByteBuf data,
                                       int padding, boolean endStream, ChannelPromise promise) {
            // duplicate 'data' to prevent readerIndex from being changed, to ease verification
            return super.writeData(ctx, streamId, data.duplicate(), padding, endStream, promise);
        }
    }
}
