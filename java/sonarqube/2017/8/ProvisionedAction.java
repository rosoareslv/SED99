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
package org.sonar.server.project.ws;

import com.google.common.io.Resources;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.function.Consumer;
import javax.annotation.Nullable;
import org.apache.ibatis.session.RowBounds;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.server.ws.Change;
import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.server.ws.WebService.Param;
import org.sonar.api.utils.DateUtils;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.server.es.SearchOptions;
import org.sonar.server.organization.DefaultOrganizationProvider;
import org.sonar.server.user.UserSession;
import org.sonarqube.ws.Common.Paging;
import org.sonarqube.ws.WsComponents.ProvisionedWsResponse;
import org.sonarqube.ws.WsComponents.ProvisionedWsResponse.Component;

import static com.google.common.collect.Sets.newHashSet;
import static java.util.Optional.ofNullable;
import static org.sonar.core.util.stream.MoreCollectors.toList;
import static org.sonar.db.permission.OrganizationPermission.PROVISION_PROJECTS;
import static org.sonar.server.es.SearchOptions.MAX_LIMIT;
import static org.sonar.server.project.Visibility.PRIVATE;
import static org.sonar.server.project.Visibility.PUBLIC;
import static org.sonar.server.project.ws.ProjectsWsSupport.PARAM_ORGANIZATION;
import static org.sonar.server.ws.WsUtils.writeProtobuf;

public class ProvisionedAction implements ProjectsWsAction {

  private static final Set<String> QUALIFIERS_FILTER = newHashSet(Qualifiers.PROJECT);
  private static final Set<String> POSSIBLE_FIELDS = newHashSet("uuid", "key", "name", "creationDate", "visibility");

  private final ProjectsWsSupport support;
  private final DbClient dbClient;
  private final UserSession userSession;
  private final DefaultOrganizationProvider defaultOrganizationProvider;

  public ProvisionedAction(ProjectsWsSupport support, DbClient dbClient, UserSession userSession, DefaultOrganizationProvider defaultOrganizationProvider) {
    this.support = support;
    this.dbClient = dbClient;
    this.userSession = userSession;
    this.defaultOrganizationProvider = defaultOrganizationProvider;
  }

  @Override
  public void define(WebService.NewController controller) {
    WebService.NewAction action = controller.createAction("provisioned");
    action
      .setDescription(
        "Get the list of provisioned projects.<br /> " +
          "Require 'Create Projects' permission.")
      .setSince("5.2")
      .setResponseExample(Resources.getResource(getClass(), "projects-example-provisioned.json"))
      .setHandler(this)
      .addPagingParams(100, MAX_LIMIT)
      .addSearchQuery("sonar", "names", "keys")
      .addFieldsParam(POSSIBLE_FIELDS);

    action.setChangelog(
      new Change("6.4", "The 'uuid' field is deprecated in the response"),
      new Change("6.4", "Paging response fields is now in a Paging object"));

    support.addOrganizationParam(action);
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    userSession.checkLoggedIn();

    SearchOptions options = new SearchOptions().setPage(
      request.mandatoryParamAsInt(Param.PAGE),
      request.mandatoryParamAsInt(Param.PAGE_SIZE));
    Set<String> desiredFields = desiredFields(request);
    String query = request.param(Param.TEXT_QUERY);

    try (DbSession dbSession = dbClient.openSession(false)) {
      OrganizationDto organization = support.getOrganization(dbSession,
        request.getParam(PARAM_ORGANIZATION).or(defaultOrganizationProvider.get()::getKey));
      userSession.checkPermission(PROVISION_PROJECTS, organization);

      RowBounds rowBounds = new RowBounds(options.getOffset(), options.getLimit());
      List<ComponentDto> projects = dbClient.componentDao().selectProvisioned(dbSession, organization.getUuid(), query, QUALIFIERS_FILTER, rowBounds);
      int nbOfProjects = dbClient.componentDao().countProvisioned(dbSession, organization.getUuid(), query, QUALIFIERS_FILTER);
      ProvisionedWsResponse result = ProvisionedWsResponse.newBuilder()
        .addAllProjects(writeProjects(projects, desiredFields))
        .setPaging(Paging.newBuilder()
          .setTotal(nbOfProjects)
          .setPageIndex(options.getPage())
          .setPageSize(options.getLimit()))
        .build();
      writeProtobuf(result, request, response);
    }
  }

  private static List<Component> writeProjects(List<ComponentDto> projects, Set<String> desiredFields) {
    return projects.stream().map(project -> {
      Component.Builder compBuilder = Component.newBuilder().setUuid(project.uuid());
      writeIfNeeded("key", project.getDbKey(), compBuilder::setKey, desiredFields);
      writeIfNeeded("name", project.name(), compBuilder::setName, desiredFields);
      writeIfNeeded("creationDate", project.getCreatedAt(), compBuilder::setCreationDate, desiredFields);
      writeIfNeeded("visibility", project.isPrivate() ? PRIVATE.getLabel() : PUBLIC.getLabel(), compBuilder::setVisibility, desiredFields);
      return compBuilder.build();
    }).collect(toList());
  }

  private static void writeIfNeeded(String fieldName, String value, Consumer<String> setter, Set<String> desiredFields) {
    if (desiredFields.contains(fieldName)) {
      setter.accept(value);
    }
  }

  private static void writeIfNeeded(String fieldName, @Nullable Date value, Consumer<String> setter, Set<String> desiredFields) {
    if (desiredFields.contains(fieldName)) {
      ofNullable(value).map(DateUtils::formatDateTime).ifPresent(setter);
    }
  }

  private static Set<String> desiredFields(Request request) {
    List<String> desiredFields = request.paramAsStrings(Param.FIELDS);
    if (desiredFields == null) {
      return POSSIBLE_FIELDS;
    }

    return newHashSet(desiredFields);
  }
}
