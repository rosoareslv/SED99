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

package org.sonar.server.component.ws;

import com.google.common.base.Throwables;
import java.io.IOException;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.utils.System2;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.NotFoundException;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.ws.TestRequest;
import org.sonar.server.ws.WsActionTester;
import org.sonarqube.ws.MediaTypes;
import org.sonarqube.ws.WsComponents;
import org.sonarqube.ws.WsComponents.BulkUpdateKeyWsResponse;
import org.sonarqube.ws.WsComponents.BulkUpdateKeyWsResponse.Key;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.tuple;
import static org.assertj.guava.api.Assertions.assertThat;
import static org.sonar.db.component.ComponentTesting.newFileDto;
import static org.sonar.db.component.ComponentTesting.newModuleDto;
import static org.sonar.db.component.ComponentTesting.newProjectDto;
import static org.sonar.test.JsonAssert.assertJson;
import static org.sonarqube.ws.client.component.ComponentsWsParameters.PARAM_DRY_RUN;
import static org.sonarqube.ws.client.component.ComponentsWsParameters.PARAM_FROM;
import static org.sonarqube.ws.client.component.ComponentsWsParameters.PARAM_ID;
import static org.sonarqube.ws.client.component.ComponentsWsParameters.PARAM_KEY;
import static org.sonarqube.ws.client.component.ComponentsWsParameters.PARAM_TO;

public class BulkUpdateKeyActionTest {
  static final String MY_PROJECT_KEY = "my_project";
  static final String FROM = "my_";
  static final String TO = "your_";

  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();
  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);
  ComponentDbTester componentDb = new ComponentDbTester(db);
  DbClient dbClient = db.getDbClient();
  DbSession dbSession = db.getSession();

  ComponentFinder componentFinder = new ComponentFinder(dbClient);

  WsActionTester ws = new WsActionTester(new BulkUpdateKeyAction(dbClient, componentFinder, userSession));

  @Before
  public void setUp() {
    userSession.setGlobalPermissions(GlobalPermissions.SYSTEM_ADMIN);
  }

  @Test
  public void json_example() {
    ComponentDto project = componentDb.insertComponent(newProjectDto().setKey("my_project"));
    componentDb.insertComponent(newModuleDto(project).setKey("my_project:module_1"));
    ComponentDto anotherProject = componentDb.insertComponent(newProjectDto().setKey("another_project"));
    componentDb.insertComponent(newModuleDto(anotherProject).setKey("my_new_project:module_1"));
    ComponentDto module2 = componentDb.insertComponent(newModuleDto(project).setKey("my_project:module_2"));
    componentDb.insertComponent(newFileDto(module2, null));

    String result = ws.newRequest()
      .setParam(PARAM_KEY, "my_project")
      .setParam(PARAM_FROM, "my_")
      .setParam(PARAM_TO, "my_new_")
      .setParam(PARAM_DRY_RUN, String.valueOf(true))
      .execute().getInput();

    assertJson(result).withStrictArrayOrder().isSimilarTo(getClass().getResource("bulk_update_key-example.json"));
  }

  @Test
  public void dry_run_by_key() {
    insertMyProject();

    BulkUpdateKeyWsResponse result = callDryRunByKey(MY_PROJECT_KEY, FROM, TO);

    assertThat(result.getKeysCount()).isEqualTo(1);
    assertThat(result.getKeys(0).getNewKey()).isEqualTo("your_project");
  }

  @Test
  public void bulk_update_project_key() {
    ComponentDto project = insertMyProject();
    ComponentDto module = componentDb.insertComponent(newModuleDto(project).setKey("my_project:root:module"));
    ComponentDto inactiveModule = componentDb.insertComponent(newModuleDto(project).setKey("my_project:root:inactive_module").setEnabled(false));
    ComponentDto file = componentDb.insertComponent(newFileDto(module, null).setKey("my_project:root:module:src/File.xoo"));
    ComponentDto inactiveFile = componentDb.insertComponent(newFileDto(module, null).setKey("my_project:root:module:src/InactiveFile.xoo").setEnabled(false));

    BulkUpdateKeyWsResponse result = callByUuid(project.uuid(), FROM, TO);

    assertThat(result.getKeysCount()).isEqualTo(2);
    assertThat(result.getKeysList()).extracting(Key::getKey, Key::getNewKey, Key::getDuplicate)
      .containsExactly(
        tuple(project.key(), "your_project", false),
        tuple(module.key(), "your_project:root:module", false));

    assertComponentKeyUpdated(project.key(), "your_project");
    assertComponentKeyUpdated(module.key(), "your_project:root:module");
    assertComponentKeyUpdated(file.key(), "your_project:root:module:src/File.xoo");
    assertComponentKeyNotUpdated(inactiveModule.key());
    assertComponentKeyNotUpdated(inactiveFile.key());
  }

  @Test
  public void bulk_update_provisioned_project_key() {
    String oldKey = "provisionedProject";
    String newKey = "provisionedProject2";
    ComponentDto provisionedProject = componentDb.insertComponent(newProjectDto().setKey(oldKey));

    callByKey(provisionedProject.key(), oldKey, newKey);

    assertComponentKeyUpdated(oldKey, newKey);
  }

  @Test
  public void fail_to_bulk_if_a_component_already_exists_with_the_same_key() {
    componentDb.insertComponent(newProjectDto().setKey("my_project"));
    componentDb.insertComponent(newProjectDto().setKey("your_project"));

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Impossible to update key: a component with key \"your_project\" already exists.");

    callByKey("my_project", "my_", "your_");
  }

  @Test
  public void fail_to_bulk_update_with_invalid_new_key() {
    insertMyProject();

    expectedException.expect(IllegalArgumentException.class);
    expectedException.expectMessage("Malformed key for 'my?project'. Allowed characters are alphanumeric, '-', '_', '.' and ':', with at least one non-digit.");

    callByKey(MY_PROJECT_KEY, FROM, "my?");
  }

  @Test
  public void fail_to_dry_bulk_update_with_invalid_new_key() {
    insertMyProject();

    expectedException.expect(IllegalArgumentException.class);
    expectedException.expectMessage("Malformed key for 'my?project'. Allowed characters are alphanumeric, '-', '_', '.' and ':', with at least one non-digit.");

    callDryRunByKey(MY_PROJECT_KEY, FROM, "my?");
  }

  @Test
  public void fail_to_bulk_update_if_not_project_or_module() {
    ComponentDto project = insertMyProject();
    ComponentDto file = componentDb.insertComponent(newFileDto(project, null));

    expectedException.expect(IllegalArgumentException.class);
    expectedException.expectMessage("Component updated must be a module or a key");

    callByKey(file.key(), FROM, TO);
  }

  @Test
  public void fail_if_from_string_is_not_provided() {
    expectedException.expect(IllegalArgumentException.class);

    ComponentDto project = insertMyProject();

    callDryRunByKey(project.key(), null, TO);
  }

  @Test
  public void fail_if_to_string_is_not_provided() {
    expectedException.expect(IllegalArgumentException.class);

    ComponentDto project = insertMyProject();

    callDryRunByKey(project.key(), FROM, null);
  }

  @Test
  public void fail_if_uuid_nor_key_provided() {
    expectedException.expect(IllegalArgumentException.class);

    call(null, null, FROM, TO, false);
  }

  @Test
  public void fail_if_uuid_and_key_provided() {
    expectedException.expect(IllegalArgumentException.class);

    ComponentDto project = insertMyProject();

    call(project.uuid(), project.key(), FROM, TO, false);
  }

  @Test
  public void fail_if_project_does_not_exist() {
    expectedException.expect(NotFoundException.class);

    callDryRunByUuid("UNKNOWN_UUID", FROM, TO);
  }

  @Test
  public void fail_if_insufficient_privileges() {
    expectedException.expect(ForbiddenException.class);
    userSession.anonymous();

    ComponentDto project = insertMyProject();

    callDryRunByUuid(project.uuid(), FROM, TO);
  }

  @Test
  public void api_definition() {
    WebService.Action definition = ws.getDef();

    assertThat(definition.isPost()).isTrue();
    assertThat(definition.since()).isEqualTo("6.1");
    assertThat(definition.key()).isEqualTo("bulk_update_key");
    assertThat(definition.params())
      .hasSize(5)
      .extracting(WebService.Param::key)
      .containsOnlyOnce("id", "key", "from", "to", "dryRun");
  }

  private void assertComponentKeyUpdated(String oldKey, String newKey) {
    assertThat(dbClient.componentDao().selectByKey(dbSession, oldKey)).isAbsent();
    assertThat(dbClient.componentDao().selectByKey(dbSession, newKey)).isPresent();
  }

  private void assertComponentKeyNotUpdated(String key) {
    assertThat(dbClient.componentDao().selectByKey(dbSession, key)).isPresent();
  }

  private ComponentDto insertMyProject() {
    return componentDb.insertComponent(newProjectDto().setKey(MY_PROJECT_KEY));
  }

  private WsComponents.BulkUpdateKeyWsResponse callDryRunByUuid(@Nullable String uuid, @Nullable String from, @Nullable String to) {
    return call(uuid, null, from, to, true);
  }

  private BulkUpdateKeyWsResponse callDryRunByKey(@Nullable String key, @Nullable String from, @Nullable String to) {
    return call(null, key, from, to, true);
  }

  private WsComponents.BulkUpdateKeyWsResponse callByUuid(@Nullable String uuid, @Nullable String from, @Nullable String to) {
    return call(uuid, null, from, to, false);
  }

  private BulkUpdateKeyWsResponse callByKey(@Nullable String key, @Nullable String from, @Nullable String to) {
    return call(null, key, from, to, false);
  }

  private BulkUpdateKeyWsResponse call(@Nullable String uuid, @Nullable String key, @Nullable String from, @Nullable String to, @Nullable Boolean dryRun) {
    TestRequest request = ws.newRequest()
      .setMediaType(MediaTypes.PROTOBUF);

    if (uuid != null) {
      request.setParam(PARAM_ID, uuid);
    }
    if (key != null) {
      request.setParam(PARAM_KEY, key);
    }
    if (from != null) {
      request.setParam(PARAM_FROM, from);
    }
    if (to != null) {
      request.setParam(PARAM_TO, to);
    }
    if (dryRun != null) {
      request.setParam(PARAM_DRY_RUN, String.valueOf(dryRun));
    }

    try {
      return WsComponents.BulkUpdateKeyWsResponse.parseFrom(request.execute().getInputStream());
    } catch (IOException e) {
      throw Throwables.propagate(e);
    }
  }
}
