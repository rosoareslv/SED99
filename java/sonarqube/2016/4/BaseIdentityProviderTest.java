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
package it.user;

import com.google.common.base.Joiner;
import com.google.common.base.Optional;
import com.sonar.orchestrator.Orchestrator;
import com.sonar.orchestrator.selenium.Selenese;
import it.Category4Suite;
import java.io.File;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.BeforeClass;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import org.sonarqube.ws.client.GetRequest;
import org.sonarqube.ws.client.WsClient;
import org.sonarqube.ws.client.WsResponse;
import util.QaOnly;
import util.user.UserRule;
import util.user.Users;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.guava.api.Assertions.assertThat;
import static util.ItUtils.newAdminWsClient;
import static util.ItUtils.setServerProperty;

/**
 * TODO : Add missing ITs
 * - creating new user using email already used
 * - display multiple identity provider plugins (probably in another class)
 */
@Category(QaOnly.class)
public class BaseIdentityProviderTest {

  @ClassRule
  public static Orchestrator ORCHESTRATOR = Category4Suite.ORCHESTRATOR;

  @ClassRule
  public static UserRule userRule = UserRule.from(ORCHESTRATOR);

  static String FAKE_PROVIDER_KEY = "fake-base-id-provider";

  static String USER_LOGIN = "john";
  static String USER_PROVIDER_ID = "fake-john";
  static String USER_NAME = "John";
  static String USER_EMAIL = "john@email.com";

  static String USER_NAME_UPDATED = "John Doe";
  static String USER_EMAIL_UPDATED = "john.doe@email.com";

  static String GROUP1 = "group1";
  static String GROUP2 = "group2";
  static String GROUP3 = "group3";

  static WsClient adminWsClient;

  @BeforeClass
  public static void setUp() {
    ORCHESTRATOR.resetData();
    adminWsClient = newAdminWsClient(ORCHESTRATOR);
  }

  @After
  public void cleanUpUsersAndGroupsAndProperties() throws Exception {
    userRule.deactivateUsers(USER_LOGIN);
    userRule.removeGroups(GROUP1, GROUP2, GROUP3);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.enabled", null);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.user", null);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.throwUnauthorizedMessage", null);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.enabledGroupsSync", null);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.groups", null);
  }

  @Test
  public void create_new_user_when_authenticate() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);

    userRule.verifyUserDoesNotExist(USER_LOGIN);

    // First connection, user is created
    authenticateWithFakeAuthProvider();

    userRule.verifyUserExists(USER_LOGIN, USER_NAME, USER_EMAIL, false);
  }

  @Test
  public void authenticate_user() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);

    ORCHESTRATOR.executeSelenese(Selenese.builder().setHtmlTestsInClasspath("authenticate_through_ui",
      "/user/BaseIdentityProviderTest/authenticate_user.html"
      ).build());

    userRule.verifyUserExists(USER_LOGIN, USER_NAME, USER_EMAIL);
  }

  @Test
  public void display_unauthorized_page_when_authentication_failed() throws Exception {
    enablePlugin();
    // As this property is null, the plugin will throw an exception
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.user", null);

    ORCHESTRATOR.executeSelenese(Selenese.builder().setHtmlTestsInClasspath("display_unauthorized_page_when_authentication_failed",
      "/user/BaseIdentityProviderTest/display_unauthorized_page_when_authentication_failed.html"
      ).build());

    userRule.verifyUserDoesNotExist(USER_LOGIN);
  }

  @Test
  public void fail_to_authenticate_when_not_allowed_to_sign_up() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.allowsUsersToSignUp", "false");

    ORCHESTRATOR.executeSelenese(Selenese.builder().setHtmlTestsInClasspath("fail_to_authenticate_when_not_allowed_to_sign_up",
      "/user/BaseIdentityProviderTest/fail_to_authenticate_when_not_allowed_to_sign_up.html"
      ).build());

    userRule.verifyUserDoesNotExist(USER_LOGIN);
  }

  @Test
  public void update_existing_user_when_authenticate() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);

    // First connection, user is created
    authenticateWithFakeAuthProvider();

    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME_UPDATED, USER_EMAIL_UPDATED);

    // Second connection, user should be updated
    authenticateWithFakeAuthProvider();

    userRule.verifyUserExists(USER_LOGIN, USER_NAME_UPDATED, USER_EMAIL_UPDATED);
  }

  @Test
  public void reactivate_disabled_user() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);

    userRule.verifyUserDoesNotExist(USER_LOGIN);

    // First connection, user is created
    authenticateWithFakeAuthProvider();

    Optional<Users.User> user = userRule.getUserByLogin(USER_LOGIN);
    assertThat(user).isPresent();

    // Disable user
    userRule.deactivateUsers(USER_LOGIN);

    // Second connection, user is reactivated
    authenticateWithFakeAuthProvider();
    userRule.verifyUserExists(USER_LOGIN, USER_NAME, USER_EMAIL);
  }

  @Test
  public void not_authenticate_when_plugin_is_disabled() throws Exception {
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.enabled", "false");
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);

    authenticateWithFakeAuthProvider();

    // User is not created as nothing plugin is disabled
    userRule.verifyUserDoesNotExist(USER_LOGIN);

    // TODO Add Selenium test to check login form
  }

  @Test
  public void display_message_in_ui_but_not_in_log_when_unauthorized_exception() throws Exception {
    enablePlugin();
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.throwUnauthorizedMessage", "true");

    ORCHESTRATOR.executeSelenese(Selenese.builder().setHtmlTestsInClasspath("fail_to_authenticate_when_not_allowed_to_sign_up",
      "/user/BaseIdentityProviderTest/fail_to_authenticate_when_not_allowed_to_sign_up.html"
    ).build());

    File logFile = ORCHESTRATOR.getServer().getLogs();
    assertThat(FileUtils.readFileToString(logFile)).doesNotContain("A functional error has happened");
    assertThat(FileUtils.readFileToString(logFile)).doesNotContain("UnauthorizedException");

    userRule.verifyUserDoesNotExist(USER_LOGIN);
  }

  @Test
  public void synchronize_groups_for_new_user() throws Exception {
    enablePlugin();
    userRule.createGroup(GROUP1);
    userRule.createGroup(GROUP2);
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);
    // Group3 doesn't exist in DB, user won't belong to this group
    setGroupsReturnedByAuthPlugin(GROUP1, GROUP2, GROUP3);

    authenticateWithFakeAuthProvider();

    userRule.verifyUserGroupMembership(USER_LOGIN, GROUP1, GROUP2);
  }

  @Test
  public void synchronize_groups_for_existing_user() throws Exception {
    enablePlugin();
    userRule.createGroup(GROUP1);
    userRule.createGroup(GROUP2);
    userRule.createGroup(GROUP3);
    userRule.createUser(USER_LOGIN, "password");
    userRule.associateGroupsToUser(USER_LOGIN, GROUP1, GROUP2);
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);
    // Group1 is not returned by the plugin, user won't belong anymore to this group
    setGroupsReturnedByAuthPlugin(GROUP2, GROUP3);

    authenticateWithFakeAuthProvider();

    userRule.verifyUserGroupMembership(USER_LOGIN, GROUP2, GROUP3);
  }

  @Test
  public void remove_user_groups_when_groups_provided_by_plugin_are_empty() throws Exception {
    enablePlugin();
    userRule.createGroup(GROUP1);
    userRule.createUser(USER_LOGIN, "password");
    userRule.associateGroupsToUser(USER_LOGIN, GROUP1);
    setUserCreatedByAuthPlugin(USER_LOGIN, USER_PROVIDER_ID, USER_NAME, USER_EMAIL);
    // No group is returned by the plugin
    setGroupsReturnedByAuthPlugin();

    authenticateWithFakeAuthProvider();

    // User is not member to any group
    userRule.verifyUserGroupMembership(USER_LOGIN);
  }

  private static void enablePlugin() {
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.enabled", "true");
  }

  private static void setUserCreatedByAuthPlugin(String login, String providerId, String name, String email) {
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.user", login + "," + providerId + "," + name + "," + email);
  }

  private static void setGroupsReturnedByAuthPlugin(String... groups) {
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.enabledGroupsSync", "true");
    setServerProperty(ORCHESTRATOR, "sonar.auth.fake-base-id-provider.groups", Joiner.on(",").join(groups));
  }

  private static void authenticateWithFakeAuthProvider() {
    WsResponse response = adminWsClient.wsConnector().call(
      new GetRequest(("/sessions/init/" + FAKE_PROVIDER_KEY)));
    assertThat(response.code()).isEqualTo(200);
  }

}
