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

package com.hazelcast.client.impl.protocol.task.transaction;

import com.hazelcast.client.impl.ClientEngineImpl;
import com.hazelcast.client.impl.protocol.ClientMessage;
import com.hazelcast.client.impl.protocol.parameters.TransactionCreateParameters;
import com.hazelcast.client.impl.protocol.parameters.TransactionCreateResultParameters;
import com.hazelcast.client.impl.protocol.task.AbstractTransactionalMessageTask;
import com.hazelcast.instance.Node;
import com.hazelcast.nio.Connection;
import com.hazelcast.security.permission.TransactionPermission;
import com.hazelcast.transaction.TransactionContext;
import com.hazelcast.transaction.TransactionOptions;
import com.hazelcast.transaction.impl.Transaction;
import com.hazelcast.transaction.impl.TransactionAccessor;
import com.hazelcast.transaction.impl.TransactionManagerServiceImpl;

import java.security.Permission;
import java.util.concurrent.TimeUnit;

public class TransactionCreateMessageTask
        extends AbstractTransactionalMessageTask<TransactionCreateParameters> {


    public TransactionCreateMessageTask(ClientMessage clientMessage, Node node, Connection connection) {
        super(clientMessage, node, connection);
    }

    @Override
    protected ClientMessage innerCall() throws Exception {
        TransactionOptions options = new TransactionOptions();
        options.setDurability(parameters.durability);
        options.setTimeout(parameters.timeout, TimeUnit.MILLISECONDS);
        options.setTransactionType(TransactionOptions.TransactionType.getByValue(parameters.transactionType));

        TransactionManagerServiceImpl transactionManager =
                (TransactionManagerServiceImpl) clientEngine.getTransactionManagerService();
        TransactionContext context = transactionManager.newClientTransactionContext(options, endpoint.getUuid());
        if (parameters.xid != null) {
            Transaction transaction = TransactionAccessor.getTransaction(context);
            transactionManager.addManagedTransaction(parameters.xid, transaction);
        }
        context.beginTransaction();
        endpoint.setTransactionContext(context);
        return TransactionCreateResultParameters.encode(context.getTxnId());
    }

    @Override
    protected long getClientThreadId() {
        return parameters.threadId;
    }

    @Override
    protected TransactionCreateParameters decodeClientMessage(ClientMessage clientMessage) {
        return TransactionCreateParameters.decode(clientMessage);
    }

    @Override
    public String getServiceName() {
        return ClientEngineImpl.SERVICE_NAME;
    }

    @Override
    public Permission getRequiredPermission() {
        return new TransactionPermission();
    }

    @Override
    public String getDistributedObjectName() {
        return null;
    }

    @Override
    public String getMethodName() {
        return null;
    }

    @Override
    public Object[] getParameters() {
        return null;
    }
}
