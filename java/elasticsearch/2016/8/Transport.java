/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.transport;

import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.common.breaker.CircuitBreaker;
import org.elasticsearch.common.breaker.NoopCircuitBreaker;
import org.elasticsearch.common.component.LifecycleComponent;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Setting.Property;
import org.elasticsearch.common.transport.BoundTransportAddress;
import org.elasticsearch.common.transport.TransportAddress;

import java.io.IOException;
import java.util.List;
import java.util.Map;

/**
 *
 */
public interface Transport extends LifecycleComponent {


    Setting<Boolean> TRANSPORT_TCP_COMPRESS = Setting.boolSetting("transport.tcp.compress", false, Property.NodeScope);

    void transportServiceAdapter(TransportServiceAdapter service);

    /**
     * The address the transport is bound on.
     */
    BoundTransportAddress boundAddress();

    /**
     * Further profile bound addresses
     * @return <code>null</code> iff profiles are unsupported, otherwise a map with name of profile and its bound transport address
     */
    Map<String, BoundTransportAddress> profileBoundAddresses();

    /**
     * Returns an address from its string representation.
     */
    TransportAddress[] addressesFromString(String address, int perAddressLimit) throws Exception;

    /**
     * Is the address type supported.
     */
    boolean addressSupported(Class<? extends TransportAddress> address);

    /**
     * Returns <tt>true</tt> if the node is connected.
     */
    boolean nodeConnected(DiscoveryNode node);

    /**
     * Connects to the given node, if already connected, does nothing.
     */
    void connectToNode(DiscoveryNode node) throws ConnectTransportException;

    /**
     * Connects to a node in a light manner. Used when just connecting for ping and then
     * disconnecting.
     */
    void connectToNodeLight(DiscoveryNode node) throws ConnectTransportException;

    /**
     * Disconnected from the given node, if not connected, will do nothing.
     */
    void disconnectFromNode(DiscoveryNode node);

    /**
     * Sends the request to the node.
     * @throws NodeNotConnectedException if the given node is not connected
     */
    void sendRequest(DiscoveryNode node, long requestId, String action, TransportRequest request, TransportRequestOptions options) throws
        IOException, TransportException;

    /**
     * Returns count of currently open connections
     */
    long serverOpen();

    List<String> getLocalAddresses();

    default CircuitBreaker getInFlightRequestBreaker() {
        return new NoopCircuitBreaker("in-flight-noop");
    }

}
