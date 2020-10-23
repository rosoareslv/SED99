/*
 * SonarQube, open source software quality management tool.
 * Copyright (C) 2008-2014 SonarSource
 * mailto:contact AT sonarsource DOT com
 *
 * SonarQube is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * SonarQube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

package org.sonar.server.permission.ws;

import com.google.common.base.Optional;
import org.sonar.api.i18n.I18n;
import org.sonar.api.resources.ResourceTypes;
import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.server.ws.WebService.Param;
import org.sonar.api.utils.Paging;
import org.sonar.core.permission.ProjectPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDto;
import org.sonar.server.user.UserSession;
import org.sonarqube.ws.Common;
import org.sonarqube.ws.WsPermissions.Permission;
import org.sonarqube.ws.WsPermissions.SearchProjectPermissionsWsResponse;
import org.sonarqube.ws.WsPermissions.SearchProjectPermissionsWsResponse.Project;
import org.sonarqube.ws.client.permission.SearchProjectPermissionsWsRequest;

import static org.sonar.server.permission.PermissionPrivilegeChecker.checkGlobalAdminUser;
import static org.sonar.server.permission.PermissionPrivilegeChecker.checkProjectAdminUserByComponentKey;
import static org.sonar.server.permission.PermissionPrivilegeChecker.checkProjectAdminUserByComponentUuid;
import static org.sonar.server.permission.ws.PermissionRequestValidator.validateQualifier;
import static org.sonar.server.permission.ws.PermissionsWsParametersBuilder.createProjectParameter;
import static org.sonar.server.permission.ws.WsProjectRef.newOptionalWsProjectRef;
import static org.sonar.server.ws.WsParameterBuilder.QualifierParameterContext.newQualifierParameterContext;
import static org.sonar.server.ws.WsParameterBuilder.createRootQualifierParameter;
import static org.sonar.server.ws.WsUtils.writeProtobuf;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PROJECT_ID;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PROJECT_KEY;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_QUALIFIER;

public class SearchProjectPermissionsAction implements PermissionsWsAction {
  private static final String PROPERTY_PREFIX = "projects_role.";
  private static final String DESCRIPTION_SUFFIX = ".desc";

  private final DbClient dbClient;
  private final UserSession userSession;
  private final I18n i18n;
  private final ResourceTypes resourceTypes;
  private final SearchProjectPermissionsDataLoader dataLoader;

  public SearchProjectPermissionsAction(DbClient dbClient, UserSession userSession, I18n i18n, ResourceTypes resourceTypes, SearchProjectPermissionsDataLoader dataLoader) {
    this.dbClient = dbClient;
    this.userSession = userSession;
    this.i18n = i18n;
    this.resourceTypes = resourceTypes;
    this.dataLoader = dataLoader;
  }

  @Override
  public void define(WebService.NewController context) {
    WebService.NewAction action = context.createAction("search_project_permissions")
      .setDescription("List project permissions. A project can be a technical project, a view or a developer.<br />" +
        "Requires 'Administer System' permission or 'Administer' rights on the specified project.")
      .setResponseExample(getClass().getResource("search_project_permissions-example.json"))
      .setSince("5.2")
      .addPagingParams(25)
      .addSearchQuery("sonarq", "project names", "project keys")
      .setHandler(this);

    createProjectParameter(action);
    createRootQualifierParameter(action, newQualifierParameterContext(userSession, i18n, resourceTypes))
      .setSince("5.3");
  }

  @Override
  public void handle(Request wsRequest, Response wsResponse) throws Exception {
    SearchProjectPermissionsWsResponse searchProjectPermissionsWsResponse = doHandle(toSearchProjectPermissionsWsRequest(wsRequest));
    writeProtobuf(searchProjectPermissionsWsResponse, wsRequest, wsResponse);
  }

  private SearchProjectPermissionsWsResponse doHandle(SearchProjectPermissionsWsRequest request) {
    checkRequestAndPermissions(request);
    DbSession dbSession = dbClient.openSession(false);
    try {
      validateQualifier(request.getQualifier(), resourceTypes);
      SearchProjectPermissionsData data = dataLoader.load(request);
      return buildResponse(data);
    } finally {
      dbClient.closeSession(dbSession);
    }
  }

  private static SearchProjectPermissionsWsRequest toSearchProjectPermissionsWsRequest(Request request) {
    return new SearchProjectPermissionsWsRequest()
      .setProjectId(request.param(PARAM_PROJECT_ID))
      .setProjectKey(request.param(PARAM_PROJECT_KEY))
      .setQualifier(request.param(PARAM_QUALIFIER))
      .setPage(request.mandatoryParamAsInt(Param.PAGE))
      .setPageSize(request.mandatoryParamAsInt(Param.PAGE_SIZE))
      .setQuery(request.param(Param.TEXT_QUERY));
  }

  private void checkRequestAndPermissions(SearchProjectPermissionsWsRequest request) {
    Optional<WsProjectRef> project = newOptionalWsProjectRef(request.getProjectId(), request.getProjectKey());
    boolean hasProject = project.isPresent();
    boolean hasProjectUuid = hasProject && project.get().uuid() != null;
    boolean hasProjectKey = hasProject && project.get().key() != null;

    if (hasProjectUuid) {
      checkProjectAdminUserByComponentUuid(userSession, project.get().uuid());
    } else if (hasProjectKey) {
      checkProjectAdminUserByComponentKey(userSession, project.get().key());
    } else {
      checkGlobalAdminUser(userSession);
    }
  }

  private SearchProjectPermissionsWsResponse buildResponse(SearchProjectPermissionsData data) {
    SearchProjectPermissionsWsResponse.Builder response = SearchProjectPermissionsWsResponse.newBuilder();
    Permission.Builder permissionResponse = Permission.newBuilder();

    Project.Builder rootComponentBuilder = Project.newBuilder();
    for (ComponentDto rootComponent : data.rootComponents()) {
      rootComponentBuilder
        .clear()
        .setId(rootComponent.uuid())
        .setKey(rootComponent.key())
        .setQualifier(rootComponent.qualifier())
        .setName(rootComponent.name());
      for (String permission : data.permissions(rootComponent.getId())) {
        rootComponentBuilder.addPermissions(
          permissionResponse
            .clear()
            .setKey(permission)
            .setUsersCount(data.userCount(rootComponent.getId(), permission))
            .setGroupsCount(data.groupCount(rootComponent.getId(), permission)));
      }
      response.addProjects(rootComponentBuilder);
    }

    for (String permissionKey : ProjectPermissions.ALL) {
      response.addPermissions(
        permissionResponse
          .clear()
          .setKey(permissionKey)
          .setName(i18nName(permissionKey))
          .setDescription(i18nDescriptionMessage(permissionKey)));
    }

    Paging paging = data.paging();
    response.setPaging(
      Common.Paging.newBuilder()
        .setPageIndex(paging.pageIndex())
        .setPageSize(paging.pageSize())
        .setTotal(paging.total()));

    return response.build();
  }

  private String i18nDescriptionMessage(String permissionKey) {
    return i18n.message(userSession.locale(), PROPERTY_PREFIX + permissionKey + DESCRIPTION_SUFFIX, "");
  }

  private String i18nName(String permissionKey) {
    return i18n.message(userSession.locale(), PROPERTY_PREFIX + permissionKey, permissionKey);
  }
}
