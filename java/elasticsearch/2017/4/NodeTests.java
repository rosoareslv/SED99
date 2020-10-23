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
package org.elasticsearch.node;

import org.apache.logging.log4j.Logger;
import org.apache.lucene.util.LuceneTestCase;
import org.elasticsearch.Version;
import org.elasticsearch.bootstrap.BootstrapCheck;
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.common.UUIDs;
import org.elasticsearch.common.network.NetworkModule;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.transport.BoundTransportAddress;
import org.elasticsearch.env.Environment;
import org.elasticsearch.env.NodeEnvironment;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.InternalTestCluster;
import org.elasticsearch.transport.MockTcpTransportPlugin;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasToString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;

@LuceneTestCase.SuppressFileSystems(value = "ExtrasFS")
public class NodeTests extends ESTestCase {

    public void testNodeName() throws IOException {
        final Path tempDir = createTempDir();
        final String name = randomBoolean() ? randomAlphaOfLength(10) : null;
        Settings.Builder settings = Settings.builder()
            .put(ClusterName.CLUSTER_NAME_SETTING.getKey(), InternalTestCluster.clusterName("single-node-cluster", randomLong()))
            .put(Environment.PATH_HOME_SETTING.getKey(), tempDir)
            .put(NetworkModule.HTTP_ENABLED.getKey(), false)
            .put("transport.type", "mock-socket-network")
            .put(Node.NODE_DATA_SETTING.getKey(), true);
        if (name != null) {
            settings.put(Node.NODE_NAME_SETTING.getKey(), name);
        }
        try (Node node = new MockNode(settings.build(), Collections.singleton(MockTcpTransportPlugin.class))) {
            final Settings nodeSettings = randomBoolean() ? node.settings() : node.getEnvironment().settings();
            if (name == null) {
                assertThat(Node.NODE_NAME_SETTING.get(nodeSettings), equalTo(node.getNodeEnvironment().nodeId().substring(0, 7)));
            } else {
                assertThat(Node.NODE_NAME_SETTING.get(nodeSettings), equalTo(name));
            }
        }
    }

    public static class CheckPlugin extends Plugin {
        public static final BootstrapCheck CHECK = new BootstrapCheck() {
            @Override
            public boolean check() {
                return false;
            }

            @Override
            public String errorMessage() {
                return "boom";
            }
        };
        @Override
        public List<BootstrapCheck> getBootstrapChecks() {
            return Collections.singletonList(CHECK);
        }
    }

    public void testLoadPluginBootstrapChecks() throws IOException {
        final Path tempDir = createTempDir();
        final String name = randomBoolean() ? randomAlphaOfLength(10) : null;
        Settings.Builder settings = Settings.builder()
            .put(ClusterName.CLUSTER_NAME_SETTING.getKey(), InternalTestCluster.clusterName("single-node-cluster", randomLong()))
            .put(Environment.PATH_HOME_SETTING.getKey(), tempDir)
            .put(NetworkModule.HTTP_ENABLED.getKey(), false)
            .put("transport.type", "mock-socket-network")
            .put(Node.NODE_DATA_SETTING.getKey(), true);
        if (name != null) {
            settings.put(Node.NODE_NAME_SETTING.getKey(), name);
        }
        AtomicBoolean executed = new AtomicBoolean(false);
        try (Node node = new MockNode(settings.build(), Arrays.asList(MockTcpTransportPlugin.class, CheckPlugin.class)) {
            @Override
            protected void validateNodeBeforeAcceptingRequests(Settings settings, BoundTransportAddress boundTransportAddress,
                                                               List<BootstrapCheck> bootstrapChecks) throws NodeValidationException {
                assertEquals(1, bootstrapChecks.size());
                assertSame(CheckPlugin.CHECK, bootstrapChecks.get(0));
                executed.set(true);
                throw new NodeValidationException("boom");
            }
        }) {
            expectThrows(NodeValidationException.class, () -> node.start());
            assertTrue(executed.get());
        }
    }

    public void testWarnIfPreRelease() {
        final Logger logger = mock(Logger.class);

        final int id = randomIntBetween(1, 9) * 1000000;
        final Version releaseVersion = Version.fromId(id + 99);
        final Version preReleaseVersion = Version.fromId(id + randomIntBetween(0, 98));

        Node.warnIfPreRelease(releaseVersion, false, logger);
        verifyNoMoreInteractions(logger);

        reset(logger);
        Node.warnIfPreRelease(releaseVersion, true, logger);
        verify(logger).warn(
            "version [{}] is a pre-release version of Elasticsearch and is not suitable for production", releaseVersion + "-SNAPSHOT");

        reset(logger);
        final boolean isSnapshot = randomBoolean();
        Node.warnIfPreRelease(preReleaseVersion, isSnapshot, logger);
        verify(logger).warn(
            "version [{}] is a pre-release version of Elasticsearch and is not suitable for production",
            preReleaseVersion + (isSnapshot ? "-SNAPSHOT" : ""));

    }

    public void testNodeAttributes() throws IOException {
        String attr = randomAlphaOfLength(5);
        Settings.Builder settings = baseSettings().put(Node.NODE_ATTRIBUTES.getKey() + "test_attr", attr);
        try (Node node = new MockNode(settings.build(), Collections.singleton(MockTcpTransportPlugin.class))) {
            final Settings nodeSettings = randomBoolean() ? node.settings() : node.getEnvironment().settings();
            assertEquals(attr, Node.NODE_ATTRIBUTES.get(nodeSettings).getAsMap().get("test_attr"));
        }

        // leading whitespace not allowed
        attr = " leading";
        settings = baseSettings().put(Node.NODE_ATTRIBUTES.getKey() + "test_attr", attr);
        try (Node node = new MockNode(settings.build(), Collections.singleton(MockTcpTransportPlugin.class))) {
            fail("should not allow a node attribute with leading whitespace");
        } catch (IllegalArgumentException e) {
            assertEquals("node.attr.test_attr cannot have leading or trailing whitespace [ leading]", e.getMessage());
        }

        // trailing whitespace not allowed
        attr = "trailing ";
        settings = baseSettings().put(Node.NODE_ATTRIBUTES.getKey() + "test_attr", attr);
        try (Node node = new MockNode(settings.build(), Collections.singleton(MockTcpTransportPlugin.class))) {
            fail("should not allow a node attribute with trailing whitespace");
        } catch (IllegalArgumentException e) {
            assertEquals("node.attr.test_attr cannot have leading or trailing whitespace [trailing ]", e.getMessage());
        }
    }

    public void testDefaultPathDataSet() throws IOException {
        final Path zero = createTempDir().toAbsolutePath();
        final Path one = createTempDir().toAbsolutePath();
        final Path defaultPathData = createTempDir().toAbsolutePath();
        final Settings settings = Settings.builder()
                .put("path.home", "/home")
                .put("path.data.0", zero)
                .put("path.data.1", one)
                .put("default.path.data", defaultPathData)
                .build();
        try (NodeEnvironment nodeEnv = new NodeEnvironment(settings, new Environment(settings))) {
            final Path defaultPathDataWithNodesAndId = defaultPathData.resolve("nodes/0");
            Files.createDirectories(defaultPathDataWithNodesAndId);
            final NodeEnvironment.NodePath defaultNodePath = new NodeEnvironment.NodePath(defaultPathDataWithNodesAndId);
            final boolean indexExists = randomBoolean();
            final List<String> indices;
            if (indexExists) {
                indices = IntStream.range(0, randomIntBetween(1, 3)).mapToObj(i -> UUIDs.randomBase64UUID()).collect(Collectors.toList());
                for (final String index : indices) {
                    Files.createDirectories(defaultNodePath.indicesPath.resolve(index));
                }
            } else {
                indices = Collections.emptyList();
            }
            final Logger mock = mock(Logger.class);
            if (indexExists) {
                final IllegalStateException e = expectThrows(
                        IllegalStateException.class,
                        () -> Node.checkForIndexDataInDefaultPathData(settings, nodeEnv, mock));
                final String message = String.format(
                        Locale.ROOT,
                        "detected index data in default.path.data [%s] where there should not be any; check the logs for details",
                        defaultPathData);
                assertThat(e, hasToString(containsString(message)));
                verify(mock)
                        .error("detected index data in default.path.data [{}] where there should not be any", defaultNodePath.indicesPath);
                for (final String index : indices) {
                    verify(mock).info(
                            "index folder [{}] in default.path.data [{}] must be moved to any of {}",
                            index,
                            defaultNodePath.indicesPath,
                            Arrays.stream(nodeEnv.nodePaths()).map(np -> np.indicesPath).collect(Collectors.toList()));
                }
                verifyNoMoreInteractions(mock);
            } else {
                Node.checkForIndexDataInDefaultPathData(settings, nodeEnv, mock);
                verifyNoMoreInteractions(mock);
            }
        }
    }

    public void testDefaultPathDataNotSet() throws IOException {
        final Path zero = createTempDir().toAbsolutePath();
        final Path one = createTempDir().toAbsolutePath();
        final Settings settings = Settings.builder()
                .put("path.home", "/home")
                .put("path.data.0", zero)
                .put("path.data.1", one)
                .build();
        try (NodeEnvironment nodeEnv = new NodeEnvironment(settings, new Environment(settings))) {
            final Logger mock = mock(Logger.class);
            Node.checkForIndexDataInDefaultPathData(settings, nodeEnv, mock);
            verifyNoMoreInteractions(mock);
        }
    }

    private static Settings.Builder baseSettings() {
        final Path tempDir = createTempDir();
        return Settings.builder()
                .put(ClusterName.CLUSTER_NAME_SETTING.getKey(), InternalTestCluster.clusterName("single-node-cluster", randomLong()))
                .put(Environment.PATH_HOME_SETTING.getKey(), tempDir)
                .put(NetworkModule.HTTP_ENABLED.getKey(), false)
                .put("transport.type", "mock-socket-network")
                .put(Node.NODE_DATA_SETTING.getKey(), true);
    }


}
