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
package org.sonar.ce.notification;

import javax.annotation.Nullable;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.Scopes;
import org.sonar.api.utils.System2;
import org.sonar.ce.queue.CeTask;
import org.sonar.ce.queue.CeTaskResult;
import org.sonar.ce.taskprocessor.CeWorker;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.RowNotFoundException;
import org.sonar.db.ce.CeActivityDto;
import org.sonar.db.ce.CeTaskTypes;
import org.sonar.db.component.ComponentDto;
import org.sonar.server.notification.NotificationService;

import static com.google.common.base.Preconditions.checkArgument;
import static java.lang.String.format;
import static java.util.Collections.singleton;

public class ReportAnalysisFailureNotificationExecutionListener implements CeWorker.ExecutionListener {
  private final NotificationService notificationService;
  private final DbClient dbClient;
  private final ReportAnalysisFailureNotificationSerializer taskFailureNotificationSerializer;
  private final System2 system2;

  public ReportAnalysisFailureNotificationExecutionListener(NotificationService notificationService, DbClient dbClient,
    ReportAnalysisFailureNotificationSerializer taskFailureNotificationSerializer, System2 system2) {
    this.notificationService = notificationService;
    this.dbClient = dbClient;
    this.taskFailureNotificationSerializer = taskFailureNotificationSerializer;
    this.system2 = system2;
  }

  @Override
  public void onStart(CeTask ceTask) {
    // nothing to do
  }

  @Override
  public void onEnd(CeTask ceTask, CeActivityDto.Status status, @Nullable CeTaskResult taskResult, @Nullable Throwable error) {
    if (status == CeActivityDto.Status.SUCCESS) {
      return;
    }
    String projectUuid = ceTask.getComponentUuid();
    if (!CeTaskTypes.REPORT.equals(ceTask.getType()) || projectUuid == null) {
      return;
    }

    if (notificationService.hasProjectSubscribersForTypes(projectUuid, singleton(ReportAnalysisFailureNotification.TYPE))) {
      try (DbSession dbSession = dbClient.openSession(false)) {
        ComponentDto projectDto = dbClient.componentDao().selectOrFailByUuid(dbSession, projectUuid);
        checkScopeAndQualifier(projectDto);
        CeActivityDto ceActivityDto = dbClient.ceActivityDao().selectByUuid(dbSession, ceTask.getUuid())
          .orElseThrow(() -> new RowNotFoundException(format("CeActivity with uuid '%s' not found", ceTask.getUuid())));
        ReportAnalysisFailureNotification taskFailureNotification = buildNotification(ceActivityDto, projectDto, error);
        notificationService.deliver(taskFailureNotificationSerializer.toNotification(taskFailureNotification));
      }
    }
  }

  /**
   * @throws IllegalArgumentException if specified {@link ComponentDto} is not a project.
   */
  private static void checkScopeAndQualifier(ComponentDto projectDto) {
    String scope = projectDto.scope();
    String qualifier = projectDto.qualifier();
    checkArgument(
      scope.equals(Scopes.PROJECT) && qualifier.equals(Qualifiers.PROJECT),
      "Component %s must be a project (scope=%s, qualifier=%s)", projectDto.uuid(), scope, qualifier);
  }

  private ReportAnalysisFailureNotification buildNotification(CeActivityDto ceActivityDto, ComponentDto projectDto, @Nullable Throwable error) {
    Long executedAt = ceActivityDto.getExecutedAt();
    return new ReportAnalysisFailureNotification(
      new ReportAnalysisFailureNotification.Project(
        projectDto.uuid(),
        projectDto.getKey(),
        projectDto.name(),
        projectDto.getBranch()),
      new ReportAnalysisFailureNotification.Task(
        ceActivityDto.getUuid(),
        ceActivityDto.getSubmittedAt(),
        executedAt == null ? system2.now() : executedAt),
      error == null ? null : error.getMessage());
  }
}
