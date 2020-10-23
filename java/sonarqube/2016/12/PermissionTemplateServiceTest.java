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
package org.sonar.server.permission;

import java.util.List;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.config.MapSettings;
import org.sonar.api.config.Settings;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.utils.System2;
import org.sonar.api.web.UserRole;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.permission.template.PermissionTemplateDbTester;
import org.sonar.db.permission.template.PermissionTemplateDto;
import org.sonar.db.user.GroupDto;
import org.sonar.db.user.UserDto;
import org.sonar.server.organization.DefaultOrganizationProvider;
import org.sonar.server.organization.TestDefaultOrganizationProvider;
import org.sonar.server.permission.index.PermissionIndexer;
import org.sonar.server.tester.UserSessionRule;

import static java.util.Collections.singletonList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.core.permission.GlobalPermissions.PROVISIONING;
import static org.sonar.core.permission.GlobalPermissions.SCAN_EXECUTION;
import static org.sonar.db.component.ComponentTesting.newProjectDto;
import static org.sonar.db.user.GroupTesting.newGroupDto;

public class PermissionTemplateServiceTest {

  private static final String DEFAULT_TEMPLATE = "default_20130101_010203";
  private static final ComponentDto PROJECT = newProjectDto().setId(123L).setUuid("THE_PROJECT_UUID");
  private static final long NOW = 123456789L;

  @Rule
  public ExpectedException throwable = ExpectedException.none();

  private System2 system2 = mock(System2.class);

  @Rule
  public DbTester dbTester = DbTester.create(system2);

  private UserSessionRule userSession = UserSessionRule.standalone();
  private PermissionTemplateDbTester templateDb = dbTester.permissionTemplates();
  private DbSession session = dbTester.getSession();
  private Settings settings = new MapSettings();
  private PermissionIndexer permissionIndexer = mock(PermissionIndexer.class);
  private DefaultOrganizationProvider defaultOrganizationProvider = TestDefaultOrganizationProvider.from(dbTester);
  private PermissionTemplateService underTest = new PermissionTemplateService(dbTester.getDbClient(), settings,
    permissionIndexer, userSession, defaultOrganizationProvider);

  @Before
  public void setUp() {
    when(system2.now()).thenReturn(NOW);
  }

  @Test
  public void apply_permission_template() {
    dbTester.prepareDbUnit(getClass(), "should_apply_permission_template.xml");

    assertThat(selectProjectPermissionsOfGroup("org1", 100L, PROJECT)).isEmpty();
    assertThat(selectProjectPermissionsOfGroup("org1", 101L, PROJECT)).isEmpty();
    assertThat(selectProjectPermissionsOfGroup("org1", null, PROJECT)).isEmpty();
    assertThat(selectProjectPermissionsOfUser(200L, PROJECT)).isEmpty();

    PermissionTemplateDto template = dbTester.getDbClient().permissionTemplateDao().selectByUuid(session, "default_20130101_010203");
    underTest.apply(session, template, singletonList(PROJECT));

    assertThat(selectProjectPermissionsOfGroup("org1", 100L, PROJECT)).containsOnly("admin", "issueadmin");
    assertThat(selectProjectPermissionsOfGroup("org1", 101L, PROJECT)).containsOnly("user", "codeviewer");
    assertThat(selectProjectPermissionsOfGroup("org1", null, PROJECT)).containsOnly("user", "codeviewer");
    assertThat(selectProjectPermissionsOfUser(200L, PROJECT)).containsOnly("admin");

    checkAuthorizationUpdatedAtIsUpdated();
  }

  private List<String> selectProjectPermissionsOfGroup(String organizationUuid, @Nullable Long groupId, ComponentDto project) {
    return dbTester.getDbClient().groupPermissionDao().selectProjectPermissionsOfGroup(session,
      organizationUuid, groupId != null ? groupId : null, project.getId());
  }

  private List<String> selectProjectPermissionsOfUser(long userId, ComponentDto project) {
    return dbTester.getDbClient().userPermissionDao().selectProjectPermissionsOfUser(session,
      userId, project.getId());
  }

  @Test
  public void applyDefaultPermissionTemplate_from_component_key() {
    dbTester.prepareDbUnit(getClass(), "apply_default_permission_template_by_component_id.xml");
    userSession.setGlobalPermissions(PROVISIONING);
    settings.setProperty("sonar.permission.template.default", DEFAULT_TEMPLATE);

    underTest.applyDefaultPermissionTemplate("org.struts:struts");
    session.commit();

    dbTester.assertDbUnitTable(getClass(), "apply_default_permission_template_by_component_id-result.xml", "user_roles", "user_id", "resource_id", "role");
  }

  @Test
  public void would_user_have_permission_with_default_permission_template() {
    UserDto user = dbTester.users().insertUser();
    GroupDto group = dbTester.users().insertGroup(newGroupDto());
    dbTester.users().insertMember(group, user);
    PermissionTemplateDto template = templateDb.insertTemplate();
    setDefaultTemplateUuid(template.getUuid());
    templateDb.addProjectCreatorToTemplate(template.getId(), SCAN_EXECUTION);
    templateDb.addUserToTemplate(template.getId(), user.getId(), UserRole.USER);
    templateDb.addGroupToTemplate(template.getId(), group.getId(), UserRole.CODEVIEWER);
    templateDb.addGroupToTemplate(template.getId(), null, UserRole.ISSUE_ADMIN);

    // authenticated user
    checkWouldUserHavePermission(user.getId(), UserRole.ADMIN, false);
    checkWouldUserHavePermission(user.getId(), SCAN_EXECUTION, true);
    checkWouldUserHavePermission(user.getId(), UserRole.USER, true);
    checkWouldUserHavePermission(user.getId(), UserRole.CODEVIEWER, true);
    checkWouldUserHavePermission(user.getId(), UserRole.ISSUE_ADMIN, true);

    // anonymous user
    checkWouldUserHavePermission(null, UserRole.ADMIN, false);
    checkWouldUserHavePermission(null, SCAN_EXECUTION, false);
    checkWouldUserHavePermission(null, UserRole.USER, false);
    checkWouldUserHavePermission(null, UserRole.CODEVIEWER, false);
    checkWouldUserHavePermission(null, UserRole.ISSUE_ADMIN, true);
  }

  @Test
  public void would_user_have_permission_with_unknown_default_permission_template() {
    setDefaultTemplateUuid("UNKNOWN_TEMPLATE_UUID");

    checkWouldUserHavePermission(null, UserRole.ADMIN, false);
  }

  @Test
  public void would_user_have_permission_with_empty_template() {
    PermissionTemplateDto template = templateDb.insertTemplate();
    setDefaultTemplateUuid(template.getUuid());

    checkWouldUserHavePermission(null, UserRole.ADMIN, false);
  }

  private void checkWouldUserHavePermission(@Nullable Long userId, String permission, boolean expectedResult) {
    assertThat(underTest.wouldUserHavePermissionWithDefaultTemplate(session, userId, permission, null, "PROJECT_KEY", Qualifiers.PROJECT)).isEqualTo(expectedResult);
  }

  private void checkAuthorizationUpdatedAtIsUpdated() {
    assertThat(dbTester.getDbClient().resourceDao().selectResource(PROJECT.getId(), session).getAuthorizationUpdatedAt()).isEqualTo(NOW);
  }

  private void setDefaultTemplateUuid(String templateUuid) {
    settings.setProperty("sonar.permission.template.default", templateUuid);
  }

}
