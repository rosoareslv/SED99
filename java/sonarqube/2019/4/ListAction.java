/*
 * SonarQube
 * Copyright (C) 2009-2019 SonarSource SA
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
package org.sonar.server.branch.ws;

import com.google.common.collect.ImmutableSet;
import com.google.common.io.Resources;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import javax.annotation.Nullable;
import org.sonar.api.server.ws.Change;
import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.web.UserRole;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.BranchDto;
import org.sonar.db.component.BranchType;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.SnapshotDto;
import org.sonar.db.measure.LiveMeasureDto;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.issue.index.BranchStatistics;
import org.sonar.server.issue.index.IssueIndex;
import org.sonar.server.user.UserSession;
import org.sonar.server.ws.WsUtils;
import org.sonarqube.ws.Common;
import org.sonarqube.ws.ProjectBranches;

import static com.google.common.base.Preconditions.checkArgument;
import static java.util.Collections.singletonList;
import static java.util.Optional.ofNullable;
import static org.sonar.api.measures.CoreMetrics.ALERT_STATUS_KEY;
import static org.sonar.api.resources.Qualifiers.APP;
import static org.sonar.api.resources.Qualifiers.PROJECT;
import static org.sonar.api.utils.DateUtils.formatDateTime;
import static org.sonar.api.web.UserRole.USER;
import static org.sonar.core.util.stream.MoreCollectors.toList;
import static org.sonar.core.util.stream.MoreCollectors.uniqueIndex;
import static org.sonar.db.component.BranchType.LONG;
import static org.sonar.db.component.BranchType.SHORT;
import static org.sonar.db.permission.OrganizationPermission.SCAN;
import static org.sonar.server.branch.ws.BranchesWs.addProjectParam;
import static org.sonar.server.branch.ws.ProjectBranchesParameters.ACTION_LIST;
import static org.sonar.server.branch.ws.ProjectBranchesParameters.PARAM_PROJECT;
import static org.sonar.server.user.AbstractUserSession.insufficientPrivilegesException;

public class ListAction implements BranchWsAction {

  private static final Set<String> ALLOWED_QUALIFIERS = ImmutableSet.of(PROJECT, APP);

  private final DbClient dbClient;
  private final UserSession userSession;
  private final ComponentFinder componentFinder;
  private final IssueIndex issueIndex;

  public ListAction(DbClient dbClient, UserSession userSession, ComponentFinder componentFinder, IssueIndex issueIndex) {
    this.dbClient = dbClient;
    this.userSession = userSession;
    this.componentFinder = componentFinder;
    this.issueIndex = issueIndex;
  }

  @Override
  public void define(WebService.NewController context) {
    WebService.NewAction action = context.createAction(ACTION_LIST)
      .setSince("6.6")
      .setDescription("List the branches of a project.<br/>" +
        "Requires 'Browse' or 'Execute analysis' rights on the specified project.")
      .setResponseExample(Resources.getResource(getClass(), "list-example.json"))
      .setChangelog(new Change("7.2", "Application can be used on this web service"))
      .setHandler(this);

    addProjectParam(action);
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    String projectKey = request.mandatoryParam(PARAM_PROJECT);

    try (DbSession dbSession = dbClient.openSession(false)) {
      ComponentDto project = componentFinder.getByKey(dbSession, projectKey);
      checkPermission(project);
      checkArgument(ALLOWED_QUALIFIERS.contains(project.qualifier()), "Invalid project");

      Collection<BranchDto> branches = dbClient.branchDao().selectByComponent(dbSession, project).stream()
        .filter(b -> b.getBranchType() == SHORT || b.getBranchType() == LONG)
        .collect(toList());
      List<String> branchUuids = branches.stream().map(BranchDto::getUuid).collect(toList());

      Map<String, BranchDto> mergeBranchesByUuid = dbClient.branchDao()
        .selectByUuids(dbSession, branches.stream().map(BranchDto::getMergeBranchUuid).filter(Objects::nonNull).collect(toList()))
        .stream().collect(uniqueIndex(BranchDto::getUuid));
      Map<String, LiveMeasureDto> qualityGateMeasuresByComponentUuids = dbClient.liveMeasureDao()
        .selectByComponentUuidsAndMetricKeys(dbSession, branchUuids, singletonList(ALERT_STATUS_KEY)).stream()
        .collect(uniqueIndex(LiveMeasureDto::getComponentUuid));
      Map<String, BranchStatistics> branchStatisticsByBranchUuid = issueIndex.searchBranchStatistics(project.uuid(), branches.stream()
        .filter(b -> b.getBranchType().equals(SHORT))
        .map(BranchDto::getUuid).collect(toList())).stream()
        .collect(uniqueIndex(BranchStatistics::getBranchUuid, Function.identity()));
      Map<String, String> analysisDateByBranchUuid = dbClient.snapshotDao()
        .selectLastAnalysesByRootComponentUuids(dbSession, branchUuids).stream()
        .collect(uniqueIndex(SnapshotDto::getComponentUuid, s -> formatDateTime(s.getCreatedAt())));

      ProjectBranches.ListWsResponse.Builder protobufResponse = ProjectBranches.ListWsResponse.newBuilder();
      branches.forEach(b -> addBranch(protobufResponse, b, mergeBranchesByUuid, qualityGateMeasuresByComponentUuids.get(b.getUuid()), branchStatisticsByBranchUuid.get(b.getUuid()),
        analysisDateByBranchUuid.get(b.getUuid())));
      WsUtils.writeProtobuf(protobufResponse.build(), request, response);
    }
  }

  private static void addBranch(ProjectBranches.ListWsResponse.Builder response, BranchDto branch, Map<String, BranchDto> mergeBranchesByUuid,
    @Nullable LiveMeasureDto qualityGateMeasure, BranchStatistics branchStatistics, @Nullable String analysisDate) {
    ProjectBranches.Branch.Builder builder = toBranchBuilder(branch, Optional.ofNullable(mergeBranchesByUuid.get(branch.getMergeBranchUuid())));
    setBranchStatus(builder, branch, qualityGateMeasure, branchStatistics);
    if (analysisDate != null) {
      builder.setAnalysisDate(analysisDate);
    }
    response.addBranches(builder);
  }

  private static ProjectBranches.Branch.Builder toBranchBuilder(BranchDto branch, Optional<BranchDto> mergeBranch) {
    ProjectBranches.Branch.Builder builder = ProjectBranches.Branch.newBuilder();
    String branchKey = branch.getKey();
    ofNullable(branchKey).ifPresent(builder::setName);
    builder.setIsMain(branch.isMain());
    builder.setType(Common.BranchType.valueOf(branch.getBranchType().name()));
    if (branch.getBranchType() == SHORT) {
      if (mergeBranch.isPresent()) {
        String mergeBranchKey = mergeBranch.get().getKey();
        builder.setMergeBranch(mergeBranchKey);
      } else {
        builder.setIsOrphan(true);
      }
    }
    return builder;
  }

  private static void setBranchStatus(ProjectBranches.Branch.Builder builder, BranchDto branch, @Nullable LiveMeasureDto qualityGateMeasure,
    @Nullable BranchStatistics branchStatistics) {
    ProjectBranches.Status.Builder statusBuilder = ProjectBranches.Status.newBuilder();
    if (qualityGateMeasure != null) {
      ofNullable(qualityGateMeasure.getDataAsString()).ifPresent(statusBuilder::setQualityGateStatus);
    }
    if (branch.getBranchType() == BranchType.SHORT) {
      statusBuilder.setBugs(branchStatistics == null ? 0L : branchStatistics.getBugs());
      statusBuilder.setVulnerabilities(branchStatistics == null ? 0L : branchStatistics.getVulnerabilities());
      statusBuilder.setCodeSmells(branchStatistics == null ? 0L : branchStatistics.getCodeSmells());
    }

    builder.setStatus(statusBuilder);
  }

  private void checkPermission(ComponentDto component) {
    if (!userSession.hasComponentPermission(USER, component) &&
      !userSession.hasComponentPermission(UserRole.SCAN, component) &&
      !userSession.hasPermission(SCAN, component.getOrganizationUuid())) {
      throw insufficientPrivilegesException();
    }
  }
}
