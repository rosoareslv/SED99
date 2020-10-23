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
import com.hazelcast.transaction.impl.SerializableXID;

import javax.transaction.xa.Xid;

@edu.umd.cs.findbugs.annotations.SuppressWarnings({"URF_UNREAD_PUBLIC_OR_PROTECTED_FIELD"})
public final class TransactionCreateParameters {


    public static final ClientMessageType TYPE = ClientMessageType.TRANSACTION_CREATE;
    public Xid xid;
    public long timeout;
    public int durability;
    public int transactionType;
    public long threadId;


    private TransactionCreateParameters(ClientMessage clientMessage) {
        xid = XIDCodec.decode(clientMessage);
        timeout = clientMessage.getLong();
        durability = clientMessage.getInt();
        transactionType = clientMessage.getInt();
    }

    public static TransactionCreateParameters decode(ClientMessage clientMessage) {
        return new TransactionCreateParameters(clientMessage);
    }

    public static ClientMessage encode(Xid xid, long timeout, int durability,
                                       int transactionType, long threadId) {
        final int requiredDataSize = calculateDataSize(xid, timeout, durability, transactionType, threadId);
        ClientMessage clientMessage = ClientMessage.createForEncode(requiredDataSize);
        clientMessage.ensureCapacity(requiredDataSize);
        XIDCodec.encode(xid, clientMessage);
        clientMessage.set(timeout).set(durability).set(transactionType).set(threadId);
        clientMessage.setMessageType(TYPE.id());
        clientMessage.updateFrameLength();
        return clientMessage;
    }

    public static int calculateDataSize(Xid xid, long timeout, int durability,
                                        int transactionType, long threadId) {
        return ClientMessage.HEADER_SIZE
                + XIDCodec.calculateDataSize(xid)
                + BitUtil.SIZE_OF_LONG
                + BitUtil.SIZE_OF_INT
                + BitUtil.SIZE_OF_INT
                + BitUtil.SIZE_OF_LONG;
    }


}

