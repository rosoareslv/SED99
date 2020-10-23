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
import com.hazelcast.nio.Address;

@edu.umd.cs.findbugs.annotations.SuppressWarnings({"URF_UNREAD_PUBLIC_OR_PROTECTED_FIELD"})
public class PartitionLostEventParameters {

    public static final ClientMessageType TYPE = ClientMessageType.PARTITION_LOST_EVENT;
    public int partitionId;
    public int lostBackupCount;
    public Address source;

    private PartitionLostEventParameters(ClientMessage flyweight) {
        partitionId = flyweight.getInt();
        lostBackupCount = flyweight.getInt();
        source = AddressCodec.decode(flyweight);

    }

    public static PartitionLostEventParameters decode(ClientMessage flyweight) {
        return new PartitionLostEventParameters(flyweight);
    }

    public static ClientMessage encode(int partitionId, int lostBackupCount, Address source) {
        final int requiredDataSize = calculateDataSize(partitionId, lostBackupCount, source);
        ClientMessage clientMessage = ClientMessage.createForEncode(requiredDataSize);
        clientMessage.setMessageType(TYPE.id());
        clientMessage.addFlag(ClientMessage.LISTENER_EVENT_FLAG);
        clientMessage.set(partitionId).set(lostBackupCount);
        AddressCodec.encode(source, clientMessage);
        clientMessage.ensureCapacity(requiredDataSize);
        clientMessage.updateFrameLength();
        return clientMessage;
    }

    /**
     * sample data size estimation
     *
     * @return size
     */
    public static int calculateDataSize(int partitionId, int lostBackupCount, Address source) {
        return ClientMessage.HEADER_SIZE + BitUtil.SIZE_OF_INT + BitUtil.SIZE_OF_INT
                + AddressCodec.calculateDataSize(source);
    }
}

