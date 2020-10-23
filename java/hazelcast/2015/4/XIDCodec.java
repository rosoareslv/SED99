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
import com.hazelcast.client.impl.protocol.util.BitUtil;
import com.hazelcast.client.impl.protocol.util.ParameterUtil;
import com.hazelcast.transaction.impl.SerializableXID;

import javax.transaction.xa.Xid;

public final class XIDCodec {

    private XIDCodec() {
    }

    public static Xid decode(ClientMessage clientMessage) {
        boolean isNull = clientMessage.getBoolean();
        if (isNull) {
            return null;
        }
        int formatId = clientMessage.getInt();
        byte[] globalTransactionId = clientMessage.getByteArray();
        byte[] branchQualifier = clientMessage.getByteArray();
        return new SerializableXID(formatId, globalTransactionId, branchQualifier);

    }

    public static void encode(Xid xid, ClientMessage clientMessage) {
        boolean isNull;
        if (xid == null) {
            isNull = true;
            clientMessage.set(isNull);
            return;
        }
        isNull = false;

        clientMessage.set(isNull);
        clientMessage.set(xid.getFormatId());
        clientMessage.set(xid.getGlobalTransactionId());
        clientMessage.set(xid.getBranchQualifier());
    }

    public static int calculateDataSize(Xid xid) {
        int dataSize = 0;
        dataSize += BitUtil.SIZE_OF_BOOLEAN;
        if (xid == null) {
            return dataSize;
        }
        dataSize += BitUtil.SIZE_OF_INT;
        dataSize += ParameterUtil.calculateByteArrayDataSize(xid.getGlobalTransactionId());
        dataSize += ParameterUtil.calculateByteArrayDataSize(xid.getBranchQualifier());
        return dataSize;
    }
}

