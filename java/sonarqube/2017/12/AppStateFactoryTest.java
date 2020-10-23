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
package org.sonar.application;

import java.net.InetAddress;
import java.util.Optional;
import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.sonar.application.cluster.ClusterAppStateImpl;
import org.sonar.application.config.TestAppSettings;
import org.sonar.process.NetworkUtilsImpl;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.Assume.assumeThat;
import static org.sonar.process.ProcessProperties.CLUSTER_ENABLED;
import static org.sonar.process.ProcessProperties.CLUSTER_HOSTS;
import static org.sonar.process.ProcessProperties.CLUSTER_NAME;
import static org.sonar.process.ProcessProperties.CLUSTER_NODE_HOST;
import static org.sonar.process.ProcessProperties.CLUSTER_NODE_TYPE;

public class AppStateFactoryTest {

  private TestAppSettings settings = new TestAppSettings();
  private AppStateFactory underTest = new AppStateFactory(settings);

  @Test
  public void create_cluster_implementation_if_cluster_is_enabled() {
    Optional<InetAddress> ip = NetworkUtilsImpl.INSTANCE.getLocalNonLoopbackIpv4Address();
    assumeThat(ip.isPresent(), CoreMatchers.is(true));

    settings.set(CLUSTER_ENABLED, "true");
    settings.set(CLUSTER_NODE_TYPE, "application");
    settings.set(CLUSTER_NODE_HOST, ip.get().getHostAddress());
    settings.set(CLUSTER_HOSTS, ip.get().getHostAddress());
    settings.set(CLUSTER_NAME, "foo");

    AppState appState = underTest.create();
    assertThat(appState).isInstanceOf(ClusterAppStateImpl.class);
    appState.close();
  }

  @Test
  public void cluster_implementation_is_disabled_by_default() {
    assertThat(underTest.create()).isInstanceOf(AppStateImpl.class);
  }
}
