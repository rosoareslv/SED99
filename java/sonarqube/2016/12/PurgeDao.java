/*
 * SonarQube
 * Copyright (C) 2009-2016 SonarSource SA
 * mailto:contact AT sonarsource DOT com
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
package org.sonar.db.purge;

import com.google.common.collect.Lists;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import org.sonar.api.utils.System2;
import org.sonar.api.utils.log.Logger;
import org.sonar.api.utils.log.Loggers;
import org.sonar.core.util.stream.Collectors;
import org.sonar.db.Dao;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDao;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTreeQuery;
import org.sonar.db.component.ComponentTreeQuery.Strategy;

import static java.util.Collections.emptyList;
import static org.sonar.api.utils.DateUtils.dateToLong;
import static org.sonar.db.DatabaseUtils.executeLargeInputs;

/**
 * @since 2.14
 */
public class PurgeDao implements Dao {
  private static final Logger LOG = Loggers.get(PurgeDao.class);
  private static final String[] UNPROCESSED_STATUS = new String[] {"U"};

  private final ComponentDao componentDao;
  private final System2 system2;

  public PurgeDao(ComponentDao componentDao, System2 system2) {
    this.componentDao = componentDao;
    this.system2 = system2;
  }

  public void purge(DbSession session, PurgeConfiguration conf, PurgeListener listener, PurgeProfiler profiler) {
    PurgeMapper mapper = session.getMapper(PurgeMapper.class);
    PurgeCommands commands = new PurgeCommands(session, mapper, profiler);
    String rootUuid = conf.rootProjectIdUuid().getUuid();
    deleteAbortedAnalyses(rootUuid, commands);
    deleteDataOfComponentsWithoutHistoricalData(session, rootUuid, conf.scopesWithoutHistoricalData(), commands);
    purgeAnalyses(commands, rootUuid);
    purgeDisabledComponents(session, conf.getDisabledComponentUuids(), listener);
    deleteOldClosedIssues(conf, mapper, listener);
  }

  private static void purgeAnalyses(PurgeCommands commands, String rootUuid) {
    List<IdUuidPair> analysisUuids = commands.selectSnapshotIdUuids(
      new PurgeSnapshotQuery()
        .setComponentUuid(rootUuid)
        .setIslast(false)
        .setNotPurged(true));
    commands.purgeAnalyses(analysisUuids);
  }

  private static void deleteOldClosedIssues(PurgeConfiguration conf, PurgeMapper mapper, PurgeListener listener) {
    Date toDate = conf.maxLiveDateOfClosedIssues();
    String rootUuid = conf.rootProjectIdUuid().getUuid();
    List<String> issueKeys = mapper.selectOldClosedIssueKeys(rootUuid, dateToLong(toDate));
    executeLargeInputs(issueKeys, input -> {
      mapper.deleteIssueChangesFromIssueKeys(input);
      return emptyList();
    });
    executeLargeInputs(issueKeys, input -> {
      mapper.deleteIssuesFromKeys(input);
      return emptyList();
    });
    listener.onIssuesRemoval(rootUuid, issueKeys);
  }

  private static void deleteAbortedAnalyses(String rootUuid, PurgeCommands commands) {
    LOG.debug("<- Delete aborted builds");
    PurgeSnapshotQuery query = new PurgeSnapshotQuery()
      .setIslast(false)
      .setStatus(UNPROCESSED_STATUS)
      .setComponentUuid(rootUuid);
    commands.deleteAnalyses(query);
  }

  private void deleteDataOfComponentsWithoutHistoricalData(DbSession dbSession, String rootUuid, String[] scopesWithoutHistoricalData, PurgeCommands purgeCommands) {
    if (scopesWithoutHistoricalData.length == 0) {
      return;
    }

    List<String> analysisUuids = purgeCommands.selectSnapshotUuids(
      new PurgeSnapshotQuery()
        .setComponentUuid(rootUuid)
        .setIslast(false)
        .setNotPurged(true));
    List<String> componentWithoutHistoricalDataUuids = componentDao
      .selectDescendants(
        dbSession,
        ComponentTreeQuery.builder()
          .setBaseUuid(rootUuid)
          .setQualifiers(Arrays.asList(scopesWithoutHistoricalData))
          .setStrategy(Strategy.LEAVES)
          .build())
      .stream().map(ComponentDto::uuid)
      .collect(Collectors.toList());

    purgeCommands.deleteComponentMeasures(analysisUuids, componentWithoutHistoricalDataUuids);
  }

  private void purgeDisabledComponents(DbSession session, Collection<String> uuids, PurgeListener listener) {
    PurgeMapper mapper = mapper(session);
    executeLargeInputs(uuids,
      input -> {
        mapper.deleteResourceIndex(input);
        mapper.deleteFileSourcesByUuid(input);
        mapper.resolveComponentIssuesNotAlreadyResolved(input, system2.now());
        return emptyList();
      });

    for (String componentUuid : uuids) {
      listener.onComponentDisabling(componentUuid);
    }

    session.commit();
  }

  public List<PurgeableAnalysisDto> selectPurgeableAnalyses(String componentUuid, DbSession session) {
    List<PurgeableAnalysisDto> result = Lists.newArrayList();
    result.addAll(mapper(session).selectPurgeableAnalysesWithEvents(componentUuid));
    result.addAll(mapper(session).selectPurgeableAnalysesWithoutEvents(componentUuid));
    // sort by date
    Collections.sort(result);
    return result;
  }

  public PurgeDao deleteProject(DbSession session, String uuid) {
    PurgeProfiler profiler = new PurgeProfiler();
    PurgeCommands purgeCommands = new PurgeCommands(session, profiler);
    deleteProject(uuid, mapper(session), purgeCommands);
    return this;
  }

  private static void deleteProject(String rootUuid, PurgeMapper mapper, PurgeCommands commands) {
    List<IdUuidPair> childrenIds = mapper.selectComponentsByProjectUuid(rootUuid);
    commands.deleteAnalyses(rootUuid);
    commands.deleteComponents(childrenIds);
    commands.deleteFileSources(rootUuid);
    commands.deleteCeActivity(rootUuid);
    commands.deleteWebhookDeliveries(rootUuid);
  }

  public void deleteAnalyses(DbSession session, PurgeProfiler profiler, List<IdUuidPair> analysisIdUuids) {
    new PurgeCommands(session, profiler).deleteAnalyses(analysisIdUuids);
  }

  private static PurgeMapper mapper(DbSession session) {
    return session.getMapper(PurgeMapper.class);
  }
}
