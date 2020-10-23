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
package org.sonar.server.usergroups.ws;

import java.util.List;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.utils.internal.AlwaysIncreasingSystem2;
import org.sonar.db.DbTester;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.db.user.GroupDto;
import org.sonar.db.user.UserDto;
import org.sonar.db.user.UserMembershipDto;
import org.sonar.db.user.UserMembershipQuery;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.NotFoundException;
import org.sonar.server.exceptions.UnauthorizedException;
import org.sonar.server.organization.TestDefaultOrganizationProvider;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.ws.WsTester;

import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.core.permission.GlobalPermissions.SYSTEM_ADMIN;
import static org.sonar.server.usergroups.ws.GroupWsSupport.PARAM_GROUP_NAME;
import static org.sonar.server.usergroups.ws.GroupWsSupport.PARAM_LOGIN;
import static org.sonar.server.usergroups.ws.GroupWsSupport.PARAM_ORGANIZATION_KEY;

public class AddUserActionTest {

  @Rule
  public DbTester db = DbTester.create(new AlwaysIncreasingSystem2());
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();
  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  private TestDefaultOrganizationProvider defaultOrganizationProvider = TestDefaultOrganizationProvider.from(db);
  private WsTester ws;

  @Before
  public void setUp() {
    ws = new WsTester(new UserGroupsWs(new AddUserAction(db.getDbClient(), userSession, newGroupWsSupport(), defaultOrganizationProvider)));
  }

  @Test
  public void add_user_to_group_referenced_by_its_id() throws Exception {
    GroupDto group = db.users().insertGroup();
    UserDto user = db.users().insertUser();
    loginAsAdminOnDefaultOrganization();

    newRequest()
      .setParam("id", group.getId().toString())
      .setParam("login", user.getLogin())
      .execute()
      .assertNoContent();

    assertThat(db.users().selectGroupIdsOfUser(user)).containsOnly(group.getId());
  }

  @Test
  public void add_user_to_group_referenced_by_its_name() throws Exception {
    GroupDto group = db.users().insertGroup();
    UserDto user = db.users().insertUser();
    loginAsAdminOnDefaultOrganization();

    newRequest()
      .setParam(PARAM_GROUP_NAME, group.getName())
      .setParam(PARAM_LOGIN, user.getLogin())
      .execute()
      .assertNoContent();

    assertThat(db.users().selectGroupIdsOfUser(user)).containsOnly(group.getId());
  }

  @Test
  public void add_user_to_group_referenced_by_its_name_and_organization() throws Exception {
    OrganizationDto org = db.organizations().insert();
    GroupDto group = db.users().insertGroup(org, "a-group");
    UserDto user = db.users().insertUser("user_login");
    loginAsAdmin(org);

    newRequest()
      .setParam(PARAM_ORGANIZATION_KEY, org.getKey())
      .setParam(PARAM_GROUP_NAME, group.getName())
      .setParam(PARAM_LOGIN, user.getLogin())
      .execute()
      .assertNoContent();

    assertThat(db.users().selectGroupIdsOfUser(user)).containsOnly(group.getId());
  }

  @Test
  public void add_user_to_another_group() throws Exception {
    OrganizationDto defaultOrg = db.getDefaultOrganization();
    GroupDto admins = db.users().insertGroup(defaultOrg, "admins");
    GroupDto users = db.users().insertGroup(defaultOrg, "users");
    UserDto user = db.users().insertUser("my-admin");
    db.users().insertMember(users, user);
    loginAsAdminOnDefaultOrganization();

    newRequest()
      .setParam("id", admins.getId().toString())
      .setParam("login", user.getLogin())
      .execute()
      .assertNoContent();

    assertThat(db.users().selectGroupIdsOfUser(user)).containsOnly(admins.getId(), users.getId());
  }

  @Test
  public void do_not_fail_if_user_is_already_member_of_group() throws Exception {
    GroupDto users = db.users().insertGroup();
    UserDto user = db.users().insertUser();
    db.users().insertMember(users, user);
    loginAsAdminOnDefaultOrganization();

    newRequest()
      .setParam("id", users.getId().toString())
      .setParam("login", user.getLogin())
      .execute()
      .assertNoContent();

    // do not insert duplicated row
    assertThat(db.users().selectGroupIdsOfUser(user)).hasSize(1).containsOnly(users.getId());
  }

  @Test
  public void group_has_multiple_members() throws Exception {
    GroupDto users = db.users().insertGroup();
    UserDto user1 = db.users().insertUser();
    UserDto user2 = db.users().insertUser();
    db.users().insertMember(users, user1);
    loginAsAdminOnDefaultOrganization();

    newRequest()
      .setParam("id", users.getId().toString())
      .setParam("login", user2.getLogin())
      .execute()
      .assertNoContent();

    assertThat(db.users().selectGroupIdsOfUser(user1)).containsOnly(users.getId());
    assertThat(db.users().selectGroupIdsOfUser(user2)).containsOnly(users.getId());
  }

  @Test
  public void fail_if_group_does_not_exist() throws Exception {
    UserDto user = db.users().insertUser();
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(NotFoundException.class);

    newRequest()
      .setParam("id", "42")
      .setParam("login", user.getLogin())
      .execute();
  }

  @Test
  public void fail_if_user_does_not_exist() throws Exception {
    GroupDto group = db.users().insertGroup(db.getDefaultOrganization(), "admins");
    loginAsAdminOnDefaultOrganization();

    expectedException.expect(NotFoundException.class);

    newRequest()
      .setParam("id", group.getId().toString())
      .setParam("login", "my-admin")
      .execute();
  }

  @Test
  public void fail_if_not_administrator() throws Exception {
    GroupDto group = db.users().insertGroup();
    UserDto user = db.users().insertUser();

    expectedException.expect(UnauthorizedException.class);

    executeRequest(group, user);
  }

  @Test
  public void set_root_flag_to_true_when_adding_user_to_group_of_default_organization_with_admin_permission() throws Exception {
    GroupDto group = db.users().insertAdminGroup();
    UserDto falselyRootUser = db.users().makeRoot(db.users().insertUser("falselyRootUser"));
    UserDto notRootUser = db.users().insertUser("notRootUser");
    loginAsAdminOnDefaultOrganization();

    executeRequest(group, falselyRootUser);
    verifyUserInGroup(falselyRootUser, group);
    db.rootFlag().verify(falselyRootUser, true);
    verifyUserNotInGroup(notRootUser, group);
    db.rootFlag().verifyUnchanged(notRootUser);

    executeRequest(group, notRootUser);
    verifyUserInGroup(falselyRootUser, group);
    db.rootFlag().verify(falselyRootUser, true);
    verifyUserInGroup(notRootUser, group);
    db.rootFlag().verify(notRootUser, true);
  }

  @Test
  public void does_not_set_root_flag_to_true_when_adding_user_to_group_of_other_organization_with_admin_permission() throws Exception {
    OrganizationDto otherOrganization = db.organizations().insert();
    GroupDto group = db.users().insertAdminGroup(otherOrganization);
    UserDto falselyRootUser = db.users().makeRoot(db.users().insertUser("falselyRootUser"));
    UserDto notRootUser = db.users().insertUser("notRootUser");
    loginAsAdmin(otherOrganization);

    executeRequest(group, falselyRootUser);
    verifyUserInGroup(falselyRootUser, group);
    db.rootFlag().verify(falselyRootUser, false);
    verifyUserNotInGroup(notRootUser, group);
    db.rootFlag().verifyUnchanged(notRootUser);

    executeRequest(group, notRootUser);
    verifyUserInGroup(falselyRootUser, group);
    db.rootFlag().verify(falselyRootUser, false);
    verifyUserInGroup(notRootUser, group);
    db.rootFlag().verify(notRootUser, false);
  }

  private void executeRequest(GroupDto groupDto, UserDto userDto) throws Exception {
    newRequest()
      .setParam("id", groupDto.getId().toString())
      .setParam("login", userDto.getLogin())
      .execute();
  }

  @Test
  public void fail_if_administrator_of_another_organization() throws Exception {
    OrganizationDto org1 = db.organizations().insert();
    GroupDto group = db.users().insertGroup(org1, "a-group");
    UserDto user = db.users().insertUser("user_login");
    OrganizationDto org2 = db.organizations().insert();
    loginAsAdmin(org2);

    expectedException.expect(ForbiddenException.class);

    newRequest()
      .setParam(PARAM_ORGANIZATION_KEY, org1.getKey())
      .setParam(PARAM_GROUP_NAME, group.getName())
      .setParam(PARAM_LOGIN, user.getLogin())
      .execute();
  }

  private WsTester.TestRequest newRequest() {
    return ws.newPostRequest("api/user_groups", "add_user");
  }

  private void loginAsAdminOnDefaultOrganization() {
    loginAsAdmin(db.getDefaultOrganization());
  }

  private void loginAsAdmin(OrganizationDto org) {
    userSession.login().addOrganizationPermission(org.getUuid(), SYSTEM_ADMIN);
  }

  private GroupWsSupport newGroupWsSupport() {
    return new GroupWsSupport(db.getDbClient(), defaultOrganizationProvider);
  }

  private void verifyUserInGroup(UserDto userDto, GroupDto groupDto) {
    assertThat(isUserInGroup(userDto, groupDto))
      .as("user '%s' is a member of group '%s' of organization '%s'", userDto.getLogin(), groupDto.getName(), groupDto.getOrganizationUuid())
      .isTrue();
  }

  private void verifyUserNotInGroup(UserDto userDto, GroupDto groupDto) {
    assertThat(isUserInGroup(userDto, groupDto))
      .as("user '%s' is not a member of group '%s' of organization '%s'", userDto.getLogin(), groupDto.getName(), groupDto.getOrganizationUuid())
      .isFalse();
  }

  private boolean isUserInGroup(UserDto userDto, GroupDto groupDto) {
    List<UserMembershipDto> members = db.getDbClient().groupMembershipDao()
      .selectMembers(db.getSession(), UserMembershipQuery.builder().groupId(groupDto.getId()).membership(UserMembershipQuery.IN).build(), 0, Integer.MAX_VALUE);
    return members
      .stream()
      .anyMatch(dto -> dto.getLogin().equals(userDto.getLogin()));
  }

}
