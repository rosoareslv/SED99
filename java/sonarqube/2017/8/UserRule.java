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
package util.user;

import com.google.common.base.Optional;
import com.google.common.base.Predicate;
import com.google.common.collect.FluentIterable;
import com.sonar.orchestrator.Orchestrator;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import javax.annotation.CheckForNull;
import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import org.junit.rules.ExternalResource;
import org.sonarqube.ws.Organizations;
import org.sonarqube.ws.WsUsers;
import org.sonarqube.ws.client.GetRequest;
import org.sonarqube.ws.client.PostRequest;
import org.sonarqube.ws.client.WsClient;
import org.sonarqube.ws.client.WsResponse;
import org.sonarqube.ws.client.permission.AddUserWsRequest;
import org.sonarqube.ws.client.user.CreateRequest;
import org.sonarqube.ws.client.user.SearchRequest;
import org.sonarqube.ws.client.user.UsersService;
import util.selenium.Consumer;

import static java.util.Arrays.asList;
import static java.util.Arrays.stream;
import static java.util.Objects.requireNonNull;
import static org.apache.commons.lang.RandomStringUtils.randomAlphabetic;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.guava.api.Assertions.assertThat;
import static util.ItUtils.newAdminWsClient;

/**
 * @deprecated replaced by {@link org.sonarqube.tests.Tester}
 */
@Deprecated
public class UserRule extends ExternalResource implements GroupManagement {

  public static final String ADMIN_LOGIN = "admin";
  private static final AtomicInteger ID_GENERATOR = new AtomicInteger();
  private final Orchestrator orchestrator;

  private WsClient adminWsClient;
  private final GroupManagement defaultOrganizationGroupManagement;

  public UserRule(Orchestrator orchestrator) {
    this.orchestrator = orchestrator;
    this.defaultOrganizationGroupManagement = new GroupManagementImpl(null);
  }

  public static UserRule from(Orchestrator orchestrator) {
    return new UserRule(requireNonNull(orchestrator, "Orchestrator instance cannot be null"));
  }

  @Override
  protected void after() {
    deactivateAllUsers();
    // TODO delete groups
  }

  // *****************
  // Users
  // *****************

  public void resetUsers() {
    for (Users.User user : getUsers().getUsers()) {
      String userLogin = user.getLogin();
      if (!userLogin.equals(ADMIN_LOGIN)) {
        deactivateUsers(userLogin);
      }
    }
  }

  public Users.User verifyUserExists(String login, String name, @Nullable String email) {
    Optional<Users.User> user = getUserByLogin(login);
    assertThat(user).as("User with login '%s' hasn't been found", login).isPresent();
    assertThat(user.get().getLogin()).isEqualTo(login);
    assertThat(user.get().getName()).isEqualTo(name);
    assertThat(user.get().getEmail()).isEqualTo(email);
    return user.get();
  }

  public void verifyUserExists(String login, String name, @Nullable String email, boolean local) {
    Users.User user = verifyUserExists(login, name, email);
    assertThat(user.isLocal()).isEqualTo(local);
  }

  public void verifyUserDoesNotExist(String login) {
    assertThat(getUserByLogin(login)).as("Unexpected user with login '%s' has been found", login).isAbsent();
  }

  public WsUsers.CreateWsResponse.User createUser(String login, String name, @Nullable String email, String password) {
    CreateRequest.Builder request = CreateRequest.builder()
      .setLogin(login)
      .setName(name)
      .setEmail(email)
      .setPassword(password);
    return adminWsClient().users().create(request.build()).getUser();
  }

  /**
   * Create user with randomly generated values. By default password is the login.
   */
  @SafeVarargs
  public final WsUsers.CreateWsResponse.User generate(Consumer<CreateRequest.Builder>... populators) {
    int id = ID_GENERATOR.getAndIncrement();
    String login = "login" + id;
    CreateRequest.Builder request = CreateRequest.builder()
      .setLogin(login)
      .setName("name" + id)
      .setEmail(id + "@test.com")
      .setPassword(login);
    stream(populators).forEach(p -> p.accept(request));
    return adminWsClient().users().create(request.build()).getUser();
  }

  public void createUser(String login, String password) {
    createUser(login, login, null, password);
  }

  public WsUsers.CreateWsResponse.User createAdministrator(Organizations.Organization organization, String password) {
    WsUsers.CreateWsResponse.User user = generate(p -> p.setPassword(password));
    adminWsClient.organizations().addMember(organization.getKey(), user.getLogin());
    forOrganization(organization.getKey()).associateGroupsToUser(user.getLogin(), "Owners");
    return user;
  }

  /**
   * Create a new admin user with random login, having password same as login
   */
  public String createAdminUser() {
    String login = randomAlphabetic(10).toLowerCase();
    return createAdminUser(login, login);
  }

  public String createAdminUser(String login, String password) {
    createUser(login, password);
    adminWsClient.permissions().addUser(new AddUserWsRequest().setLogin(login).setPermission("admin"));
    adminWsClient.userGroups().addUser(org.sonarqube.ws.client.usergroup.AddUserWsRequest.builder().setLogin(login).setName("sonar-administrators").build());
    return login;
  }

  /**
   * Create a new root user with random login, having password same as login
   */
  public String createRootUser() {
    String login = randomAlphabetic(10).toLowerCase();
    return createRootUser(login, login);
  }

  public String createRootUser(String login, String password) {
    createUser(login, password);
    setRoot(login);
    return login;
  }

  public void setRoot(String login) {
    adminWsClient().roots().setRoot(login);
  }

  public void unsetRoot(String login) {
    adminWsClient().roots().unsetRoot(login);
  }

  public Optional<Users.User> getUserByLogin(String login) {
    return FluentIterable.from(getUsers().getUsers()).firstMatch(new MatchUserLogin(login));
  }

  public Users getUsers() {
    WsResponse response = adminWsClient().wsConnector().call(
      new GetRequest("api/users/search"))
      .failIfNotSuccessful();
    return Users.parse(response.content());
  }

  public void deactivateUsers(List<String> userLogins) {
    for (String userLogin : userLogins) {
      if (getUserByLogin(userLogin).isPresent()) {
        adminWsClient().wsConnector().call(new PostRequest("api/users/deactivate").setParam("login", userLogin)).failIfNotSuccessful();
      }
    }
  }

  public void deactivateUsers(String... userLogins) {
    deactivateUsers(asList(userLogins));
  }

  public void deactivateAllUsers() {
    UsersService service = newAdminWsClient(orchestrator).users();
    List<String> logins = service.search(SearchRequest.builder().build()).getUsersList()
      .stream()
      .filter(u -> !u.getLogin().equals("admin"))
      .map(u -> u.getLogin())
      .collect(Collectors.toList());
    deactivateUsers(logins);
  }

  // *****************
  // User groups
  // *****************

  public GroupManagement forOrganization(String organizationKey) {
    return new GroupManagementImpl(organizationKey);
  }

  private final class GroupManagementImpl implements GroupManagement {
    @CheckForNull
    private final String organizationKey;

    private GroupManagementImpl(@Nullable String organizationKey) {
      this.organizationKey = organizationKey;
    }

    @Override
    public void createGroup(String name) {
      createGroup(name, null);
    }

    @Override
    public void createGroup(String name, @Nullable String description) {
      PostRequest request = new PostRequest("api/user_groups/create")
        .setParam("name", name)
        .setParam("description", description);
      addOrganizationParam(request);
      adminWsClient().wsConnector().call(request).failIfNotSuccessful();
    }

    private void addOrganizationParam(PostRequest request) {
      request.setParam("organization", organizationKey);
    }

    private void addOrganizationParam(GetRequest request) {
      request.setParam("organization", organizationKey);
    }

    @Override
    public void removeGroups(List<String> groupNames) {
      for (String groupName : groupNames) {
        if (getGroupByName(groupName).isPresent()) {
          PostRequest request = new PostRequest("api/user_groups/delete")
            .setParam("name", groupName);
          addOrganizationParam(request);
          adminWsClient().wsConnector().call(request).failIfNotSuccessful();
        }
      }
    }

    @Override
    public void removeGroups(String... groupNames) {
      removeGroups(asList(groupNames));
    }

    @Override
    public java.util.Optional<Groups.Group> getGroupByName(String name) {
      return getGroups().getGroups().stream().filter(new MatchGroupName(name)::apply).findFirst();
    }

    @Override
    public Groups getGroups() {
      GetRequest request = new GetRequest("api/user_groups/search");
      addOrganizationParam(request);
      WsResponse response = adminWsClient().wsConnector().call(request).failIfNotSuccessful();
      return Groups.parse(response.content());
    }

    @Override
    public void verifyUserGroupMembership(String userLogin, String... expectedGroups) {
      Groups userGroup = getUserGroups(userLogin);
      List<String> userGroupName = userGroup.getGroups().stream().map(Groups.Group::getName).collect(Collectors.toList());
      assertThat(userGroupName).containsOnly(expectedGroups);
    }

    @Override
    public Groups getUserGroups(String userLogin) {
      GetRequest request = new GetRequest("api/users/groups")
        .setParam("login", userLogin)
        .setParam("selected", "selected");
      addOrganizationParam(request);
      WsResponse response = adminWsClient().wsConnector().call(request).failIfNotSuccessful();
      return Groups.parse(response.content());
    }

    @Override
    public void associateGroupsToUser(String userLogin, String... groups) {
      for (String group : groups) {
        PostRequest request = new PostRequest("api/user_groups/add_user")
          .setParam("login", userLogin)
          .setParam("name", group);
        addOrganizationParam(request);
        adminWsClient().wsConnector().call(request).failIfNotSuccessful();
      }
    }
  }

  @Override
  public void createGroup(String name) {
    defaultOrganizationGroupManagement.createGroup(name);
  }

  @Override
  public void createGroup(String name, @Nullable String description) {
    defaultOrganizationGroupManagement.createGroup(name, description);
  }

  @Override
  public void removeGroups(List<String> groupNames) {
    defaultOrganizationGroupManagement.removeGroups(groupNames);
  }

  @Override
  public void removeGroups(String... groupNames) {
    defaultOrganizationGroupManagement.removeGroups(groupNames);
  }

  @Override
  public java.util.Optional<Groups.Group> getGroupByName(String name) {
    return defaultOrganizationGroupManagement.getGroupByName(name);
  }

  @Override
  public Groups getGroups() {
    return defaultOrganizationGroupManagement.getGroups();
  }

  @Override
  public void verifyUserGroupMembership(String userLogin, String... groups) {
    defaultOrganizationGroupManagement.verifyUserGroupMembership(userLogin, groups);
  }

  @Override
  public Groups getUserGroups(String userLogin) {
    return defaultOrganizationGroupManagement.getUserGroups(userLogin);
  }

  @Override
  public void associateGroupsToUser(String userLogin, String... groups) {
    defaultOrganizationGroupManagement.associateGroupsToUser(userLogin, groups);
  }

  private WsClient adminWsClient() {
    if (adminWsClient == null) {
      adminWsClient = newAdminWsClient(orchestrator);
    }
    return adminWsClient;
  }

  private class MatchUserLogin implements Predicate<Users.User> {
    private final String login;

    private MatchUserLogin(String login) {
      this.login = login;
    }

    @Override
    public boolean apply(@Nonnull Users.User user) {
      String login = user.getLogin();
      return login != null && login.equals(this.login) && user.isActive();
    }
  }

  private class MatchGroupName implements Predicate<Groups.Group> {
    private final String groupName;

    private MatchGroupName(String groupName) {
      this.groupName = groupName;
    }

    @Override
    public boolean apply(@Nonnull Groups.Group group) {
      String groupName = group.getName();
      return groupName != null && groupName.equals(this.groupName);
    }
  }

}
