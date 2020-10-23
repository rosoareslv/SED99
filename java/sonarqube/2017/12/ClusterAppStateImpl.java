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

package org.sonar.application.cluster;

import com.hazelcast.core.EntryEvent;
import com.hazelcast.core.EntryListener;
import com.hazelcast.core.HazelcastInstanceNotActiveException;
import com.hazelcast.core.IAtomicReference;
import com.hazelcast.core.MapEvent;
import com.hazelcast.core.Member;
import com.hazelcast.core.MemberAttributeEvent;
import com.hazelcast.core.MembershipEvent;
import com.hazelcast.core.MembershipListener;
import com.hazelcast.core.ReplicatedMap;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.locks.Lock;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.sonar.application.AppStateListener;
import org.sonar.application.cluster.health.HealthStateSharing;
import org.sonar.application.cluster.health.HealthStateSharingImpl;
import org.sonar.application.cluster.health.SearchNodeHealthProvider;
import org.sonar.application.config.AppSettings;
import org.sonar.application.config.ClusterSettings;
import org.sonar.process.MessageException;
import org.sonar.process.NetworkUtilsImpl;
import org.sonar.process.ProcessId;
import org.sonar.process.cluster.NodeType;
import org.sonar.process.cluster.hz.HazelcastMember;

import static java.lang.String.format;
import static org.sonar.process.cluster.hz.HazelcastMember.Attribute.NODE_TYPE;
import static org.sonar.process.cluster.hz.HazelcastObjects.CLUSTER_NAME;
import static org.sonar.process.cluster.hz.HazelcastObjects.LEADER;
import static org.sonar.process.cluster.hz.HazelcastObjects.OPERATIONAL_PROCESSES;
import static org.sonar.process.cluster.hz.HazelcastObjects.SONARQUBE_VERSION;

public class ClusterAppStateImpl implements ClusterAppState {

  private static final Logger LOGGER = LoggerFactory.getLogger(ClusterAppStateImpl.class);

  private final HazelcastMember hzMember;
  private final List<AppStateListener> listeners = new ArrayList<>();
  private final Map<ProcessId, Boolean> operationalLocalProcesses = new EnumMap<>(ProcessId.class);
  private final ReplicatedMap<ClusterProcess, Boolean> operationalProcesses;
  private final String operationalProcessListenerUUID;
  private final String nodeDisconnectedListenerUUID;
  private HealthStateSharing healthStateSharing = null;

  public ClusterAppStateImpl(AppSettings settings, HazelcastMember hzMember) {
    this.hzMember = hzMember;

    // Get or create the replicated map
    operationalProcesses = (ReplicatedMap) hzMember.getReplicatedMap(OPERATIONAL_PROCESSES);
    operationalProcessListenerUUID = operationalProcesses.addEntryListener(new OperationalProcessListener());
    nodeDisconnectedListenerUUID = hzMember.getCluster().addMembershipListener(new NodeDisconnectedListener());

    if (ClusterSettings.isLocalElasticsearchEnabled(settings)) {
      this.healthStateSharing = new HealthStateSharingImpl(hzMember, new SearchNodeHealthProvider(settings.getProps(), this, NetworkUtilsImpl.INSTANCE));
      this.healthStateSharing.start();
    }
  }

  @Override
  public HazelcastMember getHazelcastMember() {
    return hzMember;
  }

  @Override
  public void addListener(AppStateListener listener) {
    listeners.add(listener);
  }

  @Override
  public boolean isOperational(ProcessId processId, boolean local) {
    if (local) {
      return operationalLocalProcesses.computeIfAbsent(processId, p -> false);
    }
    for (Map.Entry<ClusterProcess, Boolean> entry : operationalProcesses.entrySet()) {
      if (entry.getKey().getProcessId().equals(processId) && entry.getValue()) {
        return true;
      }
    }
    return false;
  }

  @Override
  public void setOperational(ProcessId processId) {
    operationalLocalProcesses.put(processId, true);
    operationalProcesses.put(new ClusterProcess(hzMember.getUuid(), processId), Boolean.TRUE);
  }

  @Override
  public boolean tryToLockWebLeader() {
    IAtomicReference<String> leader = hzMember.getAtomicReference(LEADER);
    if (leader.get() == null) {
      Lock lock = hzMember.getLock(LEADER);
      lock.lock();
      try {
        if (leader.get() == null) {
          leader.set(hzMember.getUuid());
          return true;
        }
        return false;
      } finally {
        lock.unlock();
      }
    } else {
      return false;
    }
  }

  @Override
  public void reset() {
    throw new IllegalStateException("state reset is not supported in cluster mode");
  }

  @Override
  public void registerSonarQubeVersion(String sonarqubeVersion) {
    IAtomicReference<String> sqVersion = hzMember.getAtomicReference(SONARQUBE_VERSION);
    if (sqVersion.get() == null) {
      Lock lock = hzMember.getLock(SONARQUBE_VERSION);
      lock.lock();
      try {
        if (sqVersion.get() == null) {
          sqVersion.set(sonarqubeVersion);
        }
      } finally {
        lock.unlock();
      }
    }

    String clusterVersion = sqVersion.get();
    if (!sqVersion.get().equals(sonarqubeVersion)) {
      throw new IllegalStateException(
        format("The local version %s is not the same as the cluster %s", sonarqubeVersion, clusterVersion));
    }
  }

  @Override
  public void registerClusterName(String clusterName) {
    IAtomicReference<String> property = hzMember.getAtomicReference(CLUSTER_NAME);
    if (property.get() == null) {
      Lock lock = hzMember.getLock(CLUSTER_NAME);
      lock.lock();
      try {
        if (property.get() == null) {
          property.set(clusterName);
        }
      } finally {
        lock.unlock();
      }
    }

    String clusterValue = property.get();
    if (!property.get().equals(clusterName)) {
      throw new MessageException(
        format("This node has a cluster name [%s], which does not match [%s] from the cluster", clusterName, clusterValue));
    }
  }

  @Override
  public Optional<String> getLeaderHostName() {
    String leaderId = (String) hzMember.getAtomicReference(LEADER).get();
    if (leaderId != null) {
      Optional<Member> leader = hzMember.getCluster().getMembers().stream().filter(m -> m.getUuid().equals(leaderId)).findFirst();
      if (leader.isPresent()) {
        return Optional.of(leader.get().getAddress().getHost());
      }
    }
    return Optional.empty();
  }

  @Override
  public void close() {
    if (hzMember != null) {
      if (healthStateSharing != null) {
        healthStateSharing.stop();
      }
      try {
        // Removing listeners
        operationalProcesses.removeEntryListener(operationalProcessListenerUUID);
        hzMember.getCluster().removeMembershipListener(nodeDisconnectedListenerUUID);

        // Removing the operationalProcess from the replicated map
        operationalProcesses.keySet().forEach(
          clusterNodeProcess -> {
            if (clusterNodeProcess.getNodeUuid().equals(hzMember.getUuid())) {
              operationalProcesses.remove(clusterNodeProcess);
            }
          });

        // Shutdown Hazelcast properly
        hzMember.close();
      } catch (HazelcastInstanceNotActiveException e) {
        // hazelcastCluster may be already closed by the shutdown hook
        LOGGER.debug("Unable to close Hazelcast cluster", e);
      }
    }
  }

  private class OperationalProcessListener implements EntryListener<ClusterProcess, Boolean> {
    @Override
    public void entryAdded(EntryEvent<ClusterProcess, Boolean> event) {
      if (event.getValue()) {
        listeners.forEach(appStateListener -> appStateListener.onAppStateOperational(event.getKey().getProcessId()));
      }
    }

    @Override
    public void entryRemoved(EntryEvent<ClusterProcess, Boolean> event) {
      // Ignore it
    }

    @Override
    public void entryUpdated(EntryEvent<ClusterProcess, Boolean> event) {
      if (event.getValue()) {
        listeners.forEach(appStateListener -> appStateListener.onAppStateOperational(event.getKey().getProcessId()));
      }
    }

    @Override
    public void entryEvicted(EntryEvent<ClusterProcess, Boolean> event) {
      // Ignore it
    }

    @Override
    public void mapCleared(MapEvent event) {
      // Ignore it
    }

    @Override
    public void mapEvicted(MapEvent event) {
      // Ignore it
    }
  }

  private class NodeDisconnectedListener implements MembershipListener {
    @Override
    public void memberAdded(MembershipEvent membershipEvent) {
      // Nothing to do
    }

    @Override
    public void memberRemoved(MembershipEvent membershipEvent) {
      removeOperationalProcess(membershipEvent.getMember().getUuid());
      if (membershipEvent.getMembers().stream()
        .noneMatch(this::isAppNode)) {
        purgeSharedMemoryForAppNodes();
      }
    }

    @Override
    public void memberAttributeChanged(MemberAttributeEvent memberAttributeEvent) {
      // Nothing to do
    }

    private boolean isAppNode(Member member) {
      return NodeType.APPLICATION.getValue().equals(member.getStringAttribute(NODE_TYPE.getKey()));
    }

    private void removeOperationalProcess(String uuid) {
      for (ClusterProcess clusterProcess : operationalProcesses.keySet()) {
        if (clusterProcess.getNodeUuid().equals(uuid)) {
          LOGGER.debug("Set node process off for [{}:{}] : ", clusterProcess.getNodeUuid(), clusterProcess.getProcessId());
          hzMember.getReplicatedMap(OPERATIONAL_PROCESSES).put(clusterProcess, Boolean.FALSE);
        }
      }
    }

    private void purgeSharedMemoryForAppNodes() {
      LOGGER.info("No more application nodes, clearing cluster information about application nodes.");
      hzMember.getAtomicReference(LEADER).clear();
      hzMember.getAtomicReference(SONARQUBE_VERSION).clear();
    }
  }
}
