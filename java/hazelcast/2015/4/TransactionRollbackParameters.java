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

package com.hazelcast.client.impl.protocol.parameters;

import com.hazelcast.client.impl.protocol.ClientMessage;
import com.hazelcast.client.impl.protocol.ClientMessageType;
import com.hazelcast.client.impl.protocol.util.BitUtil;
import com.hazelcast.client.impl.protocol.util.ParameterUtil;

@edu.umd.cs.findbugs.annotations.SuppressWarnings({"URF_UNREAD_PUBLIC_OR_PROTECTED_FIELD"})
public final class TransactionRollbackParameters {


    public static final ClientMessageType TYPE = ClientMessageType.TRANSACTION_ROLLBACK;
    public String transactionId;
    public long threadId;

    private TransactionRollbackParameters(ClientMessage clientMessage) {
        transactionId = clientMessage.getStringUtf8();
        threadId = clientMessage.getLong();
    }

    public static TransactionRollbackParameters decode(ClientMessage clientMessage) {
        return new TransactionRollbackParameters(clientMessage);
    }

    public static ClientMessage encode(String transactionId, long threadId) {
        final int requiredDataSize = calculateDataSize(transactionId, threadId);
        ClientMessage clientMessage = ClientMessage.createForEncode(requiredDataSize);
        clientMessage.ensureCapacity(requiredDataSize);
        clientMessage.set(transactionId).set(threadId);
        clientMessage.setMessageType(TYPE.id());
        clientMessage.updateFrameLength();
        return clientMessage;
    }

    public static int calculateDataSize(String transactionId, long threadId) {
        return ClientMessage.HEADER_SIZE
                + BitUtil.SIZE_OF_LONG
                + ParameterUtil.calculateStringDataSize(transactionId);
    }


}

