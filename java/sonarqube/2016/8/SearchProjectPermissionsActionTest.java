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
package org.sonar.server.permission.ws;

import java.io.IOException;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.utils.System2;
import org.sonar.api.web.UserRole;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ResourceTypesRule;
import org.sonar.db.user.GroupDto;
import org.sonar.db.user.GroupRoleDto;
import org.sonar.db.user.UserDto;
import org.sonar.db.user.UserPermissionDto;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.UnauthorizedException;
import org.sonar.server.i18n.I18nRule;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.usergroups.ws.UserGroupFinder;
import org.sonar.server.ws.TestResponse;
import org.sonar.server.ws.WsActionTester;
import org.sonarqube.ws.MediaTypes;
import org.sonarqube.ws.WsPermissions.SearchProjectPermissionsWsResponse;

import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.api.server.ws.WebService.Param.PAGE;
import static org.sonar.api.server.ws.WebService.Param.PAGE_SIZE;
import static org.sonar.api.server.ws.WebService.Param.TEXT_QUERY;
import static org.sonar.db.component.ComponentTesting.newDeveloper;
import static org.sonar.db.component.ComponentTesting.newProjectCopy;
import static org.sonar.db.component.ComponentTesting.newProjectDto;
import static org.sonar.db.component.ComponentTesting.newView;
import static org.sonar.db.user.GroupTesting.newGroupDto;
import static org.sonar.db.user.UserTesting.newUserDto;
import static org.sonar.test.JsonAssert.assertJson;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PROJECT_ID;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_QUALIFIER;


public class SearchProjectPermissionsActionTest {
  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();
  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);
  ComponentDbTester componentDb = new ComponentDbTester(db);

  WsActionTester ws;
  I18nRule i18n = new I18nRule();
  DbClient dbClient = db.getDbClient();
  final DbSession dbSession = db.getSession();
  ResourceTypesRule resourceTypes = new ResourceTypesRule();
  SearchProjectPermissionsDataLoader dataLoader;

  SearchProjectPermissionsAction underTest;

  @Before
  public void setUp() {
    resourceTypes.setRootQualifiers(Qualifiers.PROJECT, Qualifiers.VIEW, "DEV");
    ComponentFinder componentFinder = new ComponentFinder(dbClient);
    PermissionDependenciesFinder finder = new PermissionDependenciesFinder(dbClient, componentFinder, new UserGroupFinder(dbClient), resourceTypes);
    i18n.setProjectPermissions();

    dataLoader = new SearchProjectPermissionsDataLoader(dbClient, finder, resourceTypes);
    underTest = new SearchProjectPermissionsAction(dbClient, userSession, i18n, resourceTypes, dataLoader);

    ws = new WsActionTester(underTest);

    userSession.login().setGlobalPermissions(GlobalPermissions.SYSTEM_ADMIN);
  }

  @Test
  public void search_project_permissions() {
    UserDto user1 = insertUser(newUserDto());
    UserDto user2 = insertUser(newUserDto());
    UserDto user3 = insertUser(newUserDto());

    ComponentDto jdk7 = insertJdk7();
    ComponentDto project2 = insertClang();
    ComponentDto dev = insertDeveloper();
    ComponentDto view = insertView();
    insertProjectInView(jdk7, view);

    insertUserRole(UserRole.ISSUE_ADMIN, user1.getId(), jdk7.getId());
    insertUserRole(UserRole.ADMIN, user1.getId(), jdk7.getId());
    insertUserRole(UserRole.ADMIN, user2.getId(), jdk7.getId());
    insertUserRole(UserRole.ADMIN, user3.getId(), jdk7.getId());
    insertUserRole(UserRole.ISSUE_ADMIN, user1.getId(), project2.getId());
    insertUserRole(UserRole.ISSUE_ADMIN, user1.getId(), dev.getId());
    insertUserRole(UserRole.ISSUE_ADMIN, user1.getId(), view.getId());
    // global permission
    insertUserRole(GlobalPermissions.SYSTEM_ADMIN, user1.getId(), null);

    GroupDto group1 = insertGroup(newGroupDto());
    GroupDto group2 = insertGroup(newGroupDto());
    GroupDto group3 = insertGroup(newGroupDto());

    insertGroupRole(UserRole.ADMIN, jdk7.getId(), null);
    insertGroupRole(UserRole.ADMIN, jdk7.getId(), group1.getId());
    insertGroupRole(UserRole.ADMIN, jdk7.getId(), group2.getId());
    insertGroupRole(UserRole.ADMIN, jdk7.getId(), group3.getId());
    insertGroupRole(UserRole.ADMIN, dev.getId(), group2.getId());
    insertGroupRole(UserRole.ADMIN, view.getId(), group2.getId());

    commit();

    String result = ws.newRequest().execute().getInput();

    assertJson(result)
      .ignoreFields("permissions")
      .isSimilarTo(getClass().getResource("search_project_permissions-example.json"));
  }

  @Test
  public void empty_result() {
    String result = ws.newRequest().execute().getInput();

    assertJson(result)
      .ignoreFields("permissions")
      .isSimilarTo(getClass().getResource("SearchProjectPermissionsActionTest/empty.json"));
  }

  @Test
  public void search_project_permissions_with_project_permission() {
    userSession.login().addProjectUuidPermissions(UserRole.ADMIN, "project-uuid");
    insertComponent(newProjectDto("project-uuid"));
    commit();

    String result = ws.newRequest()
      .setParam(PARAM_PROJECT_ID, "project-uuid")
      .execute().getInput();

    assertThat(result).contains("project-uuid");
  }

  @Test
  public void has_projects_ordered_by_name() {
    for (int i = 9; i >= 1; i--) {
      insertComponent(newProjectDto()
        .setName("project-name-" + i));
    }
    commit();

    String result = ws.newRequest()
      .setParam(PAGE, "1")
      .setParam(PAGE_SIZE, "3")
      .execute().getInput();

    assertThat(result)
      .contains("project-name-1", "project-name-2", "project-name-3")
      .doesNotContain("project-name-4");
  }

  @Test
  public void search_by_query_on_name() {
    componentDb.insertProjectAndSnapshot(newProjectDto().setName("project-name"));
    componentDb.insertProjectAndSnapshot(newProjectDto().setName("another-name"));
    componentDb.indexAllComponents();

    String result = ws.newRequest()
      .setParam(TEXT_QUERY, "project")
      .execute().getInput();

    assertThat(result).contains("project-name")
      .doesNotContain("another-name");
  }

  @Test
  public void search_by_query_on_key_must_match_exactly() {
    componentDb.insertProjectAndSnapshot(newProjectDto().setKey("project-key"));
    componentDb.insertProjectAndSnapshot(newProjectDto().setKey("another-key"));
    componentDb.indexAllComponents();

    String result = ws.newRequest()
      .setParam(TEXT_QUERY, "project-key")
      .execute().getInput();

    assertThat(result).contains("project-key")
      .doesNotContain("another-key");
  }

  @Test
  public void handle_more_than_1000_projects() {
    for (int i = 1; i <= 1001; i++) {
      componentDb.insertProjectAndSnapshot(newProjectDto("project-uuid-" + i));
    }
    componentDb.indexAllComponents();

    String result = ws.newRequest()
      .setParam(TEXT_QUERY, "project")
      .setParam(PAGE_SIZE, "1001")
      .execute().getInput();

    assertThat(result).contains("project-uuid-1", "project-uuid-999", "project-uuid-1001");
  }

  @Test
  public void result_depends_of_root_types() {
    resourceTypes.setRootQualifiers(Qualifiers.PROJECT);
    insertComponent(newView("view-uuid"));
    insertComponent(newDeveloper("developer-name"));
    insertComponent(newProjectDto("project-uuid"));
    commit();
    dataLoader = new SearchProjectPermissionsDataLoader(dbClient,
      new PermissionDependenciesFinder(dbClient, new ComponentFinder(dbClient), new UserGroupFinder(dbClient), resourceTypes),
      resourceTypes);
    underTest = new SearchProjectPermissionsAction(dbClient, userSession, i18n, resourceTypes, dataLoader);
    ws = new WsActionTester(underTest);

    String result = ws.newRequest().execute().getInput();

    assertThat(result).contains("project-uuid")
      .doesNotContain("view-uuid")
      .doesNotContain("developer-name");
  }

  @Test
  public void filter_by_qualifier() throws IOException {
    insertComponent(newView("view-uuid"));
    insertComponent(newDeveloper("developer-name"));
    insertComponent(newProjectDto("project-uuid"));
    commit();

    TestResponse wsResponse = ws.newRequest()
      .setMediaType(MediaTypes.PROTOBUF)
      .setParam(PARAM_QUALIFIER, Qualifiers.PROJECT)
      .execute();
    SearchProjectPermissionsWsResponse result = SearchProjectPermissionsWsResponse.parseFrom(wsResponse.getInputStream());

    assertThat(result.getProjectsList())
      .extracting("id")
      .contains("project-uuid")
      .doesNotContain("view-uuid")
      .doesNotContain("developer-name");
  }

  @Test
  public void fail_if_not_logged_in() {
    expectedException.expect(UnauthorizedException.class);
    userSession.anonymous();

    ws.newRequest().execute();
  }

  @Test
  public void fail_if_not_admin() {
    expectedException.expect(ForbiddenException.class);
    userSession.login();

    ws.newRequest().execute();
  }

  @Test
  public void display_all_project_permissions() {
    String result = ws.newRequest().execute().getInput();

    assertJson(result)
      .ignoreFields("permissions")
      .isSimilarTo(getClass().getResource("SearchProjectPermissionsActionTest/display_all_project_permissions.json"));
  }

  private ComponentDto insertView() {
    return insertComponent(newView()
      .setUuid("752d8bfd-420c-4a83-a4e5-8ab19b13c8fc")
      .setName("Java")
      .setKey("Java"));
  }

  private ComponentDto insertProjectInView(ComponentDto project, ComponentDto view) {
    return insertComponent(newProjectCopy("project-in-view-uuid", project, view));
  }

  private ComponentDto insertDeveloper() {
    return insertComponent(newDeveloper("Simon Brandhof")
      .setUuid("4e607bf9-7ed0-484a-946d-d58ba7dab2fb")
      .setKey("simon-brandhof"));
  }

  private ComponentDto insertClang() {
    return insertComponent(newProjectDto("project-uuid-2")
      .setName("Clang")
      .setKey("clang")
      .setUuid("ce4c03d6-430f-40a9-b777-ad877c00aa4d"));
  }

  private ComponentDto insertJdk7() {
    return insertComponent(newProjectDto("project-uuid-1")
      .setName("JDK 7")
      .setKey("net.java.openjdk:jdk7")
      .setUuid("0bd7b1e7-91d6-439e-a607-4a3a9aad3c6a"));
  }

  private UserDto insertUser(UserDto user) {
    return dbClient.userDao().insert(dbSession, user.setActive(true));
  }

  private void insertUserRole(String permission, long userId, @Nullable Long resourceId) {
    dbClient.roleDao().insertUserRole(dbSession, new UserPermissionDto()
      .setPermission(permission)
      .setUserId(userId)
      .setComponentId(resourceId));
  }

  private GroupDto insertGroup(GroupDto group) {
    return dbClient.groupDao().insert(dbSession, group);
  }

  private void insertGroupRole(String permission, @Nullable Long resourceId, @Nullable Long groupId) {
    dbClient.roleDao().insertGroupRole(dbSession, new GroupRoleDto().setRole(permission).setResourceId(resourceId).setGroupId(groupId));
  }

  private ComponentDto insertComponent(ComponentDto component) {
    dbClient.componentDao().insert(dbSession, component.setEnabled(true));
    return dbClient.componentDao().selectOrFailByUuid(dbSession, component.uuid());
  }

  private void commit() {
    dbSession.commit();
  }
}
