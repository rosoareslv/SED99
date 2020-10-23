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

import java.io.InputStream;
import javax.annotation.Nullable;
import org.junit.Test;
import org.sonar.api.server.ws.WebService;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.permission.template.PermissionTemplateDto;
import org.sonar.db.permission.template.PermissionTemplateUserDto;
import org.sonar.db.user.UserDto;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.NotFoundException;
import org.sonar.server.exceptions.UnauthorizedException;
import org.sonar.server.permission.ws.BasePermissionWsTest;
import org.sonar.server.ws.TestRequest;
import org.sonarqube.ws.WsPermissions;

import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.api.web.UserRole.ADMIN;
import static org.sonar.api.web.UserRole.CODEVIEWER;
import static org.sonar.api.web.UserRole.ISSUE_ADMIN;
import static org.sonar.api.web.UserRole.USER;
import static org.sonar.core.permission.GlobalPermissions.SCAN_EXECUTION;
import static org.sonar.db.permission.template.PermissionTemplateTesting.newPermissionTemplateUserDto;
import static org.sonar.db.user.UserTesting.newUserDto;
import static org.sonar.test.JsonAssert.assertJson;
import static org.sonarqube.ws.MediaTypes.PROTOBUF;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_PERMISSION;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_TEMPLATE_ID;
import static org.sonarqube.ws.client.permission.PermissionsWsParameters.PARAM_TEMPLATE_NAME;

public class TemplateUsersActionTest extends BasePermissionWsTest<TemplateUsersAction> {

  @Override
  protected TemplateUsersAction buildWsAction() {
    return new TemplateUsersAction(db.getDbClient(), userSession, newPermissionWsSupport());
  }

  @Test
  public void search_for_users_with_response_example() throws Exception {
    UserDto user1 = insertUser(newUserDto().setLogin("admin").setName("Administrator").setEmail("admin@admin.com"));
    UserDto user2 = insertUser(newUserDto().setLogin("george.orwell").setName("George Orwell").setEmail("george.orwell@1984.net"));

    PermissionTemplateDto template1 = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(CODEVIEWER, template1, user1));
    addUserToTemplate(newPermissionTemplateUser(CODEVIEWER, template1, user2));
    addUserToTemplate(newPermissionTemplateUser(ADMIN, template1, user2));
    loginAsAdminOnDefaultOrganization();

    String result = newRequest(null, template1.getUuid()).execute().getInput();
    assertJson(result).isSimilarTo(getClass().getResource("template_users-example.json"));
  }

  @Test
  public void search_for_users_by_template_name() throws Exception {
    loginAsAdminOnDefaultOrganization();

    UserDto user1 = insertUser(newUserDto().setLogin("login-1").setName("name-1").setEmail("email-1"));
    UserDto user2 = insertUser(newUserDto().setLogin("login-2").setName("name-2").setEmail("email-2"));
    UserDto user3 = insertUser(newUserDto().setLogin("login-3").setName("name-3").setEmail("email-3"));

    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, template, user1));
    addUserToTemplate(newPermissionTemplateUser(USER, template, user2));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user1));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user3));

    PermissionTemplateDto anotherTemplate = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, anotherTemplate, user1));

    InputStream bytes = newRequest(null, null)
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .setMediaType(PROTOBUF)
      .execute().getInputStream();

    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).extracting("login").containsExactly("login-1", "login-2", "login-3");
    assertThat(response.getUsers(0).getPermissionsList()).containsOnly("issueadmin", "user");
    assertThat(response.getUsers(1).getPermissionsList()).containsOnly("user");
    assertThat(response.getUsers(2).getPermissionsList()).containsOnly("issueadmin");
  }

  @Test
  public void search_using_text_query() throws Exception {
    loginAsAdminOnDefaultOrganization();

    UserDto user1 = insertUser(newUserDto().setLogin("login-1").setName("name-1").setEmail("email-1"));
    UserDto user2 = insertUser(newUserDto().setLogin("login-2").setName("name-2").setEmail("email-2"));
    UserDto user3 = insertUser(newUserDto().setLogin("login-3").setName("name-3").setEmail("email-3"));

    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, template, user1));
    addUserToTemplate(newPermissionTemplateUser(USER, template, user2));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user1));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user3));

    PermissionTemplateDto anotherTemplate = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, anotherTemplate, user1));

    InputStream bytes = newRequest(null, null)
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .setParam(WebService.Param.TEXT_QUERY, "ame-1")
      .setMediaType(PROTOBUF)
      .execute().getInputStream();

    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).extracting("login").containsOnly("login-1");
  }

  @Test
  public void search_using_permission() throws Exception {
    UserDto user1 = insertUser(newUserDto().setLogin("login-1").setName("name-1").setEmail("email-1"));
    UserDto user2 = insertUser(newUserDto().setLogin("login-2").setName("name-2").setEmail("email-2"));
    UserDto user3 = insertUser(newUserDto().setLogin("login-3").setName("name-3").setEmail("email-3"));

    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, template, user1));
    addUserToTemplate(newPermissionTemplateUser(USER, template, user2));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user1));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user3));

    PermissionTemplateDto anotherTemplate = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, anotherTemplate, user1));

    loginAsAdminOnDefaultOrganization();
    InputStream bytes = newRequest(USER, template.getUuid())
      .setMediaType(PROTOBUF)
      .execute().getInputStream();
    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).extracting("login").containsExactly("login-1", "login-2");
    assertThat(response.getUsers(0).getPermissionsList()).containsOnly("issueadmin", "user");
    assertThat(response.getUsers(1).getPermissionsList()).containsOnly("user");
  }

  @Test
  public void search_with_pagination() throws Exception {
    UserDto user1 = insertUser(newUserDto().setLogin("login-1").setName("name-1").setEmail("email-1"));
    UserDto user2 = insertUser(newUserDto().setLogin("login-2").setName("name-2").setEmail("email-2"));
    UserDto user3 = insertUser(newUserDto().setLogin("login-3").setName("name-3").setEmail("email-3"));

    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, template, user1));
    addUserToTemplate(newPermissionTemplateUser(USER, template, user2));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user1));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user3));

    PermissionTemplateDto anotherTemplate = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, anotherTemplate, user1));

    loginAsAdminOnDefaultOrganization();
    InputStream bytes = newRequest(USER, null)
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .setParam(WebService.Param.SELECTED, "all")
      .setParam(WebService.Param.PAGE, "2")
      .setParam(WebService.Param.PAGE_SIZE, "1")
      .setMediaType(PROTOBUF)
      .execute().getInputStream();

    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).extracting("login").containsOnly("login-2");
  }

  @Test
  public void users_are_sorted_by_name() throws Exception {
    UserDto user1 = insertUser(newUserDto().setLogin("login-2").setName("name-2"));
    UserDto user2 = insertUser(newUserDto().setLogin("login-3").setName("name-3"));
    UserDto user3 = insertUser(newUserDto().setLogin("login-1").setName("name-1"));

    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, template, user1));
    addUserToTemplate(newPermissionTemplateUser(USER, template, user2));
    addUserToTemplate(newPermissionTemplateUser(ISSUE_ADMIN, template, user3));

    loginAsAdminOnDefaultOrganization();
    InputStream bytes = newRequest(null, null)
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .setMediaType(PROTOBUF)
      .execute().getInputStream();

    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).extracting("login").containsExactly("login-1", "login-2", "login-3");
  }

  @Test
  public void empty_result_when_no_user_on_template() throws Exception {
    UserDto user = insertUser(newUserDto().setLogin("login-1").setName("name-1").setEmail("email-1"));
    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    PermissionTemplateDto anotherTemplate = addTemplateToDefaultOrganization();
    addUserToTemplate(newPermissionTemplateUser(USER, anotherTemplate, user));

    loginAsAdminOnDefaultOrganization();
    InputStream bytes = newRequest(null, null)
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .setMediaType(PROTOBUF)
      .execute()
      .getInputStream();

    WsPermissions.UsersWsResponse response = WsPermissions.UsersWsResponse.parseFrom(bytes);
    assertThat(response.getUsersList()).isEmpty();
  }

  @Test
  public void fail_if_not_a_project_permission() throws Exception {
    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(IllegalArgumentException.class);

    newRequest(GlobalPermissions.PROVISIONING, template.getUuid())
      .execute();
  }

  @Test
  public void fail_if_no_template_param() throws Exception {
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(BadRequestException.class);

    newRequest(null, null)
      .execute();
  }

  @Test
  public void fail_if_template_does_not_exist() throws Exception {
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(NotFoundException.class);

    newRequest(null, "unknown-template-uuid")
      .execute();
  }

  @Test
  public void fail_if_template_uuid_and_name_provided() throws Exception {
    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(BadRequestException.class);

    newRequest(null, template.getUuid())
      .setParam(PARAM_TEMPLATE_NAME, template.getName())
      .execute();
  }

  @Test
  public void fail_if_not_logged_in() throws Exception {
    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    userSession.anonymous();

    expectedException.expect(UnauthorizedException.class);

    newRequest(null, template.getUuid()).execute();
  }

  @Test
  public void fail_if_insufficient_privileges() throws Exception {
    PermissionTemplateDto template = addTemplateToDefaultOrganization();
    userSession.login().addOrganizationPermission(db.getDefaultOrganization().getUuid(), SCAN_EXECUTION);

    expectedException.expect(ForbiddenException.class);

    newRequest(null, template.getUuid()).execute();
  }

  private UserDto insertUser(UserDto userDto) {
    return db.users().insertUser(userDto);
  }

  private void addUserToTemplate(PermissionTemplateUserDto dto) {
    db.getDbClient().permissionTemplateDao().insertUserPermission(db.getSession(), dto.getTemplateId(), dto.getUserId(), dto.getPermission());
    db.commit();
  }

  private static PermissionTemplateUserDto newPermissionTemplateUser(String permission, PermissionTemplateDto template, UserDto user) {
    return newPermissionTemplateUserDto()
      .setPermission(permission)
      .setTemplateId(template.getId())
      .setUserId(user.getId());
  }

  private TestRequest newRequest(@Nullable String permission, @Nullable String templateUuid) {
    TestRequest request = newRequest();
    if (permission != null) {
      request.setParam(PARAM_PERMISSION, permission);
    }
    if (templateUuid != null) {
      request.setParam(PARAM_TEMPLATE_ID, templateUuid);
    }
    return request;
  }

}
