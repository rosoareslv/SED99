/*
 * SonarQube
 * Copyright (C) 2009-2017 SonarSource SA
 * mailto:info AT sonarsource DOT com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.application.config;

import java.net.InetAddress;
import java.net.SocketException;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.process.MessageException;
import org.sonar.process.NetworkUtils;
import org.sonar.process.NetworkUtilsImpl;

import static java.lang.String.format;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static org.sonar.process.ProcessId.COMPUTE_ENGINE;
import static org.sonar.process.ProcessId.ELASTICSEARCH;
import static org.sonar.process.ProcessId.WEB_SERVER;
import static org.sonar.process.ProcessProperties.CLUSTER_ENABLED;
import static org.sonar.process.ProcessProperties.CLUSTER_HOSTS;
import static org.sonar.process.ProcessProperties.CLUSTER_NODE_HOST;
import static org.sonar.process.ProcessProperties.CLUSTER_NODE_TYPE;
import static org.sonar.process.ProcessProperties.CLUSTER_SEARCH_HOSTS;
import static org.sonar.process.ProcessProperties.JDBC_URL;
import static org.sonar.process.ProcessProperties.SEARCH_HOST;

public class ClusterSettingsTest {

  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  private InetAddress nonLoopbackLocal = InetAddress.getLoopbackAddress();
  private NetworkUtils network = spy(NetworkUtilsImpl.INSTANCE);

  @Before
  public void setUp() throws Exception {
    when(network.isLocalInetAddress(nonLoopbackLocal)).thenReturn(true);
    when(network.isLoopbackInetAddress(nonLoopbackLocal)).thenReturn(false);
  }

  @Test
  public void test_isClusterEnabled() throws Exception {
    TestAppSettings settings = newSettingsForAppNode().set(CLUSTER_ENABLED, "true");
    assertThat(ClusterSettings.isClusterEnabled(settings)).isTrue();

    settings = new TestAppSettings().set(CLUSTER_ENABLED, "false");
    assertThat(ClusterSettings.isClusterEnabled(settings)).isFalse();
  }

  @Test
  public void isClusterEnabled_returns_false_by_default() {
    assertThat(ClusterSettings.isClusterEnabled(new TestAppSettings())).isFalse();
  }

  @Test
  public void getEnabledProcesses_returns_all_processes_in_standalone_mode() {
    TestAppSettings settings = new TestAppSettings().set(CLUSTER_ENABLED, "false");
    assertThat(ClusterSettings.getEnabledProcesses(settings)).containsOnly(COMPUTE_ENGINE, ELASTICSEARCH, WEB_SERVER);
  }

  @Test
  public void getEnabledProcesses_returns_configured_processes_in_cluster_mode() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();
    assertThat(ClusterSettings.getEnabledProcesses(settings)).containsOnly(COMPUTE_ENGINE, WEB_SERVER);

    settings = newSettingsForSearchNode();
    assertThat(ClusterSettings.getEnabledProcesses(settings)).containsOnly(ELASTICSEARCH);
  }

  @Test
  public void accept_throws_MessageException_if_no_node_type_is_configured() {
    TestAppSettings settings = new TestAppSettings();
    settings.set(CLUSTER_ENABLED, "true");

    expectedException.expect(MessageException.class);
    expectedException.expectMessage("Property sonar.cluster.node.type is mandatory");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_throws_MessageException_if_node_type_is_not_correct() {
    TestAppSettings settings = new TestAppSettings();
    settings.set(CLUSTER_ENABLED, "true");
    settings.set(CLUSTER_NODE_TYPE, "bla");

    expectedException.expect(MessageException.class);
    expectedException.expectMessage("Invalid value for property sonar.cluster.node.type: [bla], only [application, search] are allowed");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_throws_MessageException_if_internal_property_for_startup_leader_is_configured() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();
    settings.set("sonar.cluster.web.startupLeader", "true");

    expectedException.expect(MessageException.class);
    expectedException.expectMessage("Property [sonar.cluster.web.startupLeader] is forbidden");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_does_nothing_if_cluster_is_disabled() {
    TestAppSettings settings = new TestAppSettings();
    settings.set(CLUSTER_ENABLED, "false");
    // this property is supposed to fail if cluster is enabled
    settings.set("sonar.cluster.web.startupLeader", "true");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_throws_MessageException_if_h2_on_application_node() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();
    settings.set("sonar.jdbc.url", "jdbc:h2:mem");

    expectedException.expect(MessageException.class);
    expectedException.expectMessage("Embedded database is not supported in cluster mode");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_does_not_verify_h2_on_search_node() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.set("sonar.jdbc.url", "jdbc:h2:mem");

    // do not fail
    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void accept_throws_MessageException_on_application_node_if_default_jdbc_url() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();
    settings.clearProperty(JDBC_URL);

    expectedException.expect(MessageException.class);
    expectedException.expectMessage("Embedded database is not supported in cluster mode");

    new ClusterSettings(network).accept(settings.getProps());
  }

  @Test
  public void isLocalElasticsearchEnabled_returns_true_in_standalone_mode() {
    TestAppSettings settings = new TestAppSettings();
    assertThat(ClusterSettings.isLocalElasticsearchEnabled(settings)).isTrue();
  }

  @Test
  public void isLocalElasticsearchEnabled_returns_true_on_search_node() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();

    assertThat(ClusterSettings.isLocalElasticsearchEnabled(settings)).isTrue();
  }

  @Test
  public void isLocalElasticsearchEnabled_returns_true_for_a_application_node() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();

    assertThat(ClusterSettings.isLocalElasticsearchEnabled(settings)).isFalse();
  }

  @Test
  public void accept_throws_MessageException_if_searchHost_is_missing() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.clearProperty(SEARCH_HOST);
    assertThatPropertyIsMandatory(settings, SEARCH_HOST);
  }

  @Test
  public void accept_throws_MessageException_if_searchHost_is_empty() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.set(SEARCH_HOST, "");
    assertThatPropertyIsMandatory(settings, SEARCH_HOST);
  }

  @Test
  public void accept_throws_MessageException_if_clusterHosts_is_missing() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.clearProperty(CLUSTER_HOSTS);
    assertThatPropertyIsMandatory(settings, CLUSTER_HOSTS);
  }

  @Test
  public void accept_throws_MessageException_if_clusterSearchHosts_is_missing() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.clearProperty(CLUSTER_SEARCH_HOSTS);
    assertThatPropertyIsMandatory(settings, CLUSTER_SEARCH_HOSTS);
  }

  @Test
  public void accept_throws_MessageException_if_clusterSearchHosts_is_empty() throws Exception {
    TestAppSettings settings = newSettingsForSearchNode();
    settings.set(CLUSTER_SEARCH_HOSTS, "");
    assertThatPropertyIsMandatory(settings, CLUSTER_SEARCH_HOSTS);
  }

  @Test
  public void accept_throws_MessageException_if_jwt_token_is_not_set_on_application_nodes() throws Exception {
    TestAppSettings settings = newSettingsForAppNode();
    settings.clearProperty("sonar.auth.jwtBase64Hs256Secret");
    assertThatPropertyIsMandatory(settings, "sonar.auth.jwtBase64Hs256Secret");
  }

  private void assertThatPropertyIsMandatory(TestAppSettings settings, String key) {
    expectedException.expect(MessageException.class);
    expectedException.expectMessage(format("Property %s is mandatory", key));

    new ClusterSettings(network).accept(settings.getProps());
  }

  private TestAppSettings newSettingsForAppNode() throws SocketException {
    return new TestAppSettings()
      .set(CLUSTER_ENABLED, "true")
      .set(CLUSTER_NODE_TYPE, "application")
      .set(CLUSTER_NODE_HOST, nonLoopbackLocal.getHostAddress())
      .set(CLUSTER_HOSTS, nonLoopbackLocal.getHostAddress())
      .set(CLUSTER_SEARCH_HOSTS, nonLoopbackLocal.getHostAddress())
      .set("sonar.auth.jwtBase64Hs256Secret", "abcde")
      .set(JDBC_URL, "jdbc:mysql://localhost:3306/sonar");
  }

  private TestAppSettings newSettingsForSearchNode() throws SocketException {
    return new TestAppSettings()
      .set(CLUSTER_ENABLED, "true")
      .set(CLUSTER_NODE_TYPE, "search")
      .set(CLUSTER_NODE_HOST, nonLoopbackLocal.getHostAddress())
      .set(CLUSTER_HOSTS, nonLoopbackLocal.getHostAddress())
      .set(CLUSTER_SEARCH_HOSTS, nonLoopbackLocal.getHostAddress())
      .set(SEARCH_HOST, nonLoopbackLocal.getHostAddress());
  }
}
