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

import com.google.common.base.Function;
import java.util.List;
import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.utils.System2;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ResourceTypesRule;
import org.sonar.db.permission.OldPermissionQuery;
import org.sonar.db.permission.template.PermissionTemplateDto;
import org.sonar.db.permission.UserWithPermissionDto;
import org.sonar.db.user.UserDto;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.NotFoundException;
import org.sonar.server.exceptions.UnauthorizedException;
import org.sonar.server.permission.ws.PermissionDependenciesFinder;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.usergroups.ws.UserGroupFinder;
import org.sonar.server.ws.TestRequest;
import org.sonar.server.ws.WsActionTester;

import static com.google.common.collect.FluentIterable.from;
import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.api.web.UserRole.CODEVIEWER;
import static org.sonar.api.web.UserRole.ISSUE_ADMIN;
import static org.sonar.db.permission.template.PermissionTemplateTesting.newPermissionTemplateDto;
import static org.sonar.db.user.GroupMembershipQuery.IN;
import static org.sonar.db.user.UserTesting.newUserDto;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PERMISSION;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_TEMPLATE_NAME;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_USER_LOGIN;


public class RemoveUserFromTemplateActionTest {

  private static final String USER_LOGIN = "user-login";
  private static final String DEFAULT_PERMISSION = CODEVIEWER;
  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);
  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();
  ResourceTypesRule resourceTypes = new ResourceTypesRule().setRootQualifiers(Qualifiers.PROJECT, Qualifiers.VIEW, "DEV");

  WsActionTester ws;
  DbClient dbClient;
  DbSession dbSession;
  UserDto user;
  PermissionTemplateDto permissionTemplate;

  @Before
  public void setUp() {
    dbClient = db.getDbClient();
    dbSession = db.getSession();
    userSession.login().setGlobalPermissions(GlobalPermissions.SYSTEM_ADMIN);

    PermissionDependenciesFinder dependenciesFinder = new PermissionDependenciesFinder(dbClient, new ComponentFinder(dbClient), new UserGroupFinder(dbClient), resourceTypes);
    ws = new WsActionTester(new RemoveUserFromTemplateAction(dbClient, dependenciesFinder, userSession));

    user = insertUser(newUserDto().setLogin(USER_LOGIN));
    permissionTemplate = insertPermissionTemplate(newPermissionTemplateDto());
    addUserToTemplate(user, permissionTemplate, DEFAULT_PERMISSION);
    commit();
  }

  @Test
  public void remove_user_from_template() {
    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);

    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), DEFAULT_PERMISSION)).isEmpty();
  }

  @Test
  public void remove_user_from_template_by_name_case_insensitive() {
    ws.newRequest()
      .setParam(PARAM_USER_LOGIN, USER_LOGIN)
      .setParam(PARAM_PERMISSION, DEFAULT_PERMISSION)
      .setParam(PARAM_TEMPLATE_NAME, permissionTemplate.getName().toUpperCase())
      .execute();
    commit();

    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), DEFAULT_PERMISSION)).isEmpty();
  }

  @Test
  public void remove_user_from_template_twice_without_failing() {
    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);
    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);

    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), DEFAULT_PERMISSION)).isEmpty();
  }

  @Test
  public void keep_user_permission_not_removed() {
    addUserToTemplate(user, permissionTemplate, ISSUE_ADMIN);
    commit();

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);

    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), DEFAULT_PERMISSION)).isEmpty();
    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), ISSUE_ADMIN)).containsExactly(user.getLogin());
  }

  @Test
  public void keep_other_users_when_one_user_removed() {
    UserDto newUser = insertUser(newUserDto().setLogin("new-login"));
    addUserToTemplate(newUser, permissionTemplate, DEFAULT_PERMISSION);
    commit();

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);

    assertThat(getLoginsInTemplateAndPermission(permissionTemplate.getId(), DEFAULT_PERMISSION)).containsExactly("new-login");
  }

  @Test
  public void fail_if_not_a_project_permission() {
    expectedException.expect(BadRequestException.class);

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), GlobalPermissions.PROVISIONING);
  }

  @Test
  public void fail_if_insufficient_privileges() {
    expectedException.expect(ForbiddenException.class);
    userSession.setGlobalPermissions(GlobalPermissions.QUALITY_PROFILE_ADMIN);

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);
  }

  @Test
  public void fail_if_not_logged_in() {
    expectedException.expect(UnauthorizedException.class);
    userSession.anonymous();

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), DEFAULT_PERMISSION);
  }

  @Test
  public void fail_if_user_missing() {
    expectedException.expect(IllegalArgumentException.class);

    newRequest(null, permissionTemplate.getUuid(), DEFAULT_PERMISSION);
  }

  @Test
  public void fail_if_permission_missing() {
    expectedException.expect(IllegalArgumentException.class);

    newRequest(USER_LOGIN, permissionTemplate.getUuid(), null);
  }

  @Test
  public void fail_if_template_missing() {
    expectedException.expect(BadRequestException.class);

    newRequest(USER_LOGIN, null, DEFAULT_PERMISSION);
  }

  @Test
  public void fail_if_user_does_not_exist() {
    expectedException.expect(NotFoundException.class);
    expectedException.expectMessage("User with login 'unknown-login' is not found");

    newRequest("unknown-login", permissionTemplate.getUuid(), DEFAULT_PERMISSION);
  }

  @Test
  public void fail_if_template_key_does_not_exist() {
    expectedException.expect(NotFoundException.class);
    expectedException.expectMessage("Permission template with id 'unknown-key' is not found");

    newRequest(USER_LOGIN, "unknown-key", DEFAULT_PERMISSION);
  }

  private void newRequest(@Nullable String userLogin, @Nullable String templateKey, @Nullable String permission) {
    TestRequest request = ws.newRequest();
    if (userLogin != null) {
      request.setParam(PARAM_USER_LOGIN, userLogin);
    }
    if (templateKey != null) {
      request.setParam(org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_TEMPLATE_ID, templateKey);
    }
    if (permission != null) {
      request.setParam(PARAM_PERMISSION, permission);
    }

    request.execute();
  }

  private void commit() {
    dbSession.commit();
  }

  private UserDto insertUser(UserDto userDto) {
    return dbClient.userDao().insert(dbSession, userDto.setActive(true));
  }

  private PermissionTemplateDto insertPermissionTemplate(PermissionTemplateDto permissionTemplate) {
    return dbClient.permissionTemplateDao().insert(dbSession, permissionTemplate);
  }

  private List<String> getLoginsInTemplateAndPermission(long templateId, String permission) {
    OldPermissionQuery permissionQuery = OldPermissionQuery.builder().permission(permission).membership(IN).build();
    return from(dbClient.permissionTemplateDao()
      .selectUsers(dbSession, permissionQuery, templateId, 0, Integer.MAX_VALUE))
        .transform(UserWithPermissionToUserLogin.INSTANCE)
        .toList();
  }

  private enum UserWithPermissionToUserLogin implements Function<UserWithPermissionDto, String> {
    INSTANCE;

    @Override
    public String apply(@Nonnull UserWithPermissionDto userWithPermission) {
      return userWithPermission.getLogin();
    }

  }

  private void addUserToTemplate(UserDto user, PermissionTemplateDto permissionTemplate, String permission) {
    dbClient.permissionTemplateDao().insertUserPermission(dbSession, permissionTemplate.getId(), user.getId(), permission);
  }
}
