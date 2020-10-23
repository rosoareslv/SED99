/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.monitoring.action;

import org.elasticsearch.Version;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.io.stream.BytesStreamOutput;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.xpack.monitoring.MonitoredSystem;
import org.elasticsearch.xpack.monitoring.exporter.MonitoringDoc;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import static org.elasticsearch.test.VersionUtils.randomVersion;
import static org.hamcrest.Matchers.equalTo;

public class MonitoringBulkDocTests extends ESTestCase {

    public void testSerialization() throws IOException {
        int iterations = randomIntBetween(5, 50);
        for (int i = 0; i < iterations; i++) {
            MonitoringBulkDoc doc = newRandomMonitoringBulkDoc();

            BytesStreamOutput output = new BytesStreamOutput();
            Version outputVersion = randomVersion(random());
            output.setVersion(outputVersion);
            doc.writeTo(output);

            StreamInput streamInput = output.bytes().streamInput();
            streamInput.setVersion(outputVersion);
            MonitoringBulkDoc doc2 = MonitoringBulkDoc.readFrom(streamInput);

            assertThat(doc2.getMonitoringId(), equalTo(doc.getMonitoringId()));
            assertThat(doc2.getMonitoringVersion(), equalTo(doc.getMonitoringVersion()));
            assertThat(doc2.getClusterUUID(), equalTo(doc.getClusterUUID()));
            assertThat(doc2.getTimestamp(), equalTo(doc.getTimestamp()));
            assertThat(doc2.getSourceNode(), equalTo(doc.getSourceNode()));
            assertThat(doc2.getIndex(), equalTo(doc.getIndex()));
            assertThat(doc2.getType(), equalTo(doc.getType()));
            assertThat(doc2.getId(), equalTo(doc.getId()));
            assertThat(doc2.getXContentType(), equalTo(doc.getXContentType()));
            if (doc.getSource() == null) {
                assertThat(doc2.getSource(), equalTo(BytesArray.EMPTY));
            } else {
                assertThat(doc2.getSource(), equalTo(doc.getSource()));
            }
        }
    }

    public static MonitoringBulkDoc newRandomMonitoringBulkDoc() {
        String monitoringId = randomFrom(MonitoredSystem.values()).getSystem();
        String monitoringVersion = randomVersion(random()).toString();
        MonitoringIndex index = randomBoolean() ? randomFrom(MonitoringIndex.values()) : null;
        String type = randomFrom("type1", "type2", "type3");
        String id = randomBoolean() ? randomAlphaOfLength(3) : null;
        String clusterUUID = randomBoolean() ? randomAlphaOfLength(5) : null;
        long timestamp = randomBoolean() ? randomNonNegativeLong() : 0L;
        MonitoringDoc.Node sourceNode = randomBoolean() ? newRandomSourceNode() : null;
        BytesReference source =  new BytesArray("{\"key\" : \"value\"}");
        XContentType xContentType = XContentType.JSON;

        return new MonitoringBulkDoc(monitoringId, monitoringVersion, index, type, id,
                clusterUUID, timestamp, sourceNode, source, xContentType);
    }

    public static MonitoringDoc.Node newRandomSourceNode() {
        String uuid = null;
        String name = null;
        String ip = null;
        String transportAddress = null;
        String host = null;
        Map<String, String> attributes = null;

        if (frequently()) {
            uuid = randomAlphaOfLength(5);
            name = randomAlphaOfLength(5);
        }
        if (randomBoolean()) {
            ip = randomAlphaOfLength(5);
            transportAddress = randomAlphaOfLength(5);
            host = randomAlphaOfLength(3);
        }
        if (rarely()) {
            int nbAttributes = randomIntBetween(0, 5);
            attributes = new HashMap<>();
            for (int i = 0; i < nbAttributes; i++) {
                attributes.put("key#" + i, String.valueOf(i));
            }
        }
        return new MonitoringDoc.Node(uuid, host, transportAddress, ip, name, attributes);
    }
}
