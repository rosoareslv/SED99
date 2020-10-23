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
package org.sonar.server.setting.ws;

import java.util.List;
import java.util.Optional;
import org.sonar.api.config.PropertyDefinition;
import org.sonar.api.config.PropertyDefinitions;
import org.sonar.api.config.PropertyFieldDefinition;
import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDto;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.user.UserSession;
import org.sonarqube.ws.Settings;
import org.sonarqube.ws.Settings.ListDefinitionsWsResponse;
import org.sonarqube.ws.client.setting.ListDefinitionsRequest;

import static com.google.common.base.Strings.emptyToNull;
import static org.sonar.api.web.UserRole.USER;
import static org.sonar.core.util.Protobuf.setNullable;
import static org.sonar.server.ws.KeyExamples.KEY_PROJECT_EXAMPLE_001;
import static org.sonar.server.ws.WsUtils.writeProtobuf;
import static org.sonarqube.ws.client.setting.SettingsWsParameters.ACTION_LIST_DEFINITIONS;
import static org.sonarqube.ws.client.setting.SettingsWsParameters.PARAM_COMPONENT;

public class ListDefinitionsAction implements SettingsWsAction {

  private final DbClient dbClient;
  private final ComponentFinder componentFinder;
  private final UserSession userSession;
  private final PropertyDefinitions propertyDefinitions;
  private final SettingsWsSupport settingsWsSupport;

  public ListDefinitionsAction(DbClient dbClient, ComponentFinder componentFinder, UserSession userSession, PropertyDefinitions propertyDefinitions,
    SettingsWsSupport settingsWsSupport) {
    this.dbClient = dbClient;
    this.componentFinder = componentFinder;
    this.userSession = userSession;
    this.propertyDefinitions = propertyDefinitions;
    this.settingsWsSupport = settingsWsSupport;
  }

  @Override
  public void define(WebService.NewController context) {
    WebService.NewAction action = context.createAction(ACTION_LIST_DEFINITIONS)
      .setDescription("List settings definitions.<br>" +
        "Requires 'Browse' permission when a component is specified<br/>",
        "To access licensed settings, authentication is required<br/>" +
          "To access secured settings, one of the following permissions is required: " +
          "<ul>" +
          "<li>'Execute Analysis'</li>" +
          "<li>'Administer System'</li>" +
          "<li>'Administer' rights on the specified component</li>" +
          "</ul>")
      .setResponseExample(getClass().getResource("list_definitions-example.json"))
      .setSince("6.3")
      .setHandler(this);
    action.createParam(PARAM_COMPONENT)
      .setDescription("Component key")
      .setExampleValue(KEY_PROJECT_EXAMPLE_001);
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    writeProtobuf(doHandle(request), request, response);
  }

  private ListDefinitionsWsResponse doHandle(Request request) {
    ListDefinitionsRequest wsRequest = toWsRequest(request);
    Optional<ComponentDto> component = loadComponent(wsRequest);
    Optional<String> qualifier = getQualifier(component);
    ListDefinitionsWsResponse.Builder wsResponse = ListDefinitionsWsResponse.newBuilder();
    propertyDefinitions.getAll().stream()
      .filter(definition -> qualifier.isPresent() ? definition.qualifiers().contains(qualifier.get()) : definition.global())
      .filter(settingsWsSupport.isDefinitionVisible(component))
      .forEach(definition -> addDefinition(definition, wsResponse));
    return wsResponse.build();
  }

  private static ListDefinitionsRequest toWsRequest(Request request) {
    return ListDefinitionsRequest.builder()
      .setComponent(request.param(PARAM_COMPONENT))
      .build();
  }

  private static Optional<String> getQualifier(Optional<ComponentDto> component) {
    return component.isPresent() ? Optional.of(component.get().qualifier()) : Optional.empty();
  }

  private Optional<ComponentDto> loadComponent(ListDefinitionsRequest valuesRequest) {
    try (DbSession dbSession = dbClient.openSession(false)) {
      String componentKey = valuesRequest.getComponent();
      if (componentKey == null) {
        return Optional.empty();
      }
      ComponentDto component = componentFinder.getByKey(dbSession, componentKey);
      userSession.checkComponentPermission(USER, component);
      return Optional.of(component);
    }
  }

  private void addDefinition(PropertyDefinition definition, ListDefinitionsWsResponse.Builder wsResponse) {
    String key = definition.key();
    Settings.Definition.Builder builder = wsResponse.addDefinitionsBuilder()
      .setKey(key)
      .setType(Settings.Type.valueOf(definition.type().name()))
      .setMultiValues(definition.multiValues());
    setNullable(emptyToNull(definition.deprecatedKey()), builder::setDeprecatedKey);
    setNullable(emptyToNull(definition.name()), builder::setName);
    setNullable(emptyToNull(definition.description()), builder::setDescription);
    String category = propertyDefinitions.getCategory(key);
    setNullable(emptyToNull(category), builder::setCategory);
    String subCategory = propertyDefinitions.getSubCategory(key);
    setNullable(emptyToNull(subCategory), builder::setSubCategory);
    setNullable(emptyToNull(definition.defaultValue()), builder::setDefaultValue);
    List<String> options = definition.options();
    if (!options.isEmpty()) {
      builder.addAllOptions(options);
    }
    List<PropertyFieldDefinition> fields = definition.fields();
    if (!fields.isEmpty()) {
      fields.forEach(fieldDefinition -> addField(fieldDefinition, builder));
    }
  }

  private static void addField(PropertyFieldDefinition fieldDefinition, Settings.Definition.Builder builder) {
    builder.addFieldsBuilder()
      .setKey(fieldDefinition.key())
      .setName(fieldDefinition.name())
      .setDescription(fieldDefinition.description())
      .setType(Settings.Type.valueOf(fieldDefinition.type().name()))
      .addAllOptions(fieldDefinition.options())
      .build();
  }

}
