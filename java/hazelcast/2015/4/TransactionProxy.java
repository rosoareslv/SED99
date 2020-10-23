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

package com.hazelcast.client.txn;

import com.hazelcast.client.connection.nio.ClientConnection;
import com.hazelcast.client.impl.HazelcastClientInstanceImpl;
import com.hazelcast.client.impl.protocol.ClientMessage;
import com.hazelcast.client.impl.protocol.parameters.TransactionCommitParameters;
import com.hazelcast.client.impl.protocol.parameters.TransactionCreateParameters;
import com.hazelcast.client.impl.protocol.parameters.TransactionCreateResultParameters;
import com.hazelcast.client.impl.protocol.parameters.TransactionPrepareParameters;
import com.hazelcast.client.impl.protocol.parameters.TransactionRollbackParameters;
import com.hazelcast.client.spi.impl.ClientInvocation;
import com.hazelcast.transaction.TransactionException;
import com.hazelcast.transaction.TransactionNotActiveException;
import com.hazelcast.transaction.TransactionOptions;
import com.hazelcast.transaction.impl.SerializableXID;
import com.hazelcast.util.Clock;
import com.hazelcast.util.EmptyStatement;
import com.hazelcast.util.ExceptionUtil;
import com.hazelcast.util.ThreadUtil;

import javax.transaction.xa.Xid;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import static com.hazelcast.transaction.impl.Transaction.State;
import static com.hazelcast.transaction.impl.Transaction.State.ACTIVE;
import static com.hazelcast.transaction.impl.Transaction.State.COMMITTED;
import static com.hazelcast.transaction.impl.Transaction.State.NO_TXN;
import static com.hazelcast.transaction.impl.Transaction.State.PREPARED;
import static com.hazelcast.transaction.impl.Transaction.State.ROLLED_BACK;
import static com.hazelcast.transaction.impl.Transaction.State.ROLLING_BACK;

final class TransactionProxy {

    private static final ThreadLocal<Boolean> THREAD_FLAG = new ThreadLocal<Boolean>();

    private final TransactionOptions options;
    private final HazelcastClientInstanceImpl client;
    private final long threadId = ThreadUtil.getThreadId();
    private final ClientConnection connection;

    private Xid xid;
    private String txnId;
    private State state = NO_TXN;
    private long startTime;

    TransactionProxy(HazelcastClientInstanceImpl client, TransactionOptions options, ClientConnection connection) {
        this.options = options;
        this.client = client;
        this.connection = connection;
    }

    public String getTxnId() {
        return txnId;
    }

    public State getState() {
        return state;
    }

    public long getTimeoutMillis() {
        return options.getTimeoutMillis();
    }

    public boolean setTimeoutMillis(long timeoutMillis) {
        if (state == NO_TXN && options.getTimeoutMillis() != timeoutMillis) {
            options.setTimeout(timeoutMillis, TimeUnit.MILLISECONDS);
            return true;
        }
        return false;
    }

    void begin() {
        try {
            if (state == ACTIVE) {
                throw new IllegalStateException("Transaction is already active");
            }
            checkThread();
            if (THREAD_FLAG.get() != null) {
                throw new IllegalStateException("Nested transactions are not allowed!");
            }
            THREAD_FLAG.set(Boolean.TRUE);
            startTime = Clock.currentTimeMillis();
            ClientMessage request = TransactionCreateParameters.encode(xid, options.getTimeoutMillis(),
                    options.getDurability(), options.getTransactionType().id(), threadId);
            ClientMessage response = invoke(request);
            TransactionCreateResultParameters result = TransactionCreateResultParameters.decode(response);
            txnId = result.transactionId;
            state = ACTIVE;
        } catch (Exception e) {
            closeConnection();
            throw ExceptionUtil.rethrow(e);
        }
    }

    public void prepare() {
        try {
            if (state != ACTIVE) {
                throw new TransactionNotActiveException("Transaction is not active");
            }
            checkThread();
            checkTimeout();
            ClientMessage request = TransactionPrepareParameters.encode(txnId, threadId);
            invoke(request);
            state = PREPARED;
        } catch (Exception e) {
            state = ROLLING_BACK;
            closeConnection();
            throw ExceptionUtil.rethrow(e);
        }
    }

    void commit(boolean prepareAndCommit) {
        try {
            if (prepareAndCommit && state != ACTIVE) {
                throw new TransactionNotActiveException("Transaction is not active");
            }
            if (!prepareAndCommit && state != PREPARED) {
                throw new TransactionNotActiveException("Transaction is not prepared");
            }
            checkThread();
            checkTimeout();
            ClientMessage request = TransactionCommitParameters.encode(txnId, threadId, prepareAndCommit);
            invoke(request);
            state = COMMITTED;
        } catch (Exception e) {
            state = ROLLING_BACK;
            throw ExceptionUtil.rethrow(e);
        } finally {
            closeConnection();
        }
    }

    void rollback() {
        try {
            if (state == NO_TXN || state == ROLLED_BACK) {
                throw new IllegalStateException("Transaction is not active");
            }
            if (state == ROLLING_BACK) {
                state = ROLLED_BACK;
                return;
            }
            checkThread();
            try {
                ClientMessage request = TransactionRollbackParameters.encode(txnId, threadId);
                invoke(request);
            } catch (Exception ignored) {
                EmptyStatement.ignore(ignored);
            }
            state = ROLLED_BACK;
        } finally {
            closeConnection();
        }
    }

    SerializableXID getXid() {
        return (SerializableXID) xid;
    }

    void setXid(SerializableXID xid) {
        this.xid = xid;
    }

    private void closeConnection() {
        THREAD_FLAG.set(null);
//        try {
//            connection.release();
//        } catch (IOException e) {
//            IOUtil.closeResource(connection);
//        }
    }

    private void checkThread() {
        if (threadId != Thread.currentThread().getId()) {
            throw new IllegalStateException("Transaction cannot span multiple threads!");
        }
    }

    private void checkTimeout() {
        if (startTime + options.getTimeoutMillis() < Clock.currentTimeMillis()) {
            throw new TransactionException("Transaction is timed-out!");
        }
    }

    private ClientMessage invoke(ClientMessage request) {
        try {
            final ClientInvocation clientInvocation = new ClientInvocation(client, request, connection);
            final Future<ClientMessage> future = clientInvocation.invoke();
            return future.get();
        } catch (Exception e) {
            throw ExceptionUtil.rethrow(e);
        }
    }

}
