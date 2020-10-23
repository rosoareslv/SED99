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
package org.sonar.server.batch;

import com.google.common.collect.ImmutableMap;
import java.util.Date;
import java.util.Map;
import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.Scopes;
import org.sonar.api.utils.System2;
import org.sonar.api.web.UserRole;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTesting;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.db.organization.OrganizationTesting;
import org.sonar.db.property.PropertyDto;
import org.sonar.db.qualityprofile.DefaultQProfileDto;
import org.sonar.db.qualityprofile.QProfileDto;
import org.sonar.db.source.FileSourceDto;
import org.sonar.db.source.FileSourceDto.Type;
import org.sonar.scanner.protocol.input.FileData;
import org.sonar.scanner.protocol.input.ProjectRepositories;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.exceptions.NotFoundException;
import org.sonar.server.qualityprofile.QProfileName;
import org.sonar.server.tester.UserSessionRule;

import static java.lang.String.format;
import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.Assert.fail;
import static org.sonar.api.utils.DateUtils.formatDateTime;
import static org.sonar.core.permission.GlobalPermissions.SCAN_EXECUTION;
import static org.sonar.db.permission.OrganizationPermission.SCAN;
import static org.sonar.server.qualityprofile.QProfileTesting.newQProfileDto;

public class ProjectDataLoaderTest {
  @Rule
  public DbTester dbTester = DbTester.create(System2.INSTANCE);
  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();

  private DbClient dbClient = dbTester.getDbClient();
  private DbSession dbSession = dbTester.getSession();

  private ProjectDataLoader underTest = new ProjectDataLoader(dbClient, userSession);

  @After
  public void tearDown() throws Exception {
    dbSession.close();
  }

  @Test
  public void load_fails_with_NPE_if_query_is_null() {
    expectedException.expect(NullPointerException.class);

    underTest.load(null);
  }

  @Test
  public void load_fails_with_NFE_if_query_is_empty() {
    expectedException.expect(NotFoundException.class);
    expectedException.expectMessage("Project or module with key 'null' is not found");

    underTest.load(ProjectDataQuery.create());
  }

  @Test
  public void load_throws_NotFoundException_if_component_does_not_exist() {
    String key = "theKey";

    expectedException.expect(NotFoundException.class);
    expectedException.expectMessage("Project or module with key '" + key + "' is not found");

    underTest.load(ProjectDataQuery.create().setModuleKey(key));
  }

  private int uuidCounter = 0;

  @Test
  public void load_fails_with_BRE_if_component_is_neither_a_project_or_a_module() {
    String[][] allScopesAndQualifierButProjectAndModule = {
      {Scopes.PROJECT, Qualifiers.VIEW},
      {Scopes.PROJECT, Qualifiers.SUBVIEW},
      {Scopes.PROJECT, Qualifiers.APP},
      {Scopes.FILE, Qualifiers.PROJECT},
      {Scopes.DIRECTORY, Qualifiers.DIRECTORY},
      {Scopes.FILE, Qualifiers.UNIT_TEST_FILE},
      {Scopes.PROJECT, "DEV"},
      {Scopes.PROJECT, "DEV_PRJ"}
    };

    OrganizationDto organizationDto = dbTester.organizations().insert();
    for (String[] scopeAndQualifier : allScopesAndQualifierButProjectAndModule) {
      String scope = scopeAndQualifier[0];
      String qualifier = scopeAndQualifier[1];
      String key = "theKey_" + scope + "_" + qualifier;
      String uuid = "uuid_" + uuidCounter++;
      dbClient.componentDao().insert(dbSession, new ComponentDto()
        .setOrganizationUuid(organizationDto.getUuid())
        .setUuid(uuid)
        .setUuidPath(uuid + ".")
        .setRootUuid(uuid)
        .setProjectUuid(uuid)
        .setScope(scope)
        .setQualifier(qualifier)
        .setDbKey(key));
      dbSession.commit();

      try {
        underTest.load(ProjectDataQuery.create().setModuleKey(key));
        fail(format("A NotFoundException should have been raised because scope (%s) or qualifier (%s) is not project", scope, qualifier));
      } catch (BadRequestException e) {
        assertThat(e).hasMessage("Key '" + key + "' belongs to a component which is not a Project");
      }
    }
  }

  @Test
  public void throw_ForbiddenException_if_no_browse_permission_nor_scan_permission() {
    ComponentDto project = dbTester.components().insertPrivateProject();
    userSession.logIn();

    expectedException.expect(ForbiddenException.class);
    expectedException.expectMessage("You're not authorized to execute any SonarQube analysis");

    underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
  }

  @Test
  public void throw_ForbiddenException_if_browse_permission_but_not_scan_permission() {
    ComponentDto project = dbTester.components().insertPrivateProject();
    userSession.logIn().addProjectPermission(UserRole.USER, project);

    expectedException.expect(ForbiddenException.class);
    expectedException.expectMessage("You're only authorized to execute a local (preview) SonarQube analysis without pushing the results to the SonarQube server");

    underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
  }

  @Test
  public void issues_mode_is_allowed_if_user_has_browse_permission() {
    ComponentDto project = dbTester.components().insertPrivateProject();
    userSession.logIn().addProjectPermission(UserRole.USER, project);

    ProjectRepositories repositories = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(true));

    assertThat(repositories).isNotNull();
  }

  @Test
  public void issues_mode_is_forbidden_if_user_doesnt_have_browse_permission() {
    ComponentDto project = dbTester.components().insertPrivateProject();
    userSession.logIn().addProjectPermission(GlobalPermissions.SCAN_EXECUTION, project);

    expectedException.expect(ForbiddenException.class);
    expectedException.expectMessage("You don't have the required permissions to access this project");

    underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(true));
  }

  @Test
  public void scan_permission_on_organization_is_enough_even_without_scan_permission_on_project() {
    ComponentDto project = dbTester.components().insertPrivateProject();
    userSession.logIn().addPermission(SCAN, project.getOrganizationUuid());
    userSession.logIn().addProjectPermission(UserRole.USER, project);

    ProjectRepositories repositories = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(true));

    assertThat(repositories).isNotNull();
  }


  @Test
  public void return_project_settings_with_global_scan_permission() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));
    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));

    Map<String, String> projectSettings = ref.settings(project.getDbKey());
    assertThat(projectSettings).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
  }

  @Test
  public void return_project_settings_with_project_scan_permission() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn("john").addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));
    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));

    Map<String, String> projectSettings = ref.settings(project.getDbKey());
    assertThat(projectSettings).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
  }

  @Test
  public void not_returned_secured_settings_when_lgged_but_no_scan_permission() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn("john").addProjectPermission(UserRole.USER, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));
    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(true));
    Map<String, String> projectSettings = ref.settings(project.getDbKey());
    assertThat(projectSettings).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR"));
  }

  @Test
  public void return_project_with_module_settings() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // Module properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-SERVER").setResourceId(module.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(module.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
    assertThat(ref.settings(module.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-SERVER",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void return_project_with_module_settings_inherited_from_project() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // No property on module -> should have the same as project

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
    assertThat(ref.settings(module.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
  }

  @Test
  public void return_project_with_module_with_sub_module() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // Module properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-SERVER").setResourceId(module.getId()));
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(module.getId()));

    ComponentDto subModule = ComponentTesting.newModuleDto(module);
    dbClient.componentDao().insert(dbSession, subModule);

    // Sub module properties
    dbClient.propertiesDao().saveProperty(
      dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-SERVER-DAO").setResourceId(subModule.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
    assertThat(ref.settings(module.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-SERVER",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
    assertThat(ref.settings(subModule.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-SERVER-DAO",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void return_project_with_two_modules() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));

    ComponentDto module1 = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module1);

    // Module 1 properties
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-SERVER").setResourceId(module1.getId()));
    // This property should not be found on the other module
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(module1.getId()));

    ComponentDto module2 = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module2);

    // Module 2 property
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-APPLICATION").setResourceId(module2.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
    assertThat(ref.settings(module1.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-SERVER",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
    assertThat(ref.settings(module2.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-APPLICATION",
      "sonar.jira.login.secured", "john"));
  }

  @Test
  public void return_provisioned_project_settings() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    // No snapshot attached on the project -> provisioned project
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john"));
  }

  @Test
  public void return_sub_module_settings() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();
    // No project properties

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);
    // No module properties

    ComponentDto subModule = ComponentTesting.newModuleDto(module);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, subModule);

    // Sub module properties
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(subModule.getId()));
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(subModule.getId()));
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(subModule.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(subModule.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEmpty();
    assertThat(ref.settings(module.getDbKey())).isEmpty();
    assertThat(ref.settings(subModule.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void return_sub_module_settings_including_settings_from_parent_modules() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project property
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // Module property
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(module.getId()));

    ComponentDto subModule = ComponentTesting.newModuleDto(module);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, subModule);

    // Sub module properties
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(subModule.getId()));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(subModule.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEmpty();
    assertThat(ref.settings(module.getDbKey())).isEmpty();
    assertThat(ref.settings(subModule.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void return_sub_module_settings_only_inherited_from_project() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR").setResourceId(project.getId()));
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);
    // No module property

    ComponentDto subModule = ComponentTesting.newModuleDto(module);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, subModule);
    // No sub module property

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(subModule.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEmpty();
    assertThat(ref.settings(module.getDbKey())).isEmpty();
    assertThat(ref.settings(subModule.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void return_sub_module_settings_inherited_from_project_and_module() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // Project properties
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.login.secured").setValue("john").setResourceId(project.getId()));
    dbClient.propertiesDao()
      .saveProperty(dbSession, new PropertyDto().setKey("sonar.coverage.exclusions").setValue("**/*.java").setResourceId(project.getId()));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // Module property
    dbClient.propertiesDao().saveProperty(dbSession, new PropertyDto().setKey("sonar.jira.project.key").setValue("SONAR-SERVER").setResourceId(module.getId()));

    ComponentDto subModule = ComponentTesting.newModuleDto(module);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, subModule);
    // No sub module property

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(subModule.getDbKey()));
    assertThat(ref.settings(project.getDbKey())).isEmpty();
    assertThat(ref.settings(module.getDbKey())).isEmpty();
    assertThat(ref.settings(subModule.getDbKey())).isEqualTo(ImmutableMap.of(
      "sonar.jira.project.key", "SONAR-SERVER",
      "sonar.jira.login.secured", "john",
      "sonar.coverage.exclusions", "**/*.java"));
  }

  @Test
  public void fail_when_no_browse_permission_nor_scan_permission() {
    userSession.logIn();

    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    dbSession.commit();

    try {
      underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(ForbiddenException.class).hasMessage(Messages.NO_PERMISSION);
    }
  }

  @Test
  public void fail_when_not_preview_and_only_browse_permission_without_scan_permission() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    dbSession.commit();

    userSession.logIn("john").addProjectPermission(UserRole.USER, project);

    expectedException.expect(ForbiddenException.class);
    expectedException.expectMessage("You're only authorized to execute a local (preview) SonarQube analysis without pushing the results to the SonarQube server. " +
      "Please contact your SonarQube administrator.");
    underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(false));
  }

  @Test
  public void fail_when_preview_and_only_scan_permission_without_browse_permission() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    dbSession.commit();

    userSession.logIn("john").addProjectPermission(GlobalPermissions.SCAN_EXECUTION, project);

    expectedException.expect(ForbiddenException.class);
    expectedException.expectMessage("You don't have the required permissions to access this project. Please contact your SonarQube administrator.");
    underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()).setIssuesMode(true));
  }

  @Test
  public void return_file_data_from_single_project() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    ComponentDto file = ComponentTesting.newFileDto(project, null, "file");
    dbClient.componentDao().insert(dbSession, file);
    dbClient.fileSourceDao().insert(dbSession, newFileSourceDto(file).setSrcHash("123456"));
    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.fileDataByPath(project.getDbKey())).hasSize(1);
    FileData fileData = ref.fileData(project.getDbKey(), file.path());
    assertThat(fileData.hash()).isEqualTo("123456");
  }

  @Test
  public void return_file_data_from_multi_modules() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // File on project
    ComponentDto projectFile = ComponentTesting.newFileDto(project, null, "projectFile");
    dbClient.componentDao().insert(dbSession, projectFile);
    dbClient.fileSourceDao().insert(dbSession, newFileSourceDto(projectFile).setSrcHash("123456"));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    dbClient.componentDao().insert(dbSession, module);

    // File on module
    ComponentDto moduleFile = ComponentTesting.newFileDto(module, null, "moduleFile");
    dbClient.componentDao().insert(dbSession, moduleFile);
    dbClient.fileSourceDao().insert(dbSession, newFileSourceDto(moduleFile).setSrcHash("789456"));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(project.getDbKey()));
    assertThat(ref.fileData(project.getDbKey(), projectFile.path()).hash()).isEqualTo("123456");
    assertThat(ref.fileData(module.getDbKey(), moduleFile.path()).hash()).isEqualTo("789456");
  }

  @Test
  public void return_file_data_from_module() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    dbClient.componentDao().insert(dbSession, project);
    addDefaultProfile();

    // File on project
    ComponentDto projectFile = ComponentTesting.newFileDto(project, null, "projectFile");
    dbClient.componentDao().insert(dbSession, projectFile);
    dbClient.fileSourceDao().insert(dbSession, newFileSourceDto(projectFile).setSrcHash("123456").setRevision("987654321"));

    ComponentDto module = ComponentTesting.newModuleDto(project);
    userSession.logIn().addProjectPermission(SCAN_EXECUTION, project);
    dbClient.componentDao().insert(dbSession, module);

    // File on module
    ComponentDto moduleFile = ComponentTesting.newFileDto(module, null, "moduleFile");
    dbClient.componentDao().insert(dbSession, moduleFile);
    dbClient.fileSourceDao().insert(dbSession, newFileSourceDto(moduleFile).setSrcHash("789456").setRevision("123456789"));

    dbSession.commit();

    ProjectRepositories ref = underTest.load(ProjectDataQuery.create().setModuleKey(module.getDbKey()));
    assertThat(ref.fileData(module.getDbKey(), moduleFile.path()).hash()).isEqualTo("789456");
    assertThat(ref.fileData(module.getDbKey(), moduleFile.path()).revision()).isEqualTo("123456789");
    assertThat(ref.fileData(project.getDbKey(), projectFile.path())).isNull();
  }

  private void addDefaultProfile() {
    OrganizationDto organizationDto = OrganizationTesting.newOrganizationDto();
    dbClient.organizationDao().insert(dbSession, organizationDto, false);
    QProfileDto profileDto = newQProfileDto(organizationDto, QProfileName.createFor("xoo", "SonarQube way"), "abcd").setRulesUpdatedAt(
      formatDateTime(new Date()));
    dbClient.qualityProfileDao().insert(dbSession, profileDto);
    dbClient.defaultQProfileDao().insertOrUpdate(dbSession, DefaultQProfileDto.from(profileDto));
  }

  private static FileSourceDto newFileSourceDto(ComponentDto file) {
    return new FileSourceDto()
      .setFileUuid(file.uuid())
      .setProjectUuid(file.projectUuid())
      .setDataHash("0263047cd758c68c27683625f072f010")
      .setLineHashes("8d7b3d6b83c0a517eac07e1aac94b773")
      .setCreatedAt(System.currentTimeMillis())
      .setUpdatedAt(System.currentTimeMillis())
      .setDataType(Type.SOURCE)
      .setRevision("123456789")
      .setSrcHash("123456");
  }
}
