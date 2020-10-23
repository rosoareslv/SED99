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
package org.sonar.server.permission.ws.template;

import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.utils.System2;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.permission.template.PermissionTemplateDto;
import org.sonar.server.permission.ws.PermissionsWsAction;
import org.sonar.server.user.UserSession;
import org.sonarqube.ws.WsPermissions.CreateTemplateWsResponse;
import org.sonarqube.ws.WsPermissions.PermissionTemplate;
import org.sonarqube.ws.client.permission.CreateTemplateWsRequest;

import static java.lang.String.format;
import static org.sonar.server.permission.PermissionPrivilegeChecker.checkGlobalAdminUser;
import static org.sonar.server.permission.ws.PermissionRequestValidator.MSG_TEMPLATE_WITH_SAME_NAME;
import static org.sonar.server.permission.ws.PermissionRequestValidator.validateProjectPattern;
import static org.sonar.server.permission.ws.PermissionRequestValidator.validateTemplateNameFormat;
import static org.sonar.server.permission.ws.PermissionsWsParametersBuilder.createTemplateDescriptionParameter;
import static org.sonar.server.permission.ws.PermissionsWsParametersBuilder.createTemplateProjectKeyPatternParameter;
import static org.sonar.server.permission.ws.template.PermissionTemplateDtoBuilder.create;
import static org.sonar.server.permission.ws.template.PermissionTemplateDtoToPermissionTemplateResponse.toPermissionTemplateResponse;
import static org.sonar.server.ws.WsUtils.checkRequest;
import static org.sonar.server.ws.WsUtils.writeProtobuf;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_DESCRIPTION;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_NAME;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PROJECT_KEY_PATTERN;

public class CreateTemplateAction implements PermissionsWsAction {
  private final DbClient dbClient;
  private final UserSession userSession;
  private final System2 system;

  public CreateTemplateAction(DbClient dbClient, UserSession userSession, System2 system) {
    this.dbClient = dbClient;
    this.userSession = userSession;
    this.system = system;
  }

  @Override
  public void define(WebService.NewController context) {
    WebService.NewAction action = context.createAction("create_template")
      .setDescription("Create a permission template.<br />" +
        "It requires administration permissions to access.")
      .setResponseExample(getClass().getResource("create_template-example.json"))
      .setSince("5.2")
      .setPost(true)
      .setHandler(this);

    action.createParam(PARAM_NAME)
      .setRequired(true)
      .setDescription("Name")
      .setExampleValue("Financial Service Permissions");

    createTemplateProjectKeyPatternParameter(action);
    createTemplateDescriptionParameter(action);
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    CreateTemplateWsResponse createTemplateWsResponse = doHandle(toCreateTemplateWsRequest(request));
    writeProtobuf(createTemplateWsResponse, request, response);
  }

  private CreateTemplateWsResponse doHandle(CreateTemplateWsRequest request) {
    String name = request.getName();
    String description = request.getDescription();
    String projectPattern = request.getProjectKeyPattern();

    DbSession dbSession = dbClient.openSession(false);
    try {
      checkGlobalAdminUser(userSession);
      validateTemplateNameForCreation(dbSession, name);
      validateProjectPattern(projectPattern);

      PermissionTemplateDto permissionTemplate = insertTemplate(dbSession, name, description, projectPattern);

      return buildResponse(permissionTemplate);
    } finally {
      dbClient.closeSession(dbSession);
    }
  }

  private static CreateTemplateWsRequest toCreateTemplateWsRequest(Request request) {
    return new CreateTemplateWsRequest()
      .setName(request.mandatoryParam(PARAM_NAME))
      .setDescription(request.param(PARAM_DESCRIPTION))
      .setProjectKeyPattern(request.param(PARAM_PROJECT_KEY_PATTERN));
  }

  private void validateTemplateNameForCreation(DbSession dbSession, String name) {
    validateTemplateNameFormat(name);

    PermissionTemplateDto permissionTemplateWithSameName = dbClient.permissionTemplateDao().selectByName(dbSession, name);
    checkRequest(permissionTemplateWithSameName == null, format(MSG_TEMPLATE_WITH_SAME_NAME, name));
  }

  private PermissionTemplateDto insertTemplate(DbSession dbSession, String name, String description, String projectPattern) {
    PermissionTemplateDto template = dbClient.permissionTemplateDao().insert(dbSession, create(system)
      .setName(name)
      .setDescription(description)
      .setProjectKeyPattern(projectPattern)
      .toDto());
    dbSession.commit();
    return template;
  }

  private static CreateTemplateWsResponse buildResponse(PermissionTemplateDto permissionTemplateDto) {
    PermissionTemplate permissionTemplateBuilder = toPermissionTemplateResponse(permissionTemplateDto);
    return CreateTemplateWsResponse.newBuilder().setPermissionTemplate(permissionTemplateBuilder).build();
  }
}
