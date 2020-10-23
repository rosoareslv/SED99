/*
 * SonarQube
 * Copyright (C) 2009-2018 SonarSource SA
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
package org.sonar.server.user.ws;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.config.internal.MapSettings;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.utils.System2;
import org.sonar.core.platform.PluginRepository;
import org.sonar.db.DbClient;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.organization.OrganizationDbTester;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.db.user.UserDto;
import org.sonar.server.issue.ws.AvatarResolverImpl;
import org.sonar.server.organization.DefaultOrganizationProvider;
import org.sonar.server.organization.TestDefaultOrganizationProvider;
import org.sonar.server.organization.TestOrganizationFlags;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.ws.WsActionTester;
import org.sonarqube.ws.Users.CurrentWsResponse;

import static com.google.common.collect.Lists.newArrayList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.api.web.UserRole.USER;
import static org.sonar.db.permission.OrganizationPermission.ADMINISTER;
import static org.sonar.db.permission.OrganizationPermission.ADMINISTER_QUALITY_PROFILES;
import static org.sonar.db.permission.OrganizationPermission.PROVISION_PROJECTS;
import static org.sonar.db.permission.OrganizationPermission.SCAN;
import static org.sonar.db.user.GroupTesting.newGroupDto;
import static org.sonar.test.JsonAssert.assertJson;

public class CurrentActionTest {
  @Rule
  public UserSessionRule userSessionRule = UserSessionRule.standalone();
  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);
  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  private DbClient dbClient = db.getDbClient();
  private DefaultOrganizationProvider defaultOrganizationProvider = TestDefaultOrganizationProvider.from(db);
  private OrganizationDbTester organizationDbTester = db.organizations();

  private PluginRepository pluginRepository = mock(PluginRepository.class);
  private MapSettings settings = new MapSettings();
  private TestOrganizationFlags organizationFlags = TestOrganizationFlags.standalone();
  private HomepageTypesImpl homepageTypes = new HomepageTypesImpl(settings.asConfig(), organizationFlags, db.getDbClient());

  private WsActionTester ws = new WsActionTester(
    new CurrentAction(userSessionRule, dbClient, defaultOrganizationProvider, new AvatarResolverImpl(), homepageTypes, pluginRepository));

  @Test
  public void return_user_info() {
    db.users().insertUser(user -> user
      .setLogin("obiwan.kenobi")
      .setName("Obiwan Kenobi")
      .setEmail("obiwan.kenobi@starwars.com")
      .setLocal(true)
      .setExternalIdentity("obiwan")
      .setExternalIdentityProvider("sonarqube")
      .setScmAccounts(newArrayList("obiwan:github", "obiwan:bitbucket"))
      .setOnboarded(false));
    userSessionRule.logIn("obiwan.kenobi");

    CurrentWsResponse response = call();

    assertThat(response)
      .extracting(CurrentWsResponse::getIsLoggedIn, CurrentWsResponse::getLogin, CurrentWsResponse::getName, CurrentWsResponse::getEmail, CurrentWsResponse::getAvatar,
        CurrentWsResponse::getLocal,
        CurrentWsResponse::getExternalIdentity, CurrentWsResponse::getExternalProvider, CurrentWsResponse::getScmAccountsList, CurrentWsResponse::getShowOnboardingTutorial)
      .containsExactly(true, "obiwan.kenobi", "Obiwan Kenobi", "obiwan.kenobi@starwars.com", "f5aa64437a1821ffe8b563099d506aef", true, "obiwan", "sonarqube",
        newArrayList("obiwan:github", "obiwan:bitbucket"), true);
  }

  @Test
  public void return_minimal_user_info() {
    db.users().insertUser(user -> user
      .setLogin("obiwan.kenobi")
      .setName("Obiwan Kenobi")
      .setEmail(null)
      .setLocal(true)
      .setExternalIdentity("obiwan")
      .setExternalIdentityProvider("sonarqube")
      .setScmAccounts((String) null));
    userSessionRule.logIn("obiwan.kenobi");

    CurrentWsResponse response = call();

    assertThat(response)
      .extracting(CurrentWsResponse::getIsLoggedIn, CurrentWsResponse::getLogin, CurrentWsResponse::getName, CurrentWsResponse::hasAvatar, CurrentWsResponse::getLocal,
        CurrentWsResponse::getExternalIdentity, CurrentWsResponse::getExternalProvider)
      .containsExactly(true, "obiwan.kenobi", "Obiwan Kenobi", false, true, "obiwan", "sonarqube");
    assertThat(response.hasEmail()).isFalse();
    assertThat(response.getScmAccountsList()).isEmpty();
    assertThat(response.getGroupsList()).isEmpty();
    assertThat(response.getPermissions().getGlobalList()).isEmpty();
  }

  @Test
  public void convert_empty_email_to_null() {
    db.users().insertUser(user -> user
      .setLogin("obiwan.kenobi")
      .setEmail(""));
    userSessionRule.logIn("obiwan.kenobi");

    CurrentWsResponse response = call();

    assertThat(response.hasEmail()).isFalse();
  }

  @Test
  public void return_group_membership() {
    UserDto user = db.users().insertUser();
    userSessionRule.logIn(user.getLogin());
    db.users().insertMember(db.users().insertGroup(newGroupDto().setName("Jedi")), user);
    db.users().insertMember(db.users().insertGroup(newGroupDto().setName("Rebel")), user);

    CurrentWsResponse response = call();

    assertThat(response.getGroupsList()).containsOnly("Jedi", "Rebel");
  }

  @Test
  public void return_permissions() {
    UserDto user = db.users().insertUser();
    userSessionRule
      .logIn(user.getLogin())
      // permissions on default organization
      .addPermission(SCAN, db.getDefaultOrganization())
      .addPermission(ADMINISTER_QUALITY_PROFILES, db.getDefaultOrganization())
      // permissions on other organizations are ignored
      .addPermission(ADMINISTER, db.organizations().insert());

    CurrentWsResponse response = call();

    assertThat(response.getPermissions().getGlobalList()).containsOnly("profileadmin", "scan");
  }

  @Test
  public void return_homepage_when_set_to_MY_PROJECTS() {
    UserDto user = db.users().insertUser(u -> u.setHomepageType("MY_PROJECTS"));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType)
      .containsExactly(CurrentWsResponse.HomepageType.MY_PROJECTS);
  }

  @Test
  public void return_homepage_when_set_to_portfolios() {
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PORTFOLIOS"));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType)
      .containsExactly(CurrentWsResponse.HomepageType.PORTFOLIOS);
  }

  @Test
  public void return_homepage_when_set_to_a_portfolio() {
    withGovernancePlugin();
    ComponentDto portfolio = db.components().insertPrivatePortfolio(db.getDefaultOrganization());
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PORTFOLIO").setHomepageParameter(portfolio.uuid()));
    userSessionRule.logIn(user).addProjectPermission(USER, portfolio);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType, CurrentWsResponse.Homepage::getComponent)
      .containsExactly(CurrentWsResponse.HomepageType.PORTFOLIO, portfolio.getKey());
  }

  @Test
  public void return_default_when_set_to_a_portfolio_but_no_rights_on_this_portfolio() {
    withGovernancePlugin();
    ComponentDto portfolio = db.components().insertPrivatePortfolio(db.getDefaultOrganization());
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PORTFOLIO").setHomepageParameter(portfolio.uuid()));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType)
      .containsExactly(CurrentWsResponse.HomepageType.PROJECTS);
  }

  @Test
  public void return_homepage_when_set_to_an_application() {
    withGovernancePlugin();
    ComponentDto application = db.components().insertPrivateApplication(db.getDefaultOrganization());
    UserDto user = db.users().insertUser(u -> u.setHomepageType("APPLICATION").setHomepageParameter(application.uuid()));
    userSessionRule.logIn(user).addProjectPermission(USER, application);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType, CurrentWsResponse.Homepage::getComponent)
      .containsExactly(CurrentWsResponse.HomepageType.APPLICATION, application.getKey());
  }

  @Test
  public void return_default_homepage_when_set_to_an_application_but_no_rights_on_this_application() {
    withGovernancePlugin();
    ComponentDto application = db.components().insertPrivateApplication(db.getDefaultOrganization());
    UserDto user = db.users().insertUser(u -> u.setHomepageType("APPLICATION").setHomepageParameter(application.uuid()));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType)
      .containsExactly(CurrentWsResponse.HomepageType.PROJECTS);
  }

  @Test
  public void return_homepage_when_set_to_a_project() {
    ComponentDto project = db.components().insertPrivateProject();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter(project.uuid()));
    userSessionRule.logIn(user).addProjectPermission(USER, project);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType, CurrentWsResponse.Homepage::getComponent)
      .containsExactly(CurrentWsResponse.HomepageType.PROJECT, project.getKey());
  }

  @Test
  public void return_default_homepage_when_set_to_a_project_but_no_rights_on_this_project() {
    ComponentDto project = db.components().insertPrivateProject();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter(project.uuid()));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType)
      .containsExactly(CurrentWsResponse.HomepageType.PROJECTS);
  }

  @Test
  public void return_homepage_when_set_to_an_organization() {

    OrganizationDto organizationDto = organizationDbTester.insert();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("ORGANIZATION").setHomepageParameter(organizationDto.getUuid()));
    userSessionRule.logIn(user);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType, CurrentWsResponse.Homepage::getOrganization)
      .containsExactly(CurrentWsResponse.HomepageType.ORGANIZATION, organizationDto.getKey());
  }

  @Test
  public void return_homepage_when_set_to_a_branch() {
    ComponentDto project = db.components().insertMainBranch();
    ComponentDto branch = db.components().insertProjectBranch(project);
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter(branch.uuid()));
    userSessionRule.logIn(user).addProjectPermission(USER, project);

    CurrentWsResponse response = call();

    assertThat(response.getHomepage())
      .extracting(CurrentWsResponse.Homepage::getType, CurrentWsResponse.Homepage::getComponent, CurrentWsResponse.Homepage::getBranch)
      .containsExactly(CurrentWsResponse.HomepageType.PROJECT, branch.getKey(), branch.getBranch());
  }

  @Test
  public void fail_with_ISE_when_user_login_in_db_does_not_exist() {
    db.users().insertUser(usert -> usert.setLogin("another"));
    userSessionRule.logIn("obiwan.kenobi");

    expectedException.expect(IllegalStateException.class);
    expectedException.expectMessage("User login 'obiwan.kenobi' cannot be found");

    call();
  }

  @Test
  public void anonymous() {
    userSessionRule
      .anonymous()
      .addPermission(SCAN, db.getDefaultOrganization())
      .addPermission(PROVISION_PROJECTS, db.getDefaultOrganization());

    CurrentWsResponse response = call();

    assertThat(response.getIsLoggedIn()).isFalse();
    assertThat(response.getPermissions().getGlobalList()).containsOnly("scan", "provisioning");
    assertThat(response)
      .extracting(CurrentWsResponse::hasLogin, CurrentWsResponse::hasName, CurrentWsResponse::hasEmail, CurrentWsResponse::hasLocal,
        CurrentWsResponse::hasExternalIdentity, CurrentWsResponse::hasExternalProvider)
      .containsOnly(false);
    assertThat(response.getScmAccountsList()).isEmpty();
    assertThat(response.getGroupsList()).isEmpty();
  }

  @Test
  public void json_example() {
    ComponentDto componentDto = db.components().insertPrivateProject(u -> u.setUuid("UUID-of-the-death-star"), u -> u.setDbKey("death-star-key"));
    userSessionRule
      .logIn("obiwan.kenobi")
      .addPermission(SCAN, db.getDefaultOrganization())
      .addPermission(ADMINISTER_QUALITY_PROFILES, db.getDefaultOrganization())
      .addProjectPermission(USER, componentDto);
    UserDto obiwan = db.users().insertUser(user -> user
      .setLogin("obiwan.kenobi")
      .setName("Obiwan Kenobi")
      .setEmail("obiwan.kenobi@starwars.com")
      .setLocal(true)
      .setExternalIdentity("obiwan.kenobi")
      .setExternalIdentityProvider("sonarqube")
      .setScmAccounts(newArrayList("obiwan:github", "obiwan:bitbucket"))
      .setOnboarded(true)
      .setHomepageType("PROJECT")
      .setHomepageParameter("UUID-of-the-death-star"));
    db.users().insertMember(db.users().insertGroup(newGroupDto().setName("Jedi")), obiwan);
    db.users().insertMember(db.users().insertGroup(newGroupDto().setName("Rebel")), obiwan);


    String response = ws.newRequest().execute().getInput();

    assertJson(response).isSimilarTo(getClass().getResource("current-example.json"));
  }

  @Test
  public void test_definition() {
    WebService.Action definition = ws.getDef();
    assertThat(definition.key()).isEqualTo("current");
    assertThat(definition.description()).isEqualTo("Get the details of the current authenticated user.");
    assertThat(definition.since()).isEqualTo("5.2");
    assertThat(definition.isPost()).isFalse();
    assertThat(definition.isInternal()).isTrue();
    assertThat(definition.responseExampleAsString()).isNotEmpty();
    assertThat(definition.params()).isEmpty();
    assertThat(definition.changelog()).hasSize(2);
  }

  private CurrentWsResponse call() {
    return ws.newRequest().executeProtobuf(CurrentWsResponse.class);
  }

  @Test
  public void fallback_when_user_homepage_project_does_not_exist_in_db() {
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter("not-existing-project-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage()).isNotNull();
  }

  @Test
  public void fallback_when_user_homepage_organization_does_not_exist_in_db() {
    UserDto user = db.users().insertUser(u -> u.setHomepageType("ORGANIZATION").setHomepageParameter("not-existing-organization-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage()).isNotNull();
  }

  @Test
  public void fallback_when_user_homepage_portfolio_does_not_exist_in_db() {
    withGovernancePlugin();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PORTFOLIO").setHomepageParameter("not-existing-portfolio-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage()).isNotNull();
  }

  @Test
  public void fallback_when_user_homepage_application_does_not_exist_in_db() {
    withGovernancePlugin();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("APPLICATION").setHomepageParameter("not-existing-application-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage()).isNotNull();
  }

  @Test
  public void fallback_when_user_homepage_application_and_governance_plugin_is_not_installed() {
    withoutGovernancePlugin();
    ComponentDto application = db.components().insertPrivateApplication(db.getDefaultOrganization());
    UserDto user = db.users().insertUser(u -> u.setHomepageType("APPLICATION").setHomepageParameter(application.uuid()));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage().getType().toString()).isEqualTo("PROJECTS");
  }

  @Test
  public void fallback_to_PROJECTS_when_on_SonarQube() {
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter("not-existing-project-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage().getType().toString()).isEqualTo("PROJECTS");
  }

  @Test
  public void fallback_to_MY_PROJECTS_when_on_SonarCloud() {
    onSonarCloud();
    UserDto user = db.users().insertUser(u -> u.setHomepageType("PROJECT").setHomepageParameter("not-existing-project-uuid"));
    userSessionRule.logIn(user.getLogin());

    CurrentWsResponse response = ws.newRequest().executeProtobuf(CurrentWsResponse.class);

    assertThat(response.getHomepage().getType().toString()).isEqualTo("MY_PROJECTS");
  }

  private void onSonarCloud() {
    settings.setProperty("sonar.sonarcloud.enabled", true);
  }

  private void withGovernancePlugin(){
    when(pluginRepository.hasPlugin("governance")).thenReturn(true);
  }

  private void withoutGovernancePlugin(){
    when(pluginRepository.hasPlugin("governance")).thenReturn(false);
  }

}
