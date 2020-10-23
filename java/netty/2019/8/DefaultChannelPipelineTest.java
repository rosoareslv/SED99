/*
 * Copyright 2012 The Netty Project
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
package io.netty.channel;


import io.netty.bootstrap.Bootstrap;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.ChannelHandler.Sharable;
import io.netty.channel.ChannelHandlerMask.Skip;
import io.netty.channel.embedded.EmbeddedChannel;
import io.netty.channel.local.LocalAddress;
import io.netty.channel.local.LocalChannel;
import io.netty.channel.local.LocalHandler;
import io.netty.channel.local.LocalServerChannel;
import io.netty.channel.nio.NioHandler;
import io.netty.channel.socket.nio.NioSocketChannel;
import io.netty.util.AbstractReferenceCounted;
import io.netty.util.ReferenceCountUtil;
import io.netty.util.ReferenceCounted;
import io.netty.util.concurrent.AbstractEventExecutor;
import io.netty.util.concurrent.EventExecutor;
import io.netty.util.concurrent.Future;
import io.netty.util.concurrent.ImmediateEventExecutor;
import io.netty.util.concurrent.Promise;
import io.netty.util.concurrent.ScheduledFuture;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Test;

import java.net.SocketAddress;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.NoSuchElementException;
import java.util.Queue;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class DefaultChannelPipelineTest {

    private static final EventLoopGroup group = new MultithreadEventLoopGroup(1, LocalHandler.newFactory());

    private Channel self;
    private Channel peer;

    @AfterClass
    public static void afterClass() throws Exception {
        group.shutdownGracefully().sync();
    }

    private void setUp(final ChannelHandler... handlers) throws Exception {
        final AtomicReference<Channel> peerRef = new AtomicReference<>();
        ServerBootstrap sb = new ServerBootstrap();
        sb.group(group).channel(LocalServerChannel.class);
        sb.childHandler(new ChannelHandler() {
            @Override
            public void channelRegistered(ChannelHandlerContext ctx) throws Exception {
                peerRef.set(ctx.channel());
            }

            @Override
            public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
                ReferenceCountUtil.release(msg);
            }
        });

        ChannelFuture bindFuture = sb.bind(LocalAddress.ANY).sync();

        Bootstrap b = new Bootstrap();
        b.group(group).channel(LocalChannel.class);
        b.handler(new ChannelInitializer<LocalChannel>() {
            @Override
            protected void initChannel(LocalChannel ch) throws Exception {
                ch.pipeline().addLast(handlers);
            }
        });

        self = b.connect(bindFuture.channel().localAddress()).sync().channel();
        peer = peerRef.get();

        bindFuture.channel().close().sync();
    }

    @After
    public void tearDown() throws Exception {
        if (peer != null) {
            peer.close();
            peer = null;
        }
        if (self != null) {
            self = null;
        }
    }

    @Test
    public void testFreeCalled() throws Exception {
        final CountDownLatch free = new CountDownLatch(1);

        final ReferenceCounted holder = new AbstractReferenceCounted() {
            @Override
            protected void deallocate() {
                free.countDown();
            }

            @Override
            public ReferenceCounted touch(Object hint) {
                return this;
            }
        };

        StringInboundHandler handler = new StringInboundHandler();
        setUp(handler);

        peer.writeAndFlush(holder).sync();

        assertTrue(free.await(10, TimeUnit.SECONDS));
        assertTrue(handler.called);
    }

    private static final class StringInboundHandler implements ChannelInboundHandler {
        boolean called;

        @Override
        public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
            called = true;
            if (!(msg instanceof String)) {
                ctx.fireChannelRead(msg);
            }
        }
    }

    private static LocalChannel newLocalChannel() {
        return new LocalChannel(group.next());
    }

    @Test
    public void testRemoveChannelHandler() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();

        ChannelHandler handler1 = newHandler();
        ChannelHandler handler2 = newHandler();
        ChannelHandler handler3 = newHandler();

        pipeline.addLast("handler1", handler1);
        pipeline.addLast("handler2", handler2);
        pipeline.addLast("handler3", handler3);
        assertSame(pipeline.get("handler1"), handler1);
        assertSame(pipeline.get("handler2"), handler2);
        assertSame(pipeline.get("handler3"), handler3);

        pipeline.remove(handler1);
        assertNull(pipeline.get("handler1"));
        pipeline.remove(handler2);
        assertNull(pipeline.get("handler2"));
        pipeline.remove(handler3);
        assertNull(pipeline.get("handler3"));
    }

    @Test
    public void testRemoveIfExists() {
        DefaultChannelPipeline pipeline = new DefaultChannelPipeline(newLocalChannel());

        ChannelHandler handler1 = newHandler();
        ChannelHandler handler2 = newHandler();
        ChannelHandler handler3 = newHandler();

        pipeline.addLast("handler1", handler1);
        pipeline.addLast("handler2", handler2);
        pipeline.addLast("handler3", handler3);

        assertNotNull(pipeline.removeIfExists(handler1));
        assertNull(pipeline.get("handler1"));

        assertNotNull(pipeline.removeIfExists("handler2"));
        assertNull(pipeline.get("handler2"));

        assertNotNull(pipeline.removeIfExists(TestHandler.class));
        assertNull(pipeline.get("handler3"));
    }

    @Test
    public void testRemoveIfExistsDoesNotThrowException() {
        DefaultChannelPipeline pipeline = new DefaultChannelPipeline(newLocalChannel());

        ChannelHandler handler1 = newHandler();
        ChannelHandler handler2 = newHandler();
        pipeline.addLast("handler1", handler1);

        assertNull(pipeline.removeIfExists("handlerXXX"));
        assertNull(pipeline.removeIfExists(handler2));

        class NonExistingHandler implements ChannelHandler { }

        assertNull(pipeline.removeIfExists(NonExistingHandler.class));
        assertNotNull(pipeline.get("handler1"));
    }

    @Test(expected = NoSuchElementException.class)
    public void testRemoveThrowNoSuchElementException() {
        DefaultChannelPipeline pipeline = new DefaultChannelPipeline(newLocalChannel());

        ChannelHandler handler1 = newHandler();
        pipeline.addLast("handler1", handler1);

        pipeline.remove("handlerXXX");
    }

    @Test
    public void testReplaceChannelHandler() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();

        ChannelHandler handler1 = newHandler();
        pipeline.addLast("handler1", handler1);
        pipeline.addLast("handler2", handler1);
        pipeline.addLast("handler3", handler1);
        assertSame(pipeline.get("handler1"), handler1);
        assertSame(pipeline.get("handler2"), handler1);
        assertSame(pipeline.get("handler3"), handler1);

        ChannelHandler newHandler1 = newHandler();
        pipeline.replace("handler1", "handler1", newHandler1);
        assertSame(pipeline.get("handler1"), newHandler1);

        ChannelHandler newHandler3 = newHandler();
        pipeline.replace("handler3", "handler3", newHandler3);
        assertSame(pipeline.get("handler3"), newHandler3);

        ChannelHandler newHandler2 = newHandler();
        pipeline.replace("handler2", "handler2", newHandler2);
        assertSame(pipeline.get("handler2"), newHandler2);
    }

    @Test
    public void testChannelHandlerContextNavigation() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();

        final int HANDLER_ARRAY_LEN = 5;
        ChannelHandler[] firstHandlers = newHandlers(HANDLER_ARRAY_LEN);
        ChannelHandler[] lastHandlers = newHandlers(HANDLER_ARRAY_LEN);

        pipeline.addFirst(firstHandlers);
        pipeline.addLast(lastHandlers);

        verifyContextNumber(pipeline, HANDLER_ARRAY_LEN * 2);
    }

    @Test
    public void testFireChannelRegistered() throws Exception {
        final CountDownLatch latch = new CountDownLatch(1);
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.addLast(new ChannelInitializer<Channel>() {
            @Override
            protected void initChannel(Channel ch) throws Exception {
                ch.pipeline().addLast(new ChannelHandler() {
                    @Override
                    public void channelRegistered(ChannelHandlerContext ctx) throws Exception {
                        latch.countDown();
                    }
                });
            }
        });
        pipeline.channel().register();
        assertTrue(latch.await(2, TimeUnit.SECONDS));
    }

    @Test
    public void testPipelineOperation() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();

        final int handlerNum = 5;
        ChannelHandler[] handlers1 = newHandlers(handlerNum);
        ChannelHandler[] handlers2 = newHandlers(handlerNum);

        final String prefixX = "x";
        for (int i = 0; i < handlerNum; i++) {
            if (i % 2 == 0) {
                pipeline.addFirst(prefixX + i, handlers1[i]);
            } else {
                pipeline.addLast(prefixX + i, handlers1[i]);
            }
        }

        for (int i = 0; i < handlerNum; i++) {
            if (i % 2 != 0) {
                pipeline.addBefore(prefixX + i, String.valueOf(i), handlers2[i]);
            } else {
                pipeline.addAfter(prefixX + i, String.valueOf(i), handlers2[i]);
            }
        }

        verifyContextNumber(pipeline, handlerNum * 2);
    }

    @Test
    public void testChannelHandlerContextOrder() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();

        pipeline.addFirst("1", newHandler());
        pipeline.addLast("10", newHandler());

        pipeline.addBefore("10", "5", newHandler());
        pipeline.addAfter("1", "3", newHandler());
        pipeline.addBefore("5", "4", newHandler());
        pipeline.addAfter("5", "6", newHandler());

        pipeline.addBefore("1", "0", newHandler());
        pipeline.addAfter("10", "11", newHandler());

        DefaultChannelHandlerContext ctx = (DefaultChannelHandlerContext) pipeline.firstContext();
        assertNotNull(ctx);
        while (ctx != null) {
            int i = toInt(ctx.name());
            int j = next(ctx);
            if (j != -1) {
                assertTrue(i < j);
            } else {
                assertNull(ctx.next.next);
            }
            ctx = ctx.next;
        }

        verifyContextNumber(pipeline, 8);
    }

    @Test(timeout = 10000)
    public void testLifeCycleAwareness() throws Exception {
        setUp();

        ChannelPipeline p = self.pipeline();

        final List<LifeCycleAwareTestHandler> handlers = new ArrayList<>();
        final int COUNT = 20;
        final CountDownLatch addLatch = new CountDownLatch(COUNT);
        for (int i = 0; i < COUNT; i++) {
            final LifeCycleAwareTestHandler handler = new LifeCycleAwareTestHandler("handler-" + i);

            // Add handler.
            p.addFirst(handler.name, handler);
            self.eventLoop().execute(() -> {
                // Validate handler life-cycle methods called.
                handler.validate(true, false);

                // Store handler into the list.
                handlers.add(handler);

                addLatch.countDown();
            });
        }
        addLatch.await();

        // Change the order of remove operations over all handlers in the pipeline.
        Collections.shuffle(handlers);

        final CountDownLatch removeLatch = new CountDownLatch(COUNT);

        for (final LifeCycleAwareTestHandler handler : handlers) {
            assertSame(handler, p.remove(handler.name));

            self.eventLoop().execute(() -> {
                // Validate handler life-cycle methods called.
                handler.validate(true, true);
                removeLatch.countDown();
            });
        }
        removeLatch.await();
    }

    @Test(timeout = 100000)
    public void testRemoveAndForwardInbound() throws Exception {
        final BufferedTestHandler handler1 = new BufferedTestHandler();
        final BufferedTestHandler handler2 = new BufferedTestHandler();

        setUp(handler1, handler2);

        self.eventLoop().submit(() -> {
            ChannelPipeline p = self.pipeline();
            handler1.inboundBuffer.add(8);
            assertEquals(8, handler1.inboundBuffer.peek());
            assertTrue(handler2.inboundBuffer.isEmpty());
            p.remove(handler1);
            assertEquals(1, handler2.inboundBuffer.size());
            assertEquals(8, handler2.inboundBuffer.peek());
        }).sync();
    }

    @Test(timeout = 10000)
    public void testRemoveAndForwardOutbound() throws Exception {
        final BufferedTestHandler handler1 = new BufferedTestHandler();
        final BufferedTestHandler handler2 = new BufferedTestHandler();

        setUp(handler1, handler2);

        self.eventLoop().submit(() -> {
            ChannelPipeline p = self.pipeline();
            handler2.outboundBuffer.add(8);
            assertEquals(8, handler2.outboundBuffer.peek());
            assertTrue(handler1.outboundBuffer.isEmpty());
            p.remove(handler2);
            assertEquals(1, handler1.outboundBuffer.size());
            assertEquals(8, handler1.outboundBuffer.peek());
        }).sync();
    }

    @Test(timeout = 10000)
    public void testReplaceAndForwardOutbound() throws Exception {
        final BufferedTestHandler handler1 = new BufferedTestHandler();
        final BufferedTestHandler handler2 = new BufferedTestHandler();

        setUp(handler1);

        self.eventLoop().submit(() -> {
            ChannelPipeline p = self.pipeline();
            handler1.outboundBuffer.add(8);
            assertEquals(8, handler1.outboundBuffer.peek());
            assertTrue(handler2.outboundBuffer.isEmpty());
            p.replace(handler1, "handler2", handler2);
            assertEquals(8, handler2.outboundBuffer.peek());
        }).sync();
    }

    @Test(timeout = 10000)
    public void testReplaceAndForwardInboundAndOutbound() throws Exception {
        final BufferedTestHandler handler1 = new BufferedTestHandler();
        final BufferedTestHandler handler2 = new BufferedTestHandler();

        setUp(handler1);

        self.eventLoop().submit(() -> {
            ChannelPipeline p = self.pipeline();
            handler1.inboundBuffer.add(8);
            handler1.outboundBuffer.add(8);

            assertEquals(8, handler1.inboundBuffer.peek());
            assertEquals(8, handler1.outboundBuffer.peek());
            assertTrue(handler2.inboundBuffer.isEmpty());
            assertTrue(handler2.outboundBuffer.isEmpty());

            p.replace(handler1, "handler2", handler2);
            assertEquals(8, handler2.outboundBuffer.peek());
            assertEquals(8, handler2.inboundBuffer.peek());
        }).sync();
    }

    @Test(timeout = 10000)
    public void testRemoveAndForwardInboundOutbound() throws Exception {
        final BufferedTestHandler handler1 = new BufferedTestHandler();
        final BufferedTestHandler handler2 = new BufferedTestHandler();
        final BufferedTestHandler handler3 = new BufferedTestHandler();

        setUp(handler1, handler2, handler3);

        self.eventLoop().submit(() -> {
            ChannelPipeline p = self.pipeline();
            handler2.inboundBuffer.add(8);
            handler2.outboundBuffer.add(8);

            assertEquals(8, handler2.inboundBuffer.peek());
            assertEquals(8, handler2.outboundBuffer.peek());

            assertEquals(0, handler1.outboundBuffer.size());
            assertEquals(0, handler3.inboundBuffer.size());

            p.remove(handler2);
            assertEquals(8, handler3.inboundBuffer.peek());
            assertEquals(8, handler1.outboundBuffer.peek());
        }).sync();
    }

    // Tests for https://github.com/netty/netty/issues/2349
    @Test
    public void testCancelBind() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ChannelFuture future = pipeline.bind(new LocalAddress("test"), promise);
        assertTrue(future.isCancelled());
    }

    @Test
    public void testCancelConnect() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ChannelFuture future = pipeline.connect(new LocalAddress("test"), promise);
        assertTrue(future.isCancelled());
    }

    @Test
    public void testCancelDisconnect() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ChannelFuture future = pipeline.disconnect(promise);
        assertTrue(future.isCancelled());
    }

    @Test
    public void testCancelClose() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ChannelFuture future = pipeline.close(promise);
        assertTrue(future.isCancelled());
    }

    @Test(expected = IllegalArgumentException.class)
    public void testWrongPromiseChannel() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        ChannelPipeline pipeline2 = newLocalChannel().pipeline();
        pipeline2.channel().register().sync();

        try {
            ChannelPromise promise2 = pipeline2.channel().newPromise();
            pipeline.close(promise2);
        } finally {
            pipeline.close();
            pipeline2.close();
        }
    }

    @Test(expected = IllegalArgumentException.class)
    public void testUnexpectedVoidChannelPromise() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        try {
            ChannelPromise promise = new VoidChannelPromise(pipeline.channel(), false);
            pipeline.close(promise);
        } finally {
            pipeline.close();
        }
    }

    @Test(expected = IllegalArgumentException.class)
    public void testUnexpectedVoidChannelPromiseCloseFuture() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        try {
            ChannelPromise promise = (ChannelPromise) pipeline.channel().closeFuture();
            pipeline.close(promise);
        } finally {
            pipeline.close();
        }
    }

    @Test
    public void testCancelDeregister() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ChannelFuture future = pipeline.deregister(promise);
        assertTrue(future.isCancelled());
    }

    @Test
    public void testCancelWrite() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ByteBuf buffer = Unpooled.buffer();
        assertEquals(1, buffer.refCnt());
        ChannelFuture future = pipeline.write(buffer, promise);
        assertTrue(future.isCancelled());
        assertEquals(0, buffer.refCnt());
    }

    @Test
    public void testCancelWriteAndFlush() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.channel().register().sync();

        ChannelPromise promise = pipeline.channel().newPromise();
        assertTrue(promise.cancel(false));
        ByteBuf buffer = Unpooled.buffer();
        assertEquals(1, buffer.refCnt());
        ChannelFuture future = pipeline.writeAndFlush(buffer, promise);
        assertTrue(future.isCancelled());
        assertEquals(0, buffer.refCnt());
    }

    @Test
    public void testFirstContextEmptyPipeline() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        assertNull(pipeline.firstContext());
    }

    @Test
    public void testLastContextEmptyPipeline() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        assertNull(pipeline.lastContext());
    }

    @Test
    public void testFirstHandlerEmptyPipeline() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        assertNull(pipeline.first());
    }

    @Test
    public void testLastHandlerEmptyPipeline() throws Exception {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        assertNull(pipeline.last());
    }

    @Test(timeout = 5000)
    public void testChannelInitializerException() throws Exception {
        final IllegalStateException exception = new IllegalStateException();
        final AtomicReference<Throwable> error = new AtomicReference<>();
        final CountDownLatch latch = new CountDownLatch(1);
        EmbeddedChannel channel = new EmbeddedChannel(false, false, new ChannelInitializer<Channel>() {
            @Override
            protected void initChannel(Channel ch) throws Exception {
                throw exception;
            }

            @Override
            public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
                super.exceptionCaught(ctx, cause);
                error.set(cause);
                latch.countDown();
            }
        });
        latch.await();
        assertFalse(channel.isActive());
        assertSame(exception, error.get());
    }

    @Test(timeout = 3000)
    public void testAddHandlerBeforeRegisteredThenRemove() {
        final EventLoop loop = group.next();

        CheckEventExecutorHandler handler = new CheckEventExecutorHandler(loop);
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.addFirst(handler);
        handler.addedPromise.syncUninterruptibly();
        pipeline.channel().register();
        pipeline.remove(handler);
        handler.removedPromise.syncUninterruptibly();

        pipeline.channel().close().syncUninterruptibly();
    }

    @Test(timeout = 3000)
    public void testAddHandlerBeforeRegisteredThenReplace() throws Exception {
        final EventLoop loop = group.next();
        final CountDownLatch latch = new CountDownLatch(1);

        CheckEventExecutorHandler handler = new CheckEventExecutorHandler(loop);
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.addFirst(handler);
        handler.addedPromise.syncUninterruptibly();
        pipeline.channel().register();
        pipeline.replace(handler, null, new ChannelHandlerAdapter() {
            @Override
            public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
                latch.countDown();
            }
        });
        handler.removedPromise.syncUninterruptibly();
        latch.await();

        pipeline.channel().close().syncUninterruptibly();
    }

    @Test(timeout = 2000)
    public void testAddRemoveHandlerCalled() throws Throwable {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        CallbackCheckHandler handler = new CallbackCheckHandler();

        pipeline.addFirst(handler);
        pipeline.remove(handler);

        assertTrue(handler.addedHandler.get());
        assertTrue(handler.removedHandler.get());

        pipeline.channel().register().syncUninterruptibly();
        Throwable cause = handler.error.get();
        pipeline.channel().close().syncUninterruptibly();

        if (cause != null) {
            throw cause;
        }
    }

    @Test(timeout = 3000)
    public void testAddReplaceHandlerCalled() throws Throwable {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        CallbackCheckHandler handler = new CallbackCheckHandler();
        CallbackCheckHandler handler2 = new CallbackCheckHandler();

        pipeline.addFirst(handler);
        pipeline.replace(handler, null, handler2);

        assertTrue(handler.addedHandler.get());
        assertTrue(handler.removedHandler.get());
        assertTrue(handler2.addedHandler.get());
        assertNull(handler2.removedHandler.getNow());

        pipeline.channel().register().syncUninterruptibly();
        Throwable cause = handler.error.get();
        if (cause != null) {
            throw cause;
        }

        Throwable cause2 = handler2.error.get();
        if (cause2 != null) {
            throw cause2;
        }

        assertNull(handler2.removedHandler.getNow());
        pipeline.remove(handler2);
        assertTrue(handler2.removedHandler.get());
        pipeline.channel().close().syncUninterruptibly();
    }

    @Test(timeout = 3000)
    public void testAddBefore() throws Throwable {
        EventLoopGroup defaultGroup = new MultithreadEventLoopGroup(2, LocalHandler.newFactory());
        try {
            EventLoop eventLoop1 = defaultGroup.next();
            EventLoop eventLoop2 = defaultGroup.next();

            ChannelPipeline pipeline1 = new LocalChannel(eventLoop1).pipeline();
            ChannelPipeline pipeline2 = new LocalChannel(eventLoop2).pipeline();

            pipeline1.channel().register().syncUninterruptibly();
            pipeline2.channel().register().syncUninterruptibly();

            CountDownLatch latch = new CountDownLatch(2 * 10);
            for (int i = 0; i < 10; i++) {
                eventLoop1.execute(new TestTask(pipeline2, latch));
                eventLoop2.execute(new TestTask(pipeline1, latch));
            }
            latch.await();
            pipeline1.channel().close().syncUninterruptibly();
            pipeline2.channel().close().syncUninterruptibly();
        } finally {
            defaultGroup.shutdownGracefully();
        }
    }

    @Test(timeout = 3000)
    public void testAddInListenerNio() throws Throwable {
        EventLoopGroup nioEventLoopGroup = new MultithreadEventLoopGroup(1, NioHandler.newFactory());
        try {
            testAddInListener(new NioSocketChannel(nioEventLoopGroup.next()));
        } finally {
            nioEventLoopGroup.shutdownGracefully();
        }
    }

    @Test(timeout = 3000)
    public void testAddInListenerLocal() throws Throwable {
        testAddInListener(newLocalChannel());
    }

    private static void testAddInListener(Channel channel) throws Throwable {
        ChannelPipeline pipeline1 = channel.pipeline();
        try {
            final Object event = new Object();
            final Promise<Object> promise = ImmediateEventExecutor.INSTANCE.newPromise();
            pipeline1.channel().register().addListener((ChannelFutureListener) future -> {
                ChannelPipeline pipeline = future.channel().pipeline();
                final AtomicBoolean handlerAddedCalled = new AtomicBoolean();
                pipeline.addLast(new ChannelHandler() {
                    @Override
                    public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
                        handlerAddedCalled.set(true);
                    }

                    @Override
                    public void userEventTriggered(ChannelHandlerContext ctx, Object evt) throws Exception {
                        promise.setSuccess(event);
                    }

                    @Override
                    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
                        promise.setFailure(cause);
                    }
                });
                if (!handlerAddedCalled.get()) {
                    promise.setFailure(new AssertionError("handlerAdded(...) should have been called"));
                    return;
                }
                // This event must be captured by the added handler.
                pipeline.fireUserEventTriggered(event);
            });
            assertSame(event, promise.syncUninterruptibly().getNow());
        } finally {
            pipeline1.channel().close().syncUninterruptibly();
        }
    }

    @Test
    public void testNullName() {
        ChannelPipeline pipeline = newLocalChannel().pipeline();
        pipeline.addLast(newHandler());
        pipeline.addLast(null, newHandler());
        pipeline.addFirst(newHandler());
        pipeline.addFirst(null, newHandler());

        pipeline.addLast("test", newHandler());
        pipeline.addAfter("test", null, newHandler());

        pipeline.addBefore("test", null, newHandler());
    }

    @Test(timeout = 3000)
    public void testVoidPromiseNotify() throws Throwable {
        EventLoopGroup defaultGroup = new MultithreadEventLoopGroup(1, LocalHandler.newFactory());
        EventLoop eventLoop1 = defaultGroup.next();
        ChannelPipeline pipeline1 = new LocalChannel(eventLoop1).pipeline();

        final Promise<Throwable> promise = eventLoop1.newPromise();
        final Exception exception = new IllegalArgumentException();
        try {
            pipeline1.channel().register().syncUninterruptibly();
            pipeline1.addLast(new ChannelHandler() {
                @Override
                public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) throws Exception {
                    throw exception;
                }

                @Override
                public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
                    promise.setSuccess(cause);
                }
            });
            pipeline1.write("test", pipeline1.voidPromise());
            assertSame(exception, promise.syncUninterruptibly().getNow());
        } finally {
            pipeline1.channel().close().syncUninterruptibly();
            defaultGroup.shutdownGracefully();
        }
    }

    // Test for https://github.com/netty/netty/issues/8676.
    @Test
    public void testHandlerRemovedOnlyCalledWhenHandlerAddedCalled() throws Exception {
        EventLoopGroup group = new MultithreadEventLoopGroup(1, LocalHandler.newFactory());
        try {
            final AtomicReference<Error> errorRef = new AtomicReference<>();

            // As this only happens via a race we will verify 500 times. This was good enough to have it failed most of
            // the time.
            for (int i = 0; i < 500; i++) {

                ChannelPipeline pipeline = new LocalChannel(group.next()).pipeline();
                pipeline.channel().register().sync();

                final CountDownLatch latch = new CountDownLatch(1);

                pipeline.addLast(new ChannelHandler() {
                    @Override
                    public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
                        // Block just for a bit so we have a chance to trigger the race mentioned in the issue.
                        latch.await(50, TimeUnit.MILLISECONDS);
                    }
                });

                // Close the pipeline which will call destroy0(). This will remove each handler in the pipeline and
                // should call handlerRemoved(...) if and only if handlerAdded(...) was called for the handler before.
                pipeline.close();

                pipeline.addLast(new ChannelHandler() {
                    private boolean handerAddedCalled;

                    @Override
                    public void handlerAdded(ChannelHandlerContext ctx) {
                        handerAddedCalled = true;
                    }

                    @Override
                    public void handlerRemoved(ChannelHandlerContext ctx) {
                        if (!handerAddedCalled) {
                            errorRef.set(new AssertionError(
                                    "handlerRemoved(...) called without handlerAdded(...) before"));
                        }
                    }
                });

                latch.countDown();

                pipeline.channel().closeFuture().syncUninterruptibly();

                // Schedule something on the EventLoop to ensure all other scheduled tasks had a chance to complete.
                pipeline.channel().eventLoop().submit(() -> {
                    // NOOP
                }).syncUninterruptibly();
                Error error = errorRef.get();
                if (error != null) {
                    throw error;
                }
            }
        } finally {
            group.shutdownGracefully();
        }
    }

    @Test
    public void testSkipHandlerMethodsIfAnnotated() {
        EmbeddedChannel channel = new EmbeddedChannel(true);
        ChannelPipeline pipeline = channel.pipeline();

        final class SkipHandler implements ChannelInboundHandler, ChannelOutboundHandler {
            private int state = 2;
            private Error errorRef;

            private void fail() {
                errorRef = new AssertionError("Method should never been called");
            }

            @Skip
            @Override
            public void bind(ChannelHandlerContext ctx, SocketAddress localAddress, ChannelPromise promise) {
                fail();
                ctx.bind(localAddress, promise);
            }

            @Skip
            @Override
            public void connect(ChannelHandlerContext ctx, SocketAddress remoteAddress,
                                SocketAddress localAddress, ChannelPromise promise) {
                fail();
                ctx.connect(remoteAddress, localAddress, promise);
            }

            @Skip
            @Override
            public void disconnect(ChannelHandlerContext ctx, ChannelPromise promise) {
                fail();
                ctx.disconnect(promise);
            }

            @Skip
            @Override
            public void close(ChannelHandlerContext ctx, ChannelPromise promise) {
                fail();
                ctx.close(promise);
            }

            @Skip
            @Override
            public void register(ChannelHandlerContext ctx, ChannelPromise promise) {
                fail();
                ctx.register(promise);
            }

            @Skip
            @Override
            public void deregister(ChannelHandlerContext ctx, ChannelPromise promise) {
                fail();
                ctx.deregister(promise);
            }

            @Skip
            @Override
            public void read(ChannelHandlerContext ctx) {
                fail();
                ctx.read();
            }

            @Skip
            @Override
            public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) {
                fail();
                ctx.write(msg, promise);
            }

            @Skip
            @Override
            public void flush(ChannelHandlerContext ctx) {
                fail();
                ctx.flush();
            }

            @Skip
            @Override
            public void channelRegistered(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelRegistered();
            }

            @Skip
            @Override
            public void channelUnregistered(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelUnregistered();
            }

            @Skip
            @Override
            public void channelActive(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelActive();
            }

            @Skip
            @Override
            public void channelInactive(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelInactive();
            }

            @Skip
            @Override
            public void channelRead(ChannelHandlerContext ctx, Object msg) {
                fail();
                ctx.fireChannelRead(msg);
            }

            @Skip
            @Override
            public void channelReadComplete(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelReadComplete();
            }

            @Skip
            @Override
            public void userEventTriggered(ChannelHandlerContext ctx, Object evt) {
                fail();
                ctx.fireUserEventTriggered(evt);
            }

            @Skip
            @Override
            public void channelWritabilityChanged(ChannelHandlerContext ctx) {
                fail();
                ctx.fireChannelWritabilityChanged();
            }

            @Skip
            @Override
            public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
                fail();
                ctx.fireExceptionCaught(cause);
            }

            @Override
            public void handlerAdded(ChannelHandlerContext ctx) {
                state--;
            }

            @Override
            public void handlerRemoved(ChannelHandlerContext ctx) {
                state--;
            }

            void assertSkipped() {
                assertEquals(0, state);
                Error error = errorRef;
                if (error != null) {
                    throw error;
                }
            }
        }

        final class OutboundCalledHandler implements ChannelOutboundHandler {
            private static final int MASK_BIND = 1;
            private static final int MASK_CONNECT = 1 << 1;
            private static final int MASK_DISCONNECT = 1 << 2;
            private static final int MASK_CLOSE = 1 << 3;
            private static final int MASK_REGISTER = 1 << 4;
            private static final int MASK_DEREGISTER = 1 << 5;
            private static final int MASK_READ = 1 << 6;
            private static final int MASK_WRITE = 1 << 7;
            private static final int MASK_FLUSH = 1 << 8;
            private static final int MASK_ADDED = 1 << 9;
            private static final int MASK_REMOVED = 1 << 10;

            private int executionMask;

            @Override
            public void handlerAdded(ChannelHandlerContext ctx) {
                executionMask |= MASK_ADDED;
            }

            @Override
            public void handlerRemoved(ChannelHandlerContext ctx) {
                executionMask |= MASK_REMOVED;
            }

            @Override
            public void bind(ChannelHandlerContext ctx, SocketAddress localAddress, ChannelPromise promise) {
                executionMask |= MASK_BIND;
                promise.setSuccess();
            }

            @Override
            public void connect(ChannelHandlerContext ctx, SocketAddress remoteAddress,
                                SocketAddress localAddress, ChannelPromise promise) {
                executionMask |= MASK_CONNECT;
                promise.setSuccess();
            }

            @Override
            public void disconnect(ChannelHandlerContext ctx, ChannelPromise promise) {
                executionMask |= MASK_DISCONNECT;
                promise.setSuccess();
            }

            @Override
            public void close(ChannelHandlerContext ctx, ChannelPromise promise) {
                executionMask |= MASK_CLOSE;
                promise.setSuccess();
            }

            @Override
            public void register(ChannelHandlerContext ctx, ChannelPromise promise) {
                executionMask |= MASK_REGISTER;
                promise.setSuccess();
            }

            @Override
            public void deregister(ChannelHandlerContext ctx, ChannelPromise promise) {
                executionMask |= MASK_DEREGISTER;
                promise.setSuccess();
            }

            @Override
            public void read(ChannelHandlerContext ctx) {
                executionMask |= MASK_READ;
            }

            @Override
            public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) {
                executionMask |= MASK_WRITE;
                promise.setSuccess();
            }

            @Override
            public void flush(ChannelHandlerContext ctx) {
                executionMask |= MASK_FLUSH;
            }

            void assertCalled() {
                assertCalled("handlerAdded", MASK_ADDED);
                assertCalled("handlerRemoved", MASK_REMOVED);
                assertCalled("bind", MASK_BIND);
                assertCalled("connect", MASK_CONNECT);
                assertCalled("disconnect", MASK_DISCONNECT);
                assertCalled("close", MASK_CLOSE);
                assertCalled("register", MASK_REGISTER);
                assertCalled("deregister", MASK_DEREGISTER);
                assertCalled("read", MASK_READ);
                assertCalled("write", MASK_WRITE);
                assertCalled("flush", MASK_FLUSH);
            }

            private void assertCalled(String methodName, int mask) {
                assertTrue(methodName + " was not called", (executionMask & mask) != 0);
            }
        }

        final class InboundCalledHandler implements ChannelInboundHandler {

            private static final int MASK_CHANNEL_REGISTER = 1;
            private static final int MASK_CHANNEL_UNREGISTER = 1 << 1;
            private static final int MASK_CHANNEL_ACTIVE = 1 << 2;
            private static final int MASK_CHANNEL_INACTIVE = 1 << 3;
            private static final int MASK_CHANNEL_READ = 1 << 4;
            private static final int MASK_CHANNEL_READ_COMPLETE = 1 << 5;
            private static final int MASK_USER_EVENT_TRIGGERED = 1 << 6;
            private static final int MASK_CHANNEL_WRITABILITY_CHANGED = 1 << 7;
            private static final int MASK_EXCEPTION_CAUGHT = 1 << 8;
            private static final int MASK_ADDED = 1 << 9;
            private static final int MASK_REMOVED = 1 << 10;

            private int executionMask;

            @Override
            public void handlerAdded(ChannelHandlerContext ctx) {
                executionMask |= MASK_ADDED;
            }

            @Override
            public void handlerRemoved(ChannelHandlerContext ctx) {
                executionMask |= MASK_REMOVED;
            }

            @Override
            public void channelRegistered(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_REGISTER;
            }

            @Override
            public void channelUnregistered(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_UNREGISTER;
            }

            @Override
            public void channelActive(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_ACTIVE;
            }

            @Override
            public void channelInactive(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_INACTIVE;
            }

            @Override
            public void channelRead(ChannelHandlerContext ctx, Object msg) {
                executionMask |= MASK_CHANNEL_READ;
            }

            @Override
            public void channelReadComplete(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_READ_COMPLETE;
            }

            @Override
            public void userEventTriggered(ChannelHandlerContext ctx, Object evt) {
                executionMask |= MASK_USER_EVENT_TRIGGERED;
            }

            @Override
            public void channelWritabilityChanged(ChannelHandlerContext ctx) {
                executionMask |= MASK_CHANNEL_WRITABILITY_CHANGED;
            }

            @Override
            public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
                executionMask |= MASK_EXCEPTION_CAUGHT;
            }

            void assertCalled() {
                assertCalled("handlerAdded", MASK_ADDED);
                assertCalled("handlerRemoved", MASK_REMOVED);
                assertCalled("channelRegistered", MASK_CHANNEL_REGISTER);
                assertCalled("channelUnregistered", MASK_CHANNEL_UNREGISTER);
                assertCalled("channelActive", MASK_CHANNEL_ACTIVE);
                assertCalled("channelInactive", MASK_CHANNEL_INACTIVE);
                assertCalled("channelRead", MASK_CHANNEL_READ);
                assertCalled("channelReadComplete", MASK_CHANNEL_READ_COMPLETE);
                assertCalled("userEventTriggered", MASK_USER_EVENT_TRIGGERED);
                assertCalled("channelWritabilityChanged", MASK_CHANNEL_WRITABILITY_CHANGED);
                assertCalled("exceptionCaught", MASK_EXCEPTION_CAUGHT);
            }

            private void assertCalled(String methodName, int mask) {
                assertTrue(methodName + " was not called", (executionMask & mask) != 0);
            }
        }

        OutboundCalledHandler outboundCalledHandler = new OutboundCalledHandler();
        SkipHandler skipHandler = new SkipHandler();
        InboundCalledHandler inboundCalledHandler = new InboundCalledHandler();
        pipeline.addLast(outboundCalledHandler, skipHandler, inboundCalledHandler);

        pipeline.fireChannelRegistered();
        pipeline.fireChannelUnregistered();
        pipeline.fireChannelActive();
        pipeline.fireChannelInactive();
        pipeline.fireChannelRead("");
        pipeline.fireChannelReadComplete();
        pipeline.fireChannelWritabilityChanged();
        pipeline.fireUserEventTriggered("");
        pipeline.fireExceptionCaught(new Exception());

        pipeline.register().syncUninterruptibly();
        pipeline.deregister().syncUninterruptibly();
        pipeline.bind(new SocketAddress() {
        }).syncUninterruptibly();
        pipeline.connect(new SocketAddress() {
        }).syncUninterruptibly();
        pipeline.disconnect().syncUninterruptibly();
        pipeline.close().syncUninterruptibly();
        pipeline.write("");
        pipeline.flush();
        pipeline.read();

        pipeline.remove(outboundCalledHandler);
        pipeline.remove(inboundCalledHandler);
        pipeline.remove(skipHandler);

        assertFalse(channel.finish());

        outboundCalledHandler.assertCalled();
        inboundCalledHandler.assertCalled();
        skipHandler.assertSkipped();
    }

    @Test
    public void testWriteThrowsReleaseMessage() {
        testWriteThrowsReleaseMessage0(false);
    }

    @Test
    public void testWriteAndFlushThrowsReleaseMessage() {
        testWriteThrowsReleaseMessage0(true);
    }

    private void testWriteThrowsReleaseMessage0(boolean flush) {
        ReferenceCounted referenceCounted = new AbstractReferenceCounted() {
            @Override
            protected void deallocate() {
                // NOOP
            }

            @Override
            public ReferenceCounted touch(Object hint) {
                return this;
            }
        };
        assertEquals(1, referenceCounted.refCnt());

        Channel channel = new LocalChannel(group.next());
        Channel channel2 = new LocalChannel(group.next());
        channel.register().syncUninterruptibly();
        channel2.register().syncUninterruptibly();

        try {
            if (flush) {
                channel.writeAndFlush(referenceCounted, channel2.newPromise());
            } else {
                channel.write(referenceCounted, channel2.newPromise());
            }
            fail();
        } catch (IllegalArgumentException expected) {
            // expected
        }
        assertEquals(0, referenceCounted.refCnt());

        channel.close().syncUninterruptibly();
        channel2.close().syncUninterruptibly();
    }

    @Test(timeout = 5000)
    public void handlerAddedStateUpdatedBeforeHandlerAddedDoneForceEventLoop() throws InterruptedException {
        handlerAddedStateUpdatedBeforeHandlerAddedDone(true);
    }

    @Test(timeout = 5000)
    public void handlerAddedStateUpdatedBeforeHandlerAddedDoneOnCallingThread() throws InterruptedException {
        handlerAddedStateUpdatedBeforeHandlerAddedDone(false);
    }

    private static void handlerAddedStateUpdatedBeforeHandlerAddedDone(boolean executeInEventLoop)
            throws InterruptedException {
        final ChannelPipeline pipeline = newLocalChannel().pipeline();
        final Object userEvent = new Object();
        final Object writeObject = new Object();
        final CountDownLatch doneLatch = new CountDownLatch(1);

        Runnable r = () -> {
            pipeline.addLast(new ChannelHandler() {
                @Override
                public void userEventTriggered(ChannelHandlerContext ctx, Object evt) {
                    if (evt == userEvent) {
                        ctx.write(writeObject);
                    }
                    ctx.fireUserEventTriggered(evt);
                }
            });
            pipeline.addFirst(new ChannelHandler() {
                @Override
                public void handlerAdded(ChannelHandlerContext ctx) {
                    ctx.fireUserEventTriggered(userEvent);
                }

                @Override
                public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) {
                    if (msg == writeObject) {
                        doneLatch.countDown();
                    }
                    ctx.write(msg, promise);
                }
            });
        };

        if (executeInEventLoop) {
            pipeline.channel().eventLoop().execute(r);
        } else {
            r.run();
        }

        doneLatch.await();
    }

    private static final class TestTask implements Runnable {

        private final ChannelPipeline pipeline;
        private final CountDownLatch latch;

        TestTask(ChannelPipeline pipeline, CountDownLatch latch) {
            this.pipeline = pipeline;
            this.latch = latch;
        }

        @Override
        public void run() {
            pipeline.addLast(new ChannelHandler() { });
            latch.countDown();
        }
    }

    private static final class CallbackCheckHandler extends ChannelHandlerAdapter {
        final Promise<Boolean> addedHandler = ImmediateEventExecutor.INSTANCE.newPromise();
        final Promise<Boolean> removedHandler = ImmediateEventExecutor.INSTANCE.newPromise();
        final AtomicReference<Throwable> error = new AtomicReference<>();

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
            if (!addedHandler.trySuccess(true)) {
                error.set(new AssertionError("handlerAdded(...) called multiple times: " + ctx.name()));
            } else if (removedHandler.getNow() == Boolean.TRUE) {
                error.set(new AssertionError("handlerRemoved(...) called before handlerAdded(...): " + ctx.name()));
            }
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
            if (!removedHandler.trySuccess(true)) {
                error.set(new AssertionError("handlerRemoved(...) called multiple times: " + ctx.name()));
            } else if (addedHandler.getNow() == Boolean.FALSE) {
                error.set(new AssertionError("handlerRemoved(...) called before handlerAdded(...): " + ctx.name()));
            }
        }
    }

    private static final class CheckExceptionHandler implements ChannelInboundHandler {
        private final Throwable expected;
        private final Promise<Void> promise;

        CheckExceptionHandler(Throwable expected, Promise<Void> promise) {
            this.expected = expected;
            this.promise = promise;
        }

        @Override
        public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
            if (cause instanceof ChannelPipelineException && cause.getCause() == expected) {
                promise.setSuccess(null);
            } else {
                promise.setFailure(new AssertionError("cause not the expected instance"));
            }
        }
    }

    private static void assertHandler(CheckOrderHandler actual, CheckOrderHandler... handlers) throws Throwable {
        for (CheckOrderHandler h : handlers) {
            if (h == actual) {
                actual.checkError();
                return;
            }
        }
        fail("handler was not one of the expected handlers");
    }

    private static final class CheckOrderHandler implements ChannelInboundHandler {
        private final Queue<CheckOrderHandler> addedQueue;
        private final Queue<CheckOrderHandler> removedQueue;
        private final AtomicReference<Throwable> error = new AtomicReference<>();

        CheckOrderHandler(Queue<CheckOrderHandler> addedQueue, Queue<CheckOrderHandler> removedQueue) {
            this.addedQueue = addedQueue;
            this.removedQueue = removedQueue;
        }

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
            addedQueue.add(this);
            checkExecutor(ctx);
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
            removedQueue.add(this);
            checkExecutor(ctx);
        }

        @Override
        public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) throws Exception {
            error.set(cause);
        }

        void checkError() throws Throwable {
            Throwable cause = error.get();
            if (cause != null) {
                throw cause;
            }
        }

        private void checkExecutor(ChannelHandlerContext ctx) {
            if (!ctx.executor().inEventLoop()) {
                error.set(new AssertionError());
            }
        }
    }

    private static final class CheckEventExecutorHandler extends ChannelHandlerAdapter {
        final EventExecutor executor;
        final Promise<Void> addedPromise;
        final Promise<Void> removedPromise;

        CheckEventExecutorHandler(EventExecutor executor) {
            this.executor = executor;
            addedPromise = executor.newPromise();
            removedPromise = executor.newPromise();
        }

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
            assertExecutor(ctx, addedPromise);
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
            assertExecutor(ctx, removedPromise);
        }

        private void assertExecutor(ChannelHandlerContext ctx, Promise<Void> promise) {
            final boolean same;
            try {
                same = executor == ctx.executor();
            } catch (Throwable cause) {
                promise.setFailure(cause);
                return;
            }
            if (same) {
                promise.setSuccess(null);
            } else {
                promise.setFailure(new AssertionError("EventExecutor not the same"));
            }
        }
    }
    private static final class ErrorChannelHandler extends ChannelHandlerAdapter {
        private final AtomicReference<Throwable> error;

        ErrorChannelHandler(AtomicReference<Throwable> error) {
            this.error = error;
        }

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
            error.set(new AssertionError());
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
            error.set(new AssertionError());
        }
    }

    private static int next(DefaultChannelHandlerContext ctx) {
        DefaultChannelHandlerContext next = ctx.next;
        if (next == null) {
            return Integer.MAX_VALUE;
        }

        return toInt(next.name());
    }

    private static int toInt(String name) {
        try {
            return Integer.parseInt(name);
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static void verifyContextNumber(ChannelPipeline pipeline, int expectedNumber) {
        assertEquals(expectedNumber, pipeline.names().size());
        assertEquals(expectedNumber, pipeline.toMap().size());

        pipeline.executor().submit(new Runnable() {
            @Override
            public void run() {
                DefaultChannelHandlerContext ctx = (DefaultChannelHandlerContext) pipeline.firstContext();
                int handlerNumber = 0;
                if (ctx != null) {
                    for (;;) {
                        handlerNumber++;
                        if (ctx == pipeline.lastContext()) {
                            break;
                        }
                        ctx = ctx.next;
                    }
                }
                assertEquals(expectedNumber, handlerNumber);
            }
        }).syncUninterruptibly();
    }

    private static ChannelHandler[] newHandlers(int num) {
        assert num > 0;

        ChannelHandler[] handlers = new ChannelHandler[num];
        for (int i = 0; i < num; i++) {
            handlers[i] = newHandler();
        }

        return handlers;
    }

    private static ChannelHandler newHandler() {
        return new TestHandler();
    }

    @Sharable
    private static class TestHandler implements ChannelHandler { }

    private static class BufferedTestHandler implements ChannelHandler {
        final Queue<Object> inboundBuffer = new ArrayDeque<>();
        final Queue<Object> outboundBuffer = new ArrayDeque<>();

        @Override
        public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) throws Exception {
            outboundBuffer.add(msg);
        }

        @Override
        public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
            inboundBuffer.add(msg);
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
            if (!inboundBuffer.isEmpty()) {
                for (Object o: inboundBuffer) {
                    ctx.fireChannelRead(o);
                }
                ctx.fireChannelReadComplete();
            }
            if (!outboundBuffer.isEmpty()) {
                for (Object o: outboundBuffer) {
                    ctx.write(o);
                }
                ctx.flush();
            }
        }
    }

    /** Test handler to validate life-cycle aware behavior. */
    private static final class LifeCycleAwareTestHandler extends ChannelHandlerAdapter {
        private final String name;

        private boolean afterAdd;
        private boolean afterRemove;

        /**
         * Constructs life-cycle aware test handler.
         *
         * @param name Handler name to display in assertion messages.
         */
        private LifeCycleAwareTestHandler(String name) {
            this.name = name;
        }

        public void validate(boolean afterAdd, boolean afterRemove) {
            assertEquals(name, afterAdd, this.afterAdd);
            assertEquals(name, afterRemove, this.afterRemove);
        }

        @Override
        public void handlerAdded(ChannelHandlerContext ctx) {
            validate(false, false);

            afterAdd = true;
        }

        @Override
        public void handlerRemoved(ChannelHandlerContext ctx) {
            validate(true, false);

            afterRemove = true;
        }
    }

    private static final class WrapperExecutor extends AbstractEventExecutor {

        private final ExecutorService wrapped = Executors.newSingleThreadExecutor();

        @Override
        public boolean isShuttingDown() {
            return wrapped.isShutdown();
        }

        @Override
        public Future<?> shutdownGracefully(long l, long l2, TimeUnit timeUnit) {
            throw new IllegalStateException();
        }

        @Override
        public Future<?> terminationFuture() {
            throw new IllegalStateException();
        }

        @Override
        public void shutdown() {
            wrapped.shutdown();
        }

        @Override
        public List<Runnable> shutdownNow() {
            return wrapped.shutdownNow();
        }

        @Override
        public boolean isShutdown() {
            return wrapped.isShutdown();
        }

        @Override
        public boolean isTerminated() {
            return wrapped.isTerminated();
        }

        @Override
        public boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException {
            return wrapped.awaitTermination(timeout, unit);
        }

        @Override
        public boolean inEventLoop(Thread thread) {
            return false;
        }

        @Override
        public void execute(Runnable command) {
            wrapped.execute(command);
        }

        @Override
        public ScheduledFuture<?> schedule(Runnable command, long delay, TimeUnit unit) {
            throw new UnsupportedOperationException();
        }

        @Override
        public <V> ScheduledFuture<V> schedule(Callable<V> callable, long delay, TimeUnit unit) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ScheduledFuture<?> scheduleAtFixedRate(Runnable command, long initialDelay, long period, TimeUnit unit) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ScheduledFuture<?> scheduleWithFixedDelay(
                Runnable command, long initialDelay, long delay, TimeUnit unit) {
            throw new UnsupportedOperationException();
        }
    }
}
