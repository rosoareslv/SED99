/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.monitoring.exporter;

import org.elasticsearch.Version;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.common.Strings;
import org.elasticsearch.common.transport.TransportAddress;
import org.elasticsearch.test.ESTestCase;

import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import static org.elasticsearch.xpack.monitoring.exporter.MonitoringDoc.Node.fromDiscoveryNode;

public class MonitoringDocTests extends ESTestCase {

    public void testEmptyIdBecomesNull() {
        final String id = randomFrom("", null, randomAlphaOfLength(5));
        final MonitoringDoc doc =
                new MonitoringDoc("monitoringId", "monitoringVersion",
                                  "type", id, "clusterUUID",
                                  0, (DiscoveryNode)null);

        if (Strings.isNullOrEmpty(id)) {
            assertNull(doc.getId());
        } else {
            assertSame(id, doc.getId());
        }
    }

    public void testFromDiscoveryNode() {
        assertEquals(null, fromDiscoveryNode(null));

        String nodeId = randomAlphaOfLength(5);
        TransportAddress address = buildNewFakeTransportAddress();
        Version version = randomFrom(Version.V_5_0_1, Version.V_5_3_0, Version.CURRENT);

        String name = randomBoolean() ? randomAlphaOfLength(5) : "";
        Map<String, String> attributes = new HashMap<>();
        if (randomBoolean()) {
            int nbAttrs = randomIntBetween(1, 5);
            for (int i = 0; i < nbAttrs; i++) {
                attributes.put("attr_" + String.valueOf(i), String.valueOf(i));
            }
        }
        Set<DiscoveryNode.Role> roles = new HashSet<>();
        if (randomBoolean()) {
            roles.addAll(randomSubsetOf(Arrays.asList(DiscoveryNode.Role.values())));
        }
        final MonitoringDoc.Node expectedNode = new MonitoringDoc.Node(nodeId,
                address.address().getHostString(), address.toString(),
                address.getAddress(), name, attributes);

        DiscoveryNode discoveryNode =
                new DiscoveryNode(name, nodeId, address, attributes, roles, version);
        assertEquals(expectedNode, fromDiscoveryNode(discoveryNode));
    }
}
