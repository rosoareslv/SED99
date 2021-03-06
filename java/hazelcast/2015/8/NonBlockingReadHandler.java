/*
 * Copyright (c) 2008-2015, Hazelcast, Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.hazelcast.nio.tcp.nonblocking;

import com.hazelcast.internal.metrics.MetricsRegistry;
import com.hazelcast.internal.metrics.Probe;
import com.hazelcast.nio.Protocols;
import com.hazelcast.nio.ascii.SocketTextReader;
import com.hazelcast.nio.tcp.ClientMessageSocketReader;
import com.hazelcast.nio.tcp.ClientPacketSocketReader;
import com.hazelcast.nio.tcp.ReadHandler;
import com.hazelcast.nio.tcp.SocketReader;
import com.hazelcast.nio.tcp.TcpIpConnection;
import com.hazelcast.nio.tcp.WriteHandler;
import com.hazelcast.util.counters.Counter;
import com.hazelcast.util.counters.SwCounter;

import java.io.EOFException;
import java.io.IOException;
import java.net.SocketException;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;

import static com.hazelcast.nio.ConnectionType.MEMBER;
import static com.hazelcast.nio.IOService.KILO_BYTE;
import static com.hazelcast.nio.Protocols.CLIENT_BINARY;
import static com.hazelcast.nio.Protocols.CLIENT_BINARY_NEW;
import static com.hazelcast.nio.Protocols.CLUSTER;
import static com.hazelcast.util.Clock.currentTimeMillis;
import static com.hazelcast.util.StringUtil.bytesToString;
import static com.hazelcast.util.counters.SwCounter.newSwCounter;

/**
 * The reading side of the {@link com.hazelcast.nio.Connection}.
 */
public final class NonBlockingReadHandler extends AbstractSelectionHandler implements ReadHandler {

    @Probe(name = "in.eventCount")
    private final SwCounter eventCount = newSwCounter();
    @Probe(name = "in.bytesRead")
    private final SwCounter bytesRead = newSwCounter();
    @Probe(name = "in.normalPacketsRead")
    private final SwCounter normalPacketsRead = newSwCounter();
    @Probe(name = "in.priorityPacketsRead")
    private final SwCounter priorityPacketsRead = newSwCounter();
    private final MetricsRegistry metricRegistry;

    private SocketReader socketReader;
    private ByteBuffer inputBuffer;
    private volatile long lastReadTime;

    public NonBlockingReadHandler(
            TcpIpConnection connection,
            NonBlockingIOThread ioThread,
            MetricsRegistry metricsRegistry) {
        super(connection, ioThread, SelectionKey.OP_READ);
        this.ioThread = ioThread;
        this.metricRegistry = metricsRegistry;
        metricRegistry.scanAndRegister(this, "tcp.connection[" + connection.getMetricsId() + "]");
    }

    @Probe(name = "in.idleTimeMs")
    private long idleTimeMs() {
        return Math.max(System.currentTimeMillis() - lastReadTime, 0);
    }

    @Probe(name = "in.interestedOps")
    private long interestOps() {
        SelectionKey selectionKey = this.selectionKey;
        return selectionKey == null ? -1 : selectionKey.interestOps();
    }

    @Probe(name = "in.readyOps")
    private long readyOps() {
        SelectionKey selectionKey = this.selectionKey;
        return selectionKey == null ? -1 : selectionKey.readyOps();
    }

    @Override
    public Counter getNormalPacketsReadCounter() {
        return normalPacketsRead;
    }

    @Override
    public Counter getPriorityPacketsReadCounter() {
        return priorityPacketsRead;
    }

    @Override
    public long getLastReadTimeMillis() {
        return lastReadTime;
    }

    @Override
    public long getEventCount() {
        return eventCount.get();
    }

    @Override
    public void start() {
        ioThread.addTaskAndWakeup(new Runnable() {
            @Override
            public void run() {
                getSelectionKey();
            }
        });
    }

    /**
     * Migrates this handler to a new NonBlockingIOThread.
     * The migration logic is rather simple:
     * <p><ul>
     * <li>Submit a de-registration task to a current NonBlockingIOThread</li>
     * <li>The de-registration task submits a registration task to the new NonBlockingIOThread</li>
     * </ul></p>
     *
     * @param newOwner target NonBlockingIOThread this handler migrates to
     */
    @Override
    public void requestMigration(NonBlockingIOThread newOwner) {
        ioThread.addTaskAndWakeup(new StartMigrationTask(newOwner));
    }

    @Override
    public void handle() throws Exception {
        eventCount.inc();
        // we are going to set the timestamp even if the socketChannel is going to fail reading. In that case
        // the connection is going to be closed anyway.
        lastReadTime = currentTimeMillis();

        if (!connection.isAlive()) {
            logger.finest("We are being asked to read, but connection is not live so we won't");
            return;
        }

        if (socketReader == null) {
            initializeSocketReader();
            if (socketReader == null) {
                // when using SSL, we can read 0 bytes since data read from socket can be handshake packets.
                return;
            }
        }

        int readBytes = socketChannel.read(inputBuffer);
        if (readBytes <= 0) {
            if (readBytes == -1) {
                throw new EOFException("Remote socket closed!");
            }
            return;
        }

        bytesRead.inc(readBytes);

        inputBuffer.flip();
        socketReader.read(inputBuffer);
        if (inputBuffer.hasRemaining()) {
            inputBuffer.compact();
        } else {
            inputBuffer.clear();
        }
    }

    private void initializeSocketReader() throws IOException {
        if (socketReader != null) {
            return;
        }

        ByteBuffer protocolBuffer = ByteBuffer.allocate(3);
        int readBytes = socketChannel.read(protocolBuffer);
        if (readBytes == -1) {
            throw new EOFException("Could not read protocol type!");
        }

        if (readBytes == 0 && connectionManager.isSSLEnabled()) {
            // when using SSL, we can read 0 bytes since data read from socket can be handshake packets.
            return;
        }

        if (!protocolBuffer.hasRemaining()) {
            String protocol = bytesToString(protocolBuffer.array());
            WriteHandler writeHandler = connection.getWriteHandler();
            if (CLUSTER.equals(protocol)) {
                configureBuffers(ioService.getSocketReceiveBufferSize() * KILO_BYTE);
                connection.setType(MEMBER);
                writeHandler.setProtocol(CLUSTER);
                socketReader = ioService.createSocketReader(connection);
            } else if (CLIENT_BINARY.equals(protocol)) {
                configureBuffers(ioService.getSocketClientReceiveBufferSize() * KILO_BYTE);
                writeHandler.setProtocol(CLIENT_BINARY);
                socketReader = new ClientPacketSocketReader(connection, ioService);
            } else if (CLIENT_BINARY_NEW.equals(protocol)) {
                configureBuffers(ioService.getSocketClientReceiveBufferSize() * KILO_BYTE);
                writeHandler.setProtocol(CLIENT_BINARY_NEW);
                socketReader = new ClientMessageSocketReader(connection, ioService);
            } else {
                configureBuffers(ioService.getSocketReceiveBufferSize() * KILO_BYTE);
                writeHandler.setProtocol(Protocols.TEXT);
                inputBuffer.put(protocolBuffer.array());
                socketReader = new SocketTextReader(connection);
                connection.getConnectionManager().incrementTextConnections();
            }
        }

        if (socketReader == null) {
            throw new IOException("Could not initialize SocketReader!");
        }
    }

    private void configureBuffers(int size) {
        inputBuffer = ByteBuffer.allocate(size);
        try {
            connection.setReceiveBufferSize(size);
        } catch (SocketException e) {
            logger.finest("Failed to adjust TCP receive buffer of " + connection + " to "
                    + size + " B.", e);
        }
    }

    @Override
    public void shutdown() {
        //todo:
        // ioThread race, shutdown can end up on the old selector
        metricRegistry.deregister(this);
        ioThread.addTaskAndWakeup(new Runnable() {
            @Override
            public void run() {
                try {
                    socketChannel.closeInbound();
                } catch (IOException e) {
                    logger.finest("Error while closing inbound", e);
                }
            }
        });
    }

    @Override
    public String toString() {
        return connection + ".readHandler";
    }

    private class StartMigrationTask implements Runnable {
        private final NonBlockingIOThread newOwner;

        public StartMigrationTask(NonBlockingIOThread newOwner) {
            this.newOwner = newOwner;
        }

        @Override
        public void run() {
            // if there is no change, we are done
            if (ioThread == newOwner) {
                return;
            }

            startMigration(newOwner);
        }
    }
}
