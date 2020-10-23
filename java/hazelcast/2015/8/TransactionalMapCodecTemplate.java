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

package com.hazelcast.client.impl.protocol.template;

import com.hazelcast.annotation.GenerateCodec;
import com.hazelcast.annotation.Request;
import com.hazelcast.client.impl.protocol.ResponseMessageConst;
import com.hazelcast.nio.serialization.Data;

@GenerateCodec(id = TemplateConstants.TX_MAP_TEMPLATE_ID,
        name = "TransactionalMap", ns = "Hazelcast.Client.Protocol.TransactionalMap")
public interface TransactionalMapCodecTemplate {
    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key.
     * @return True if this map contains an entry for the specified key.
     */
    @Request(id = 1, retryable = false, response = ResponseMessageConst.BOOLEAN)
    Object containsKey(String name, String txnId, long threadId, Data key);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @return The value for the specified key
     */
    @Request(id = 2, retryable = false, response = ResponseMessageConst.DATA)
    Object get(String name, String txnId, long threadId, Data key);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The value to which the specified key is mapped
     * @return The value for the specified key
     */
    @Request(id = 3, retryable = false, response = ResponseMessageConst.DATA)
    Object getForUpdate(String name, String txnId, long threadId, Data key);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @return The number of entries in this map.
     */
    @Request(id = 4, retryable = false, response = ResponseMessageConst.INTEGER)
    Object size(String name, String txnId, long threadId);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @return <tt>true</tt> if this map contains no entries.
     */
    @Request(id = 5, retryable = false, response = ResponseMessageConst.BOOLEAN)
    Object isEmpty(String name, String txnId, long threadId);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @param value The value to associate with the key.
     * @param ttl The duration in milliseconds after which this entry shall be deleted. O means infinite.
     * @return Previous value associated with key or  null if there was no mapping for key
     */
    @Request(id = 6, retryable = false, response = ResponseMessageConst.DATA)
    Object put(String name, String txnId, long threadId, Data key, Data value, long ttl);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @param value The value to associate with key
     */
    @Request(id = 7, retryable = false, response = ResponseMessageConst.VOID)
    void set(String name, String txnId, long threadId, Data key, Data value);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @param value The value to associate with the key when there is no previous value.
     * @return The previous value associated with key, or null if there was no mapping for key.
     */
    @Request(id = 8, retryable = false, response = ResponseMessageConst.DATA)
    Object putIfAbsent(String name, String txnId, long threadId, Data key, Data value);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @param value The value replaced the previous value
     * @return The previous value associated with key, or null if there was no mapping for key.
     */
    @Request(id = 9, retryable = false, response = ResponseMessageConst.DATA)
    Object replace(String name, String txnId, long threadId, Data key, Data value);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key.
     * @param oldValue Replace the key value if it is the old value.
     * @param newValue The new value to replace the old value.
     * @return true if the value was replaced.
     */
    @Request(id = 10, retryable = false, response = ResponseMessageConst.BOOLEAN)
    Object replaceIfSame(String name, String txnId, long threadId, Data key, Data oldValue, Data newValue);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key Remove the mapping for this key.
     * @return The previous value associated with key, or null if there was no mapping for key
     */
    @Request(id = 11, retryable = false, response = ResponseMessageConst.DATA)
    Object remove(String name, String txnId, long threadId, Data key);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction operation
     * @param threadId The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key  Remove the mapping for this key.
     */
    @Request(id = 12, retryable = false, response = ResponseMessageConst.VOID)
    void delete(String name, String txnId, long threadId, Data key);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction opearation
     * @param threadId  The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param key The specified key
     * @param value Remove the key if it has this value.
     * @return True if the value was removed
     */
    @Request(id = 13, retryable = false, response = ResponseMessageConst.BOOLEAN)
    Object removeIfSame(String name, String txnId, long threadId, Data key, Data value);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction opearation
     * @param threadId  The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @return A set clone of the keys contained in this map.
     */
    @Request(id = 14, retryable = false, response = ResponseMessageConst.SET_DATA)
    Object keySet(String name, String txnId, long threadId);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction opearation
     * @param threadId  The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param predicate Specified query criteria.
     * @return Result key set for the query.
     */
    @Request(id = 15, retryable = false, response = ResponseMessageConst.SET_DATA)
    Object keySetWithPredicate(String name, String txnId, long threadId, Data predicate);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction opearation
     * @param threadId  The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @return All values in the map
     */
    @Request(id = 16, retryable = false, response = ResponseMessageConst.LIST_DATA)
    Object values(String name, String txnId, long threadId);

    /**
     *
     * @param name Name of the Transactional Map
     * @param txnId ID of the this transaction opearation
     * @param threadId  The id of the user thread performing the operation. It is used to guarantee that only the lock holder thread (if a lock exists on the entry) can perform the requested operation.
     * @param predicate Specified query criteria.
     * @return Result value collection of the query.
     */
    @Request(id = 17, retryable = false, response = ResponseMessageConst.LIST_DATA)
    Object valuesWithPredicate(String name, String txnId, long threadId, Data predicate);

}
