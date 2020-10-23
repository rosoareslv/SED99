/*
 * Copyright 2014 The Netty Project
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

import static io.netty.buffer.Unpooled.EMPTY_BUFFER;
import static io.netty.buffer.Unpooled.wrappedBuffer;
import static io.netty.handler.codec.http2.Http2CodecUtil.DEFAULT_PRIORITY_WEIGHT;
import static io.netty.handler.codec.http2.Http2CodecUtil.emptyPingBuf;
import static io.netty.handler.codec.http2.Http2Error.PROTOCOL_ERROR;
import static io.netty.handler.codec.http2.Http2Stream.State.HALF_CLOSED_REMOTE;
import static io.netty.handler.codec.http2.Http2Stream.State.IDLE;
import static io.netty.handler.codec.http2.Http2Stream.State.RESERVED_LOCAL;
import static io.netty.util.CharsetUtil.UTF_8;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyBoolean;
import static org.mockito.Matchers.anyInt;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Matchers.anyShort;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.PooledByteBufAllocator;
import io.netty.buffer.Unpooled;
import io.netty.buffer.UnpooledByteBufAllocator;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFuture;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelPromise;
import io.netty.channel.DefaultChannelPromise;
import io.netty.handler.codec.http2.Http2RemoteFlowController.FlowControlled;
import io.netty.util.concurrent.Future;
import io.netty.util.concurrent.GenericFutureListener;
import io.netty.util.concurrent.ImmediateEventExecutor;
import junit.framework.AssertionFailedError;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;

import java.util.ArrayList;
import java.util.List;

/**
 * Tests for {@link DefaultHttp2ConnectionEncoder}
 */
public class DefaultHttp2ConnectionEncoderTest {
    private static final int STREAM_ID = 2;
    private static final int PUSH_STREAM_ID = 4;

    @Mock
    private Http2RemoteFlowController remoteFlow;

    @Mock
    private ChannelHandlerContext ctx;

    @Mock
    private Channel channel;

    @Mock
    private Http2FrameListener listener;

    @Mock
    private Http2FrameWriter writer;

    @Mock
    private Http2FrameWriter.Configuration writerConfig;

    @Mock
    private Http2FrameSizePolicy frameSizePolicy;

    @Mock
    private Http2LifecycleManager lifecycleManager;

    private DefaultHttp2ConnectionEncoder encoder;
    private Http2Connection connection;
    private ArgumentCaptor<Http2RemoteFlowController.FlowControlled> payloadCaptor;
    private List<String> writtenData;
    private List<Integer> writtenPadding;
    private boolean streamClosed;

    @Before
    public void setup() throws Exception {
        MockitoAnnotations.initMocks(this);

        when(channel.isActive()).thenReturn(true);
        when(writer.configuration()).thenReturn(writerConfig);
        when(writerConfig.frameSizePolicy()).thenReturn(frameSizePolicy);
        when(frameSizePolicy.maxFrameSize()).thenReturn(64);
        doAnswer(new Answer<ChannelFuture>() {
            @Override
            public ChannelFuture answer(InvocationOnMock in) throws Throwable {
                return ((ChannelPromise) in.getArguments()[2]).setSuccess();
            }
        }).when(writer).writeSettings(eq(ctx), any(Http2Settings.class), any(ChannelPromise.class));
        doAnswer(new Answer<ChannelFuture>() {
            @Override
            public ChannelFuture answer(InvocationOnMock in) throws Throwable {
                ((ByteBuf) in.getArguments()[3]).release();
                return ((ChannelPromise) in.getArguments()[4]).setSuccess();
            }
        }).when(writer).writeGoAway(eq(ctx), anyInt(), anyInt(), any(ByteBuf.class), any(ChannelPromise.class));
        writtenData = new ArrayList<String>();
        writtenPadding = new ArrayList<Integer>();
        when(writer.writeData(eq(ctx), anyInt(), any(ByteBuf.class), anyInt(), anyBoolean(),
                any(ChannelPromise.class))).then(new Answer<ChannelFuture>() {
                    @Override
                    public ChannelFuture answer(InvocationOnMock in) throws Throwable {
                        // Make sure we only receive stream closure on the last frame and that void promises
                        // are used for all writes except the last one.
                        ChannelPromise promise = (ChannelPromise) in.getArguments()[5];
                        if (streamClosed) {
                            fail("Stream already closed");
                        } else {
                            streamClosed = (Boolean) in.getArguments()[4];
                        }
                        writtenPadding.add((Integer) in.getArguments()[3]);
                        ByteBuf data = (ByteBuf) in.getArguments()[2];
                        writtenData.add(data.toString(UTF_8));
                        // Release the buffer just as DefaultHttp2FrameWriter does
                        data.release();
                        // Let the promise succeed to trigger listeners.
                        return promise.setSuccess();
                    }
                });
        when(writer.writeHeaders(eq(ctx), anyInt(), any(Http2Headers.class), anyInt(), anyShort(), anyBoolean(),
                anyInt(), anyBoolean(), any(ChannelPromise.class)))
                .then(new Answer<ChannelFuture>() {
                    @Override
                    public ChannelFuture answer(InvocationOnMock invocationOnMock) throws Throwable {
                        ChannelPromise promise = (ChannelPromise) invocationOnMock.getArguments()[8];
                        if (streamClosed) {
                            fail("Stream already closed");
                        } else {
                            streamClosed = (Boolean) invocationOnMock.getArguments()[5];
                        }
                        return promise.setSuccess();
                    }
                });
        payloadCaptor = ArgumentCaptor.forClass(Http2RemoteFlowController.FlowControlled.class);
        doNothing().when(remoteFlow).addFlowControlled(any(Http2Stream.class), payloadCaptor.capture());
        when(ctx.alloc()).thenReturn(UnpooledByteBufAllocator.DEFAULT);
        when(ctx.channel()).thenReturn(channel);
        doAnswer(new Answer<ChannelPromise>() {
            @Override
            public ChannelPromise answer(InvocationOnMock in) throws Throwable {
                return newPromise();
            }
        }).when(ctx).newPromise();
        doAnswer(new Answer<ChannelFuture>() {
            @Override
            public ChannelFuture answer(InvocationOnMock in) throws Throwable {
                return newSucceededFuture();
            }
        }).when(ctx).newSucceededFuture();
        when(ctx.flush()).thenThrow(new AssertionFailedError("forbidden"));
        when(channel.alloc()).thenReturn(PooledByteBufAllocator.DEFAULT);

        // Use a server-side connection so we can test server push.
        connection = new DefaultHttp2Connection(true);
        connection.remote().flowController(remoteFlow);

        encoder = new DefaultHttp2ConnectionEncoder(connection, writer);
        encoder.lifecycleManager(lifecycleManager);
    }

    @Test
    public void dataWriteShouldSucceed() throws Exception {
        createStream(STREAM_ID, false);
        final ByteBuf data = dummyData();
        ChannelPromise p = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, true, p);
        assertEquals(payloadCaptor.getValue().size(), 8);
        payloadCaptor.getValue().write(ctx, 8);
        assertEquals(0, payloadCaptor.getValue().size());
        assertEquals("abcdefgh", writtenData.get(0));
        assertEquals(0, data.refCnt());
        assertTrue(p.isSuccess());
    }

    @Test
    public void dataFramesShouldMerge() throws Exception {
        createStream(STREAM_ID, false);
        final ByteBuf data = dummyData().retain();

        ChannelPromise promise1 = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, true, promise1);
        ChannelPromise promise2 = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, true, promise2);

        // Now merge the two payloads.
        List<FlowControlled> capturedWrites = payloadCaptor.getAllValues();
        FlowControlled mergedPayload = capturedWrites.get(0);
        mergedPayload.merge(ctx, capturedWrites.get(1));
        assertEquals(16, mergedPayload.size());
        assertFalse(promise1.isDone());
        assertFalse(promise2.isDone());

        // Write the merged payloads and verify it was written correctly.
        mergedPayload.write(ctx, 16);
        assertEquals(0, mergedPayload.size());
        assertEquals("abcdefghabcdefgh", writtenData.get(0));
        assertEquals(0, data.refCnt());
        assertTrue(promise1.isSuccess());
        assertTrue(promise2.isSuccess());
    }

    @Test
    public void dataFramesShouldMergeUseVoidPromise() throws Exception {
        createStream(STREAM_ID, false);
        final ByteBuf data = dummyData().retain();

        ChannelPromise promise1 = newVoidPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, true, promise1);
        ChannelPromise promise2 = newVoidPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, true, promise2);

        // Now merge the two payloads.
        List<FlowControlled> capturedWrites = payloadCaptor.getAllValues();
        FlowControlled mergedPayload = capturedWrites.get(0);
        mergedPayload.merge(ctx, capturedWrites.get(1));
        assertEquals(16, mergedPayload.size());
        assertFalse(promise1.isSuccess());
        assertFalse(promise2.isSuccess());

        // Write the merged payloads and verify it was written correctly.
        mergedPayload.write(ctx, 16);
        assertEquals(0, mergedPayload.size());
        assertEquals("abcdefghabcdefgh", writtenData.get(0));
        assertEquals(0, data.refCnt());

        // The promises won't be set since there are no listeners.
        assertFalse(promise1.isSuccess());
        assertFalse(promise2.isSuccess());
    }

    @Test
    public void dataFramesDontMergeWithHeaders() throws Exception {
        createStream(STREAM_ID, false);
        final ByteBuf data = dummyData().retain();
        encoder.writeData(ctx, STREAM_ID, data, 0, false, newPromise());
        when(remoteFlow.hasFlowControlled(any(Http2Stream.class))).thenReturn(true);
        encoder.writeHeaders(ctx, STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, true, newPromise());
        List<FlowControlled> capturedWrites = payloadCaptor.getAllValues();
        assertFalse(capturedWrites.get(0).merge(ctx, capturedWrites.get(1)));
    }

    @Test
    public void emptyFrameShouldSplitPadding() throws Exception {
        ByteBuf data = Unpooled.buffer(0);
        assertSplitPaddingOnEmptyBuffer(data);
        assertEquals(0, data.refCnt());
    }

    private void assertSplitPaddingOnEmptyBuffer(ByteBuf data) throws Exception {
        createStream(STREAM_ID, false);
        when(frameSizePolicy.maxFrameSize()).thenReturn(5);
        ChannelPromise p = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 10, true, p);
        assertEquals(payloadCaptor.getValue().size(), 10);
        payloadCaptor.getValue().write(ctx, 10);
        // writer was called 2 times
        assertEquals(1, writtenData.size());
        assertEquals("", writtenData.get(0));
        assertEquals(10, (int) writtenPadding.get(0));
        assertEquals(0, data.refCnt());
        assertTrue(p.isSuccess());
    }

    @Test
    public void headersWriteForUnknownStreamShouldCreateStream() throws Exception {
        writeAllFlowControlledFrames();
        final int streamId = 6;
        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, streamId, EmptyHttp2Headers.INSTANCE, 0, false, promise);
        verify(writer).writeHeaders(eq(ctx), eq(streamId), eq(EmptyHttp2Headers.INSTANCE), eq(0),
                eq(DEFAULT_PRIORITY_WEIGHT), eq(false), eq(0), eq(false), eq(promise));
        assertTrue(promise.isSuccess());
    }

    @Test
    public void headersWriteShouldOpenStreamForPush() throws Exception {
        writeAllFlowControlledFrames();
        Http2Stream parent = createStream(STREAM_ID, false);
        reservePushStream(PUSH_STREAM_ID, parent);

        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, PUSH_STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, false, promise);
        assertEquals(HALF_CLOSED_REMOTE, stream(PUSH_STREAM_ID).state());
        verify(writer).writeHeaders(eq(ctx), eq(PUSH_STREAM_ID), eq(EmptyHttp2Headers.INSTANCE), eq(0),
                eq(DEFAULT_PRIORITY_WEIGHT), eq(false), eq(0), eq(false), eq(promise));
    }

    @Test
    public void pushPromiseWriteAfterGoAwayReceivedShouldFail() throws Exception {
        createStream(STREAM_ID, false);
        goAwayReceived(0);
        ChannelFuture future =  encoder.writePushPromise(ctx, STREAM_ID, PUSH_STREAM_ID, EmptyHttp2Headers.INSTANCE, 0,
                newPromise());
        assertTrue(future.isDone());
        assertFalse(future.isSuccess());
    }

    @Test
    public void pushPromiseWriteShouldReserveStream() throws Exception {
        createStream(STREAM_ID, false);
        ChannelPromise promise = newPromise();
        encoder.writePushPromise(ctx, STREAM_ID, PUSH_STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, promise);
        assertEquals(RESERVED_LOCAL, stream(PUSH_STREAM_ID).state());
        verify(writer).writePushPromise(eq(ctx), eq(STREAM_ID), eq(PUSH_STREAM_ID),
                eq(EmptyHttp2Headers.INSTANCE), eq(0), eq(promise));
    }

    @Test
    public void priorityWriteAfterGoAwayShouldSucceed() throws Exception {
        createStream(STREAM_ID, false);
        goAwayReceived(Integer.MAX_VALUE);
        ChannelPromise promise = newPromise();
        encoder.writePriority(ctx, STREAM_ID, 0, (short) 255, true, promise);
        verify(writer).writePriority(eq(ctx), eq(STREAM_ID), eq(0), eq((short) 255), eq(true), eq(promise));
    }

    @Test
    public void priorityWriteShouldSetPriorityForStream() throws Exception {
        ChannelPromise promise = newPromise();
        short weight = 255;
        encoder.writePriority(ctx, STREAM_ID, 0, weight, true, promise);

        // Verify that this created an idle stream with the desired weight.
        Http2Stream stream = stream(STREAM_ID);
        assertEquals(IDLE, stream.state());
        assertEquals(weight, stream.weight());

        verify(writer).writePriority(eq(ctx), eq(STREAM_ID), eq(0), eq((short) 255), eq(true), eq(promise));
    }

    @Test
    public void priorityWriteOnPreviouslyExistingStreamShouldSucceed() throws Exception {
        createStream(STREAM_ID, false).close();
        ChannelPromise promise = newPromise();
        short weight = 255;
        encoder.writePriority(ctx, STREAM_ID, 0, weight, true, promise);
        verify(writer).writePriority(eq(ctx), eq(STREAM_ID), eq(0), eq(weight), eq(true), eq(promise));
    }

    @Test
    public void priorityWriteOnPreviouslyExistingParentStreamShouldSucceed() throws Exception {
        final int parentStreamId = STREAM_ID + 2;
        createStream(STREAM_ID, false);
        createStream(parentStreamId, false).close();

        ChannelPromise promise = newPromise();
        short weight = 255;
        encoder.writePriority(ctx, STREAM_ID, parentStreamId, weight, true, promise);
        verify(writer).writePriority(eq(ctx), eq(STREAM_ID), eq(parentStreamId), eq(weight), eq(true), eq(promise));
    }

    @Test
    public void rstStreamWriteForUnknownStreamShouldIgnore() throws Exception {
        ChannelPromise promise = newPromise();
        encoder.writeRstStream(ctx, 5, PROTOCOL_ERROR.code(), promise);
        verify(writer, never()).writeRstStream(eq(ctx), anyInt(), anyLong(), eq(promise));
    }

    @Test
    public void rstStreamShouldCloseStream() throws Exception {
        // Create the stream and send headers.
        writeAllFlowControlledFrames();
        encoder.writeHeaders(ctx, STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, true, newPromise());

        // Now verify that a stream reset is performed.
        stream(STREAM_ID);
        ChannelPromise promise = newPromise();
        encoder.writeRstStream(ctx, STREAM_ID, PROTOCOL_ERROR.code(), promise);
        verify(lifecycleManager).resetStream(eq(ctx), eq(STREAM_ID), anyInt(), eq(promise));
    }

    @Test
    public void pingWriteAfterGoAwayShouldSucceed() throws Exception {
        ChannelPromise promise = newPromise();
        goAwayReceived(0);
        encoder.writePing(ctx, false, emptyPingBuf(), promise);
        verify(writer).writePing(eq(ctx), eq(false), eq(emptyPingBuf()), eq(promise));
    }

    @Test
    public void pingWriteShouldSucceed() throws Exception {
        ChannelPromise promise = newPromise();
        encoder.writePing(ctx, false, emptyPingBuf(), promise);
        verify(writer).writePing(eq(ctx), eq(false), eq(emptyPingBuf()), eq(promise));
    }

    @Test
    public void settingsWriteAfterGoAwayShouldSucceed() throws Exception {
        goAwayReceived(0);
        ChannelPromise promise = newPromise();
        encoder.writeSettings(ctx, new Http2Settings(), promise);
        verify(writer).writeSettings(eq(ctx), any(Http2Settings.class), eq(promise));
    }

    @Test
    public void settingsWriteShouldNotUpdateSettings() throws Exception {
        Http2Settings settings = new Http2Settings();
        settings.initialWindowSize(100);
        settings.maxConcurrentStreams(1000);
        settings.headerTableSize(2000);

        ChannelPromise promise = newPromise();
        encoder.writeSettings(ctx, settings, promise);
        verify(writer).writeSettings(eq(ctx), eq(settings), eq(promise));
    }

    @Test
    public void dataWriteShouldCreateHalfClosedStream() throws Exception {
        writeAllFlowControlledFrames();

        Http2Stream stream = createStream(STREAM_ID, false);
        ByteBuf data = dummyData();
        ChannelPromise promise = newPromise();
        encoder.writeData(ctx, STREAM_ID, data.retain(), 0, true, promise);
        assertTrue(promise.isSuccess());
        verify(remoteFlow).addFlowControlled(eq(stream), any(FlowControlled.class));
        verify(lifecycleManager).closeStreamLocal(stream, promise);
        assertEquals(data.toString(UTF_8), writtenData.get(0));
        data.release();
    }

    @Test
    public void headersWriteShouldHalfCloseStream() throws Exception {
        writeAllFlowControlledFrames();
        createStream(STREAM_ID, false);
        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, true, promise);

        assertTrue(promise.isSuccess());
        verify(lifecycleManager).closeStreamLocal(eq(stream(STREAM_ID)), eq(promise));
    }

    @Test
    public void headersWriteShouldHalfClosePushStream() throws Exception {
        writeAllFlowControlledFrames();
        Http2Stream parent = createStream(STREAM_ID, false);
        Http2Stream stream = reservePushStream(PUSH_STREAM_ID, parent);
        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, PUSH_STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, true, promise);
        assertEquals(HALF_CLOSED_REMOTE, stream.state());
        assertTrue(promise.isSuccess());
        verify(lifecycleManager).closeStreamLocal(eq(stream), eq(promise));
    }

    @Test
    public void encoderDelegatesGoAwayToLifeCycleManager() {
        ChannelPromise promise = newPromise();
        encoder.writeGoAway(ctx, STREAM_ID, Http2Error.INTERNAL_ERROR.code(), null, promise);
        verify(lifecycleManager).goAway(eq(ctx), eq(STREAM_ID), eq(Http2Error.INTERNAL_ERROR.code()),
                eq((ByteBuf) null), eq(promise));
        verifyNoMoreInteractions(writer);
    }

    @Test
    public void dataWriteToClosedStreamShouldFail() throws Exception {
        createStream(STREAM_ID, false).close();
        ByteBuf data = mock(ByteBuf.class);
        ChannelPromise promise = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, false, promise);
        assertTrue(promise.isDone());
        assertFalse(promise.isSuccess());
        assertThat(promise.cause(), instanceOf(IllegalArgumentException.class));
        verify(data).release();
    }

    @Test
    public void dataWriteToHalfClosedLocalStreamShouldFail() throws Exception {
        createStream(STREAM_ID, true);
        ByteBuf data = mock(ByteBuf.class);
        ChannelPromise promise = newPromise();
        encoder.writeData(ctx, STREAM_ID, data, 0, false, promise);
        assertTrue(promise.isDone());
        assertFalse(promise.isSuccess());
        assertThat(promise.cause(), instanceOf(IllegalStateException.class));
        verify(data).release();
    }

    @Test
    public void canWriteDataFrameAfterGoAwaySent() throws Exception {
        Http2Stream stream = createStream(STREAM_ID, false);
        connection.goAwaySent(0, 0, EMPTY_BUFFER);
        ByteBuf data = mock(ByteBuf.class);
        encoder.writeData(ctx, STREAM_ID, data, 0, false, newPromise());
        verify(remoteFlow).addFlowControlled(eq(stream), any(FlowControlled.class));
    }

    @Test
    public void canWriteHeaderFrameAfterGoAwaySent() throws Exception {
        writeAllFlowControlledFrames();
        createStream(STREAM_ID, false);
        goAwaySent(0);
        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, false, promise);
        verify(writer).writeHeaders(eq(ctx), eq(STREAM_ID), eq(EmptyHttp2Headers.INSTANCE), eq(0),
                eq(DEFAULT_PRIORITY_WEIGHT), eq(false), eq(0), eq(false), eq(promise));
    }

    @Test
    public void canWriteDataFrameAfterGoAwayReceived() throws Exception {
        Http2Stream stream = createStream(STREAM_ID, false);
        goAwayReceived(STREAM_ID);
        ByteBuf data = mock(ByteBuf.class);
        encoder.writeData(ctx, STREAM_ID, data, 0, false, newPromise());
        verify(remoteFlow).addFlowControlled(eq(stream), any(FlowControlled.class));
    }

    @Test
    public void canWriteHeaderFrameAfterGoAwayReceived() {
        writeAllFlowControlledFrames();
        goAwayReceived(STREAM_ID);
        ChannelPromise promise = newPromise();
        encoder.writeHeaders(ctx, STREAM_ID, EmptyHttp2Headers.INSTANCE, 0, false, promise);
        verify(writer).writeHeaders(eq(ctx), eq(STREAM_ID), eq(EmptyHttp2Headers.INSTANCE), eq(0),
                eq(DEFAULT_PRIORITY_WEIGHT), eq(false), eq(0), eq(false), eq(promise));
    }

    private void writeAllFlowControlledFrames() {
        doAnswer(new Answer<Void>() {
            @Override
            public Void answer(InvocationOnMock invocationOnMock) throws Throwable {
                FlowControlled flowControlled = (FlowControlled) invocationOnMock.getArguments()[1];
                flowControlled.write(ctx, Integer.MAX_VALUE);
                flowControlled.writeComplete();
                return null;
            }
        }).when(remoteFlow).addFlowControlled(any(Http2Stream.class), payloadCaptor.capture());
    }

    private Http2Stream createStream(int streamId, boolean halfClosed) throws Http2Exception {
        return connection.local().createStream(streamId, halfClosed);
    }

    private Http2Stream reservePushStream(int pushStreamId, Http2Stream parent) throws Http2Exception {
        return connection.local().reservePushStream(pushStreamId, parent);
    }

    private Http2Stream stream(int streamId) {
        return connection.stream(streamId);
    }

    private void goAwayReceived(int lastStreamId) {
        connection.goAwayReceived(lastStreamId, 0, EMPTY_BUFFER);
    }

    private void goAwaySent(int lastStreamId) {
        connection.goAwaySent(lastStreamId, 0, EMPTY_BUFFER);
    }

    private ChannelPromise newPromise() {
        return new DefaultChannelPromise(channel, ImmediateEventExecutor.INSTANCE);
    }

    private ChannelPromise newVoidPromise() {
        return new DefaultChannelPromise(channel, ImmediateEventExecutor.INSTANCE) {
            @Override
            public ChannelPromise addListener(
                    GenericFutureListener<? extends Future<? super Void>> listener) {
                throw new AssertionFailedError();
            }

            @Override
            public ChannelPromise addListeners(
                    GenericFutureListener<? extends Future<? super Void>>... listeners) {
                throw new AssertionFailedError();
            }

            @Override
            public boolean isVoid() {
                return true;
            }
        };
    }

    private ChannelFuture newSucceededFuture() {
        return newPromise().setSuccess();
    }

    private static ByteBuf dummyData() {
        // The buffer is purposely 8 bytes so it will even work for a ping frame.
        return wrappedBuffer("abcdefgh".getBytes(UTF_8));
    }
}
