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

package com.hazelcast.client.impl.protocol.task.map;

import com.hazelcast.client.impl.protocol.ClientMessage;
import com.hazelcast.client.impl.protocol.codec.MapKeySetWithPagingPredicateCodec;
import com.hazelcast.instance.Node;
import com.hazelcast.nio.Connection;
import com.hazelcast.nio.serialization.Data;
import com.hazelcast.query.Predicate;
import com.hazelcast.query.impl.QueryResultEntry;

import java.util.Collection;
import java.util.HashSet;
import java.util.Set;

public class MapKeySetWithPagingPredicateMessageTask
        extends AbstractMapQueryMessageTask<MapKeySetWithPagingPredicateCodec.RequestParameters> {

    public MapKeySetWithPagingPredicateMessageTask(ClientMessage clientMessage, Node node, Connection connection) {
        super(clientMessage, node, connection);
    }

    @Override
    protected Object reduce(Collection<QueryResultEntry> result) {
        Set<Data> set = new HashSet<Data>(result.size());
        for (QueryResultEntry resultEntry : result) {
            set.add(resultEntry.getKeyData());
        }
        return set;
    }

    @Override
    protected Predicate getPredicate() {
        return serializationService.toObject(parameters.predicate);
    }

    @Override
    protected MapKeySetWithPagingPredicateCodec.RequestParameters decodeClientMessage(ClientMessage clientMessage) {
        return MapKeySetWithPagingPredicateCodec.decodeRequest(clientMessage);
    }

    @Override
    protected ClientMessage encodeResponse(Object response) {
        return MapKeySetWithPagingPredicateCodec.encodeResponse((Set<Data>) response);
    }

    @Override
    public Object[] getParameters() {
        return new Object[]{parameters.predicate};
    }


    @Override
    public String getDistributedObjectName() {
        return parameters.name;
    }

    @Override
    public String getMethodName() {
        return "keySet";
    }

}
