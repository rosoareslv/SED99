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

package org.sonar.server.setting.ws;

import com.google.common.collect.ImmutableMap;
import com.google.gson.Gson;
import java.net.HttpURLConnection;
import java.util.List;
import javax.annotation.Nullable;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.PropertyType;
import org.sonar.api.config.PropertyDefinition;
import org.sonar.api.config.PropertyDefinitions;
import org.sonar.api.config.PropertyFieldDefinition;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.server.ws.WebService.Param;
import org.sonar.api.utils.System2;
import org.sonar.api.web.UserRole;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.property.PropertyDbTester;
import org.sonar.db.property.PropertyDto;
import org.sonar.db.property.PropertyQuery;
import org.sonar.scanner.protocol.GsonHelper;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.i18n.I18nRule;
import org.sonar.server.platform.SettingsChangeNotifier;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.ws.TestRequest;
import org.sonar.server.ws.TestResponse;
import org.sonar.server.ws.WsActionTester;

import static com.google.common.collect.Lists.newArrayList;
import static java.util.Collections.singletonList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.groups.Tuple.tuple;
import static org.sonar.db.component.ComponentTesting.newView;
import static org.sonar.db.metric.MetricTesting.newMetricDto;
import static org.sonar.db.property.PropertyTesting.newComponentPropertyDto;
import static org.sonar.db.property.PropertyTesting.newGlobalPropertyDto;
import static org.sonar.db.user.UserTesting.newUserDto;

public class SetActionTest {

  private static final Gson GSON = GsonHelper.create();

  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone()
    .setGlobalPermissions(GlobalPermissions.SYSTEM_ADMIN);
  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);
  private PropertyDbTester propertyDb = new PropertyDbTester(db);
  private DbClient dbClient = db.getDbClient();
  private DbSession dbSession = db.getSession();
  private ComponentFinder componentFinder = new ComponentFinder(dbClient);

  private I18nRule i18n = new I18nRule();
  private PropertyDefinitions definitions = new PropertyDefinitions();
  private FakeSettingsNotifier settingsChangeNotifier = new FakeSettingsNotifier(dbClient);
  private SettingsUpdater settingsUpdater = new SettingsUpdater(dbClient, definitions);
  private SettingValidations validations = new SettingValidations(definitions, dbClient, i18n);
  private SetAction underTest = new SetAction(definitions, dbClient, componentFinder, userSession, settingsUpdater, settingsChangeNotifier, validations);

  private WsActionTester ws = new WsActionTester(underTest);

  @Test
  public void empty_204_response() {
    TestResponse result = ws.newRequest()
      .setParam("key", "my.key")
      .setParam("value", "my value")
      .execute();

    assertThat(result.getStatus()).isEqualTo(HttpURLConnection.HTTP_NO_CONTENT);
    assertThat(result.getInput()).isEmpty();
  }

  @Test
  public void persist_new_global_setting() {
    callForGlobalSetting("my.key", "my,value");

    assertGlobalSetting("my.key", "my,value");
    assertThat(settingsChangeNotifier.wasCalled).isTrue();
  }

  @Test
  public void update_existing_global_setting() {
    propertyDb.insertProperty(newGlobalPropertyDto("my.key", "my value"));
    assertGlobalSetting("my.key", "my value");

    callForGlobalSetting("my.key", "my new value");

    assertGlobalSetting("my.key", "my new value");
    assertThat(settingsChangeNotifier.wasCalled).isTrue();
  }

  @Test
  public void persist_new_project_setting() {
    propertyDb.insertProperty(newGlobalPropertyDto("my.key", "my global value"));
    ComponentDto project = db.components().insertProject();

    callForProjectSettingByUuid("my.key", "my project value", project.uuid());

    assertGlobalSetting("my.key", "my global value");
    assertComponentSetting("my.key", "my project value", project.getId());
    assertThat(settingsChangeNotifier.wasCalled).isFalse();
  }

  @Test
  public void persist_project_property_with_project_admin_permission() {
    ComponentDto project = db.components().insertProject();
    userSession.anonymous().addProjectUuidPermissions(UserRole.ADMIN, project.uuid());

    callForProjectSettingByUuid("my.key", "my value", project.uuid());

    assertComponentSetting("my.key", "my value", project.getId());
  }

  @Test
  public void update_existing_project_setting() {
    propertyDb.insertProperty(newGlobalPropertyDto("my.key", "my global value"));
    ComponentDto project = db.components().insertProject();
    propertyDb.insertProperty(newComponentPropertyDto("my.key", "my project value", project));
    assertComponentSetting("my.key", "my project value", project.getId());

    callForProjectSettingByKey("my.key", "my new project value", project.key());

    assertComponentSetting("my.key", "my new project value", project.getId());
  }

  @Test
  public void persist_several_multi_value_setting() {
    callForMultiValueGlobalSetting("my.key", newArrayList("first,Value", "second,Value", "third,Value"));

    String expectedValue = "first%2CValue,second%2CValue,third%2CValue";
    assertGlobalSetting("my.key", expectedValue);
    assertThat(settingsChangeNotifier.wasCalled).isTrue();
  }

  @Test
  public void persist_one_multi_value_setting() {
    callForMultiValueGlobalSetting("my.key", newArrayList("first,Value"));

    assertGlobalSetting("my.key", "first%2CValue");
  }

  @Test
  public void persist_property_set_setting() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("firstField")
          .name("First Field")
          .type(PropertyType.STRING)
          .build(),
        PropertyFieldDefinition.build("secondField")
          .name("Second Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    callForGlobalPropertySet("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue", "secondField", "secondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "anotherFirstValue", "secondField", "anotherSecondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "yetFirstValue", "secondField", "yetSecondValue"))));

    assertThat(dbClient.propertiesDao().selectGlobalProperties(dbSession)).hasSize(7);
    assertGlobalSetting("my.key", "1,2,3");
    assertGlobalSetting("my.key.1.firstField", "firstValue");
    assertGlobalSetting("my.key.1.secondField", "secondValue");
    assertGlobalSetting("my.key.2.firstField", "anotherFirstValue");
    assertGlobalSetting("my.key.2.secondField", "anotherSecondValue");
    assertGlobalSetting("my.key.3.firstField", "yetFirstValue");
    assertGlobalSetting("my.key.3.secondField", "yetSecondValue");
    assertThat(settingsChangeNotifier.wasCalled).isTrue();
  }

  @Test
  public void update_property_set_setting() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("firstField")
          .name("First Field")
          .type(PropertyType.STRING)
          .build(),
        PropertyFieldDefinition.build("secondField")
          .name("Second Field")
          .type(PropertyType.STRING)
          .build()))
      .build());
    propertyDb.insertProperties(
      newGlobalPropertyDto("my.key", "1,2,3,4"),
      newGlobalPropertyDto("my.key.1.firstField", "oldFirstValue"),
      newGlobalPropertyDto("my.key.1.secondField", "oldSecondValue"),
      newGlobalPropertyDto("my.key.2.firstField", "anotherOldFirstValue"),
      newGlobalPropertyDto("my.key.2.secondField", "anotherOldSecondValue"),
      newGlobalPropertyDto("my.key.3.firstField", "oldFirstValue"),
      newGlobalPropertyDto("my.key.3.secondField", "oldSecondValue"),
      newGlobalPropertyDto("my.key.4.firstField", "anotherOldFirstValue"),
      newGlobalPropertyDto("my.key.4.secondField", "anotherOldSecondValue"));

    callForGlobalPropertySet("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue", "secondField", "secondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "anotherFirstValue", "secondField", "anotherSecondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "yetFirstValue", "secondField", "yetSecondValue"))));

    assertThat(dbClient.propertiesDao().selectGlobalProperties(dbSession)).hasSize(7);
    assertGlobalSetting("my.key", "1,2,3");
    assertGlobalSetting("my.key.1.firstField", "firstValue");
    assertGlobalSetting("my.key.1.secondField", "secondValue");
    assertGlobalSetting("my.key.2.firstField", "anotherFirstValue");
    assertGlobalSetting("my.key.2.secondField", "anotherSecondValue");
    assertGlobalSetting("my.key.3.firstField", "yetFirstValue");
    assertGlobalSetting("my.key.3.secondField", "yetSecondValue");
    assertThat(settingsChangeNotifier.wasCalled).isTrue();
  }

  @Test
  public void update_property_set_on_component_setting() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .onQualifiers(Qualifiers.PROJECT)
      .fields(newArrayList(
        PropertyFieldDefinition.build("firstField")
          .name("First Field")
          .type(PropertyType.STRING)
          .build(),
        PropertyFieldDefinition.build("secondField")
          .name("Second Field")
          .type(PropertyType.STRING)
          .build()))
      .build());
    ComponentDto project = db.components().insertProject();
    propertyDb.insertProperties(
      newGlobalPropertyDto("my.key", "1"),
      newGlobalPropertyDto("my.key.1.firstField", "oldFirstValue"),
      newGlobalPropertyDto("my.key.1.secondField", "oldSecondValue"),
      newComponentPropertyDto("my.key", "1", project),
      newComponentPropertyDto("my.key.1.firstField", "componentFirstValue", project),
      newComponentPropertyDto("my.key.1.firstField", "componentSecondValue", project));

    callForComponentPropertySetByUuid("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue", "secondField", "secondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "anotherFirstValue", "secondField", "anotherSecondValue"))),
      project.uuid());

    assertThat(dbClient.propertiesDao().selectGlobalProperties(dbSession)).hasSize(3);
    assertThat(dbClient.propertiesDao().selectProjectProperties(dbSession, project.key())).hasSize(5);
    assertGlobalSetting("my.key", "1");
    assertGlobalSetting("my.key.1.firstField", "oldFirstValue");
    assertGlobalSetting("my.key.1.secondField", "oldSecondValue");
    Long projectId = project.getId();
    assertComponentSetting("my.key", "1,2", projectId);
    assertComponentSetting("my.key.1.firstField", "firstValue", projectId);
    assertComponentSetting("my.key.1.secondField", "secondValue", projectId);
    assertComponentSetting("my.key.2.firstField", "anotherFirstValue", projectId);
    assertComponentSetting("my.key.2.secondField", "anotherSecondValue", projectId);
    assertThat(settingsChangeNotifier.wasCalled).isFalse();
  }

  @Test
  public void persist_multi_value_with_type_metric() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.METRIC)
      .defaultValue("default")
      .multiValues(true)
      .build());
    dbClient.metricDao().insert(dbSession, newMetricDto().setKey("metric.key.1"));
    dbClient.metricDao().insert(dbSession, newMetricDto().setKey("metric.key.2"));
    dbSession.commit();

    callForMultiValueGlobalSetting("my.key", newArrayList("metric.key.1", "metric.key.2"));

    assertGlobalSetting("my.key", "metric.key.1,metric.key.2");
  }

  @Test
  public void persist_multi_value_with_type_login() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.USER_LOGIN)
      .defaultValue("default")
      .multiValues(true)
      .build());
    db.users().insertUser(newUserDto().setLogin("login.1"));
    db.users().insertUser(newUserDto().setLogin("login.2"));

    callForMultiValueGlobalSetting("my.key", newArrayList("login.1", "login.2"));

    assertGlobalSetting("my.key", "login.1,login.2");
  }

  @Test
  public void user_setting_is_not_updated() {
    propertyDb.insertProperty(newGlobalPropertyDto("my.key", "my user value").setUserId(42L));
    propertyDb.insertProperty(newGlobalPropertyDto("my.key", "my global value"));

    callForGlobalSetting("my.key", "my new global value");

    assertGlobalSetting("my.key", "my new global value");
    assertUserSetting("my.key", "my user value", 42L);
  }

  @Test
  public void persist_global_property_with_deprecated_key() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .deprecatedKey("my.old.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.STRING)
      .defaultValue("default")
      .build());

    callForGlobalSetting("my.old.key", "My Value");

    assertGlobalSetting("my.key", "My Value");
  }

  @Test
  public void fail_when_no_key() {
    expectedException.expect(IllegalArgumentException.class);

    callForGlobalSetting(null, "my value");
  }

  @Test
  public void fail_when_empty_key_value() {
    expectedException.expect(IllegalArgumentException.class);
    expectedException.expectMessage("Setting key is mandatory and must not be empty");

    callForGlobalSetting("  ", "my value");
  }

  @Test
  public void fail_when_no_value() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("One and only one of 'value', 'values', 'fieldValues' must be provided");

    callForGlobalSetting("my.key", null);
  }

  @Test
  public void fail_when_empty_value() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("A non empty value must be provided");

    callForGlobalSetting("my.key", "");
  }

  @Test
  public void fail_when_one_empty_value_on_multi_value() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("A non empty value must be provided");

    callForMultiValueGlobalSetting("my.key", newArrayList("oneValue", "  ", "anotherValue"));

  }

  @Test
  public void fail_when_insufficient_privileges() {
    userSession.anonymous().setGlobalPermissions(GlobalPermissions.QUALITY_PROFILE_ADMIN);
    expectedException.expect(ForbiddenException.class);

    callForGlobalSetting("my.key", "my value");
  }

  @Test
  public void fail_when_data_and_type_do_not_match() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.INTEGER)
      .defaultValue("default")
      .build());
    i18n.put("property.error.notInteger", "Not an integer error message");
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Not an integer error message");

    callForGlobalSetting("my.key", "My Value");
  }

  @Test
  public void fail_when_data_and_metric_type_with_invalid_key() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.METRIC)
      .defaultValue("default")
      .multiValues(true)
      .build());
    dbClient.metricDao().insert(dbSession, newMetricDto().setKey("metric.key"));
    dbClient.metricDao().insert(dbSession, newMetricDto().setKey("metric.disabled.key").setEnabled(false));
    dbSession.commit();

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Error when validating metric setting with key 'my.key' and values [metric.key, metric.disabled.key]. A value is not a valid metric key.");

    callForMultiValueGlobalSetting("my.key", newArrayList("metric.key", "metric.disabled.key"));
  }

  @Test
  public void fail_when_data_and_login_type_with_invalid_login() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.USER_LOGIN)
      .defaultValue("default")
      .multiValues(true)
      .build());
    db.users().insertUser(newUserDto().setLogin("login.1"));
    db.users().insertUser(newUserDto().setLogin("login.2").setActive(false));

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Error when validating login setting with key 'my.key' and values [login.1, login.2]. A value is not a valid login.");

    callForMultiValueGlobalSetting("my.key", newArrayList("login.1", "login.2"));
  }

  @Test
  public void fail_when_data_and_type_do_not_match_with_unknown_error_key() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.INTEGER)
      .defaultValue("default")
      .build());
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Error when validating setting with key 'my.key' and value [My Value, My Other Value]");

    callForMultiValueGlobalSetting("my.key", newArrayList("My Value", "My Other Value"));
  }

  @Test
  public void fail_when_global_with_property_only_on_projects() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.INTEGER)
      .defaultValue("default")
      .onlyOnQualifiers(Qualifiers.PROJECT)
      .build());
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Setting 'my.key' cannot be global");

    callForGlobalSetting("my.key", "42");
  }

  @Test
  public void fail_when_view_property_when_on_projects_only() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.STRING)
      .defaultValue("default")
      .onQualifiers(Qualifiers.PROJECT)
      .build());
    ComponentDto view = db.components().insertComponent(newView("view-uuid"));
    i18n.put("qualifier." + Qualifiers.VIEW, "View");
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Setting 'my.key' cannot be set on a View");

    callForProjectSettingByUuid("my.key", "My Value", view.uuid());
  }

  @Test
  public void fail_when_single_and_multi_value_provided() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("One and only one of 'value', 'values', 'fieldValues' must be provided");

    call("my.key", "My Value", newArrayList("Another Value"), null, null, null);
  }

  @Test
  public void fail_when_multi_definition_and_single_value_provided() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .type(PropertyType.STRING)
      .multiValues(true)
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Parameter 'value' must be used for single value setting. Parameter 'values' must be used for multi value setting.");

    callForGlobalSetting("my.key", "My Value");
  }

  @Test
  public void fail_when_single_definition_and_multi_value_provided() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .type(PropertyType.STRING)
      .multiValues(false)
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Parameter 'value' must be used for single value setting. Parameter 'values' must be used for multi value setting.");

    callForMultiValueGlobalSetting("my.key", newArrayList("My Value"));
  }

  @Test
  public void fail_when_empty_values_on_one_property_set() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("firstField")
          .name("First Field")
          .type(PropertyType.STRING)
          .build(),
        PropertyFieldDefinition.build("secondField")
          .name("Second Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("A non empty value must be provided");

    callForGlobalPropertySet("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue", "secondField", "secondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "", "secondField", "")),
      GSON.toJson(ImmutableMap.of("firstField", "yetFirstValue", "secondField", "yetSecondValue"))));
  }

  @Test
  public void do_not_fail_when_only_one_empty_value_on_one_property_set() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("firstField")
          .name("First Field")
          .type(PropertyType.STRING)
          .build(),
        PropertyFieldDefinition.build("secondField")
          .name("Second Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    callForGlobalPropertySet("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue", "secondField", "secondValue")),
      GSON.toJson(ImmutableMap.of("firstField", "anotherFirstValue", "secondField", "")),
      GSON.toJson(ImmutableMap.of("firstField", "yetFirstValue", "secondField", "yetSecondValue"))));

    assertGlobalSetting("my.key", "1,2,3");
  }

  @Test
  public void fail_when_property_set_setting_is_not_defined() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Setting 'my.key' is undefined");

    callForGlobalPropertySet("my.key", singletonList("{\"field\":\"value\"}"));
  }

  @Test
  public void fail_when_property_set_with_unknown_field() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("field")
          .name("Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Unknown field key 'unknownField' for setting 'my.key'");

    callForGlobalPropertySet("my.key", newArrayList(GSON.toJson(ImmutableMap.of("field", "value", "unknownField", "anotherValue"))));
  }

  @Test
  public void fail_when_property_set_has_field_with_incorrect_type() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("field")
          .name("Field")
          .type(PropertyType.INTEGER)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Error when validating setting with key 'my.key'. Field 'field' has incorrect value 'notAnInt'.");

    callForGlobalPropertySet("my.key", newArrayList(GSON.toJson(ImmutableMap.of("field", "notAnInt"))));
  }

  @Test
  public void fail_when_property_set_has_a_null_field_value() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("field")
          .name("Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("A non empty value must be provided");

    callForGlobalPropertySet("my.key", newArrayList("{\"field\": null}"));
  }

  @Test
  public void fail_when_property_set_with_invalid_json() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("field")
          .name("Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("JSON 'incorrectJson:incorrectJson' does not respect expected format for setting 'my.key'. " +
      "Ex: {\"field1\":\"value1\", \"field2\":\"value2\"}");

    callForGlobalPropertySet("my.key", newArrayList("incorrectJson:incorrectJson"));
  }

  @Test
  public void fail_when_property_set_with_json_of_the_wrong_format() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(
        PropertyFieldDefinition.build("field")
          .name("Field")
          .type(PropertyType.STRING)
          .build()))
      .build());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("JSON '[{\"field\":\"v1\"}, {\"field\":\"v2\"}]' does not respect expected format for setting 'my.key'. " +
      "Ex: {\"field1\":\"value1\", \"field2\":\"value2\"}");

    callForGlobalPropertySet("my.key", newArrayList("[{\"field\":\"v1\"}, {\"field\":\"v2\"}]"));
  }

  @Test
  public void fail_when_property_set_on_component_of_global_setting() {
    definitions.addComponent(PropertyDefinition
      .builder("my.key")
      .name("foo")
      .description("desc")
      .category("cat")
      .subCategory("subCat")
      .type(PropertyType.PROPERTY_SET)
      .defaultValue("default")
      .fields(newArrayList(PropertyFieldDefinition.build("firstField").name("First Field").type(PropertyType.STRING).build()))
      .build());
    i18n.put("qualifier." + Qualifiers.PROJECT, "Project");
    ComponentDto project = db.components().insertProject();

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Setting 'my.key' cannot be set on a Project");

    callForComponentPropertySetByUuid("my.key", newArrayList(
      GSON.toJson(ImmutableMap.of("firstField", "firstValue"))), project.uuid());
  }

  @Test
  public void definition() {
    WebService.Action definition = ws.getDef();

    assertThat(definition.key()).isEqualTo("set");
    assertThat(definition.isPost()).isTrue();
    assertThat(definition.isInternal()).isTrue();
    assertThat(definition.since()).isEqualTo("6.1");
    assertThat(definition.params()).extracting(Param::key)
      .containsOnly("key", "value", "values", "fieldValues", "componentId", "componentKey");
  }

  private void assertGlobalSetting(String key, String value) {
    PropertyDto result = dbClient.propertiesDao().selectGlobalProperty(key);

    assertThat(result)
      .extracting(PropertyDto::getKey, PropertyDto::getValue, PropertyDto::getResourceId)
      .containsExactly(key, value, null);
  }

  private void assertUserSetting(String key, String value, long userId) {
    List<PropertyDto> result = dbClient.propertiesDao().selectByQuery(PropertyQuery.builder().setKey(key).setUserId((int) userId).build(), dbSession);

    assertThat(result).hasSize(1)
      .extracting(PropertyDto::getKey, PropertyDto::getValue, PropertyDto::getUserId)
      .containsExactly(tuple(key, value, userId));
  }

  private void assertComponentSetting(String key, String value, long componentId) {
    PropertyDto result = dbClient.propertiesDao().selectProjectProperty(componentId, key);

    assertThat(result)
      .extracting(PropertyDto::getKey, PropertyDto::getValue, PropertyDto::getResourceId)
      .containsExactly(key, value, componentId);
  }

  private void callForGlobalSetting(@Nullable String key, @Nullable String value) {
    call(key, value, null, null, null, null);
  }

  private void callForMultiValueGlobalSetting(@Nullable String key, @Nullable List<String> values) {
    call(key, null, values, null, null, null);
  }

  private void callForGlobalPropertySet(@Nullable String key, @Nullable List<String> fieldValues) {
    call(key, null, null, fieldValues, null, null);
  }

  private void callForComponentPropertySetByUuid(@Nullable String key, @Nullable List<String> fieldValues, @Nullable String componentUuid) {
    call(key, null, null, fieldValues, componentUuid, null);
  }

  private void callForProjectSettingByUuid(@Nullable String key, @Nullable String value, @Nullable String componentUuid) {
    call(key, value, null, null, componentUuid, null);
  }

  private void callForProjectSettingByKey(@Nullable String key, @Nullable String value, @Nullable String componentKey) {
    call(key, value, null, null, null, componentKey);
  }

  private void call(@Nullable String key, @Nullable String value, @Nullable List<String> values, @Nullable List<String> fieldValues, @Nullable String componentUuid,
    @Nullable String componentKey) {
    TestRequest request = ws.newRequest();

    if (key != null) {
      request.setParam("key", key);
    }

    if (value != null) {
      request.setParam("value", value);
    }

    if (values != null) {
      request.setMultiParam("values", values);
    }

    if (fieldValues != null) {
      request.setMultiParam("fieldValues", fieldValues);
    }

    if (componentUuid != null) {
      request.setParam("componentId", componentUuid);
    }

    if (componentKey != null) {
      request.setParam("componentKey", componentKey);
    }

    request.execute();
  }

  private static class FakeSettingsNotifier extends SettingsChangeNotifier {
    private final DbClient dbClient;

    private boolean wasCalled = false;

    private FakeSettingsNotifier(DbClient dbClient) {
      this.dbClient = dbClient;
    }

    @Override
    public void onGlobalPropertyChange(String key, @Nullable String value) {
      wasCalled = true;
      PropertyDto property = dbClient.propertiesDao().selectGlobalProperty(key);

      assertThat(property.getValue()).isEqualTo(value);
    }
  }
}
