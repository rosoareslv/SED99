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
package org.sonar.server.component;

import java.util.Optional;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.Scopes;
import org.sonar.api.utils.System2;
import org.sonar.db.DbTester;
import org.sonar.db.component.BranchDto;
import org.sonar.db.component.BranchType;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.db.user.UserDto;
import org.sonar.server.es.ProjectIndexer;
import org.sonar.server.es.TestProjectIndexers;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.favorite.FavoriteUpdater;
import org.sonar.server.i18n.I18nRule;
import org.sonar.server.permission.PermissionTemplateService;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.sonar.api.resources.Qualifiers.APP;
import static org.sonar.api.resources.Qualifiers.VIEW;

public class ComponentUpdaterTest {

  private static final String DEFAULT_PROJECT_KEY = "project-key";
  private static final String DEFAULT_PROJECT_NAME = "project-name";

  private System2 system2 = System2.INSTANCE;

  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public DbTester db = DbTester.create(system2);
  @Rule
  public I18nRule i18n = new I18nRule().put("qualifier.TRK", "Project");

  private TestProjectIndexers projectIndexers = new TestProjectIndexers();
  private PermissionTemplateService permissionTemplateService = mock(PermissionTemplateService.class);

  private ComponentUpdater underTest = new ComponentUpdater(db.getDbClient(), i18n, system2,
    permissionTemplateService,
    new FavoriteUpdater(db.getDbClient()),
    projectIndexers);

  @Test
  public void persist_and_index_when_creating_project() throws Exception {
    NewComponent project = NewComponent.newComponentBuilder()
      .setKey(DEFAULT_PROJECT_KEY)
      .setName(DEFAULT_PROJECT_NAME)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .setPrivate(true)
      .build();
    ComponentDto returned = underTest.create(db.getSession(), project, null);

    ComponentDto loaded = db.getDbClient().componentDao().selectOrFailByUuid(db.getSession(), returned.uuid());
    assertThat(loaded.getDbKey()).isEqualTo(DEFAULT_PROJECT_KEY);
    assertThat(loaded.deprecatedKey()).isEqualTo(DEFAULT_PROJECT_KEY);
    assertThat(loaded.name()).isEqualTo(DEFAULT_PROJECT_NAME);
    assertThat(loaded.longName()).isEqualTo(DEFAULT_PROJECT_NAME);
    assertThat(loaded.qualifier()).isEqualTo(Qualifiers.PROJECT);
    assertThat(loaded.scope()).isEqualTo(Scopes.PROJECT);
    assertThat(loaded.getOrganizationUuid()).isEqualTo(db.getDefaultOrganization().getUuid());
    assertThat(loaded.uuid()).isNotNull();
    assertThat(loaded.projectUuid()).isEqualTo(loaded.uuid());
    assertThat(loaded.moduleUuid()).isNull();
    assertThat(loaded.moduleUuidPath()).isEqualTo("." + loaded.uuid() + ".");
    assertThat(loaded.isPrivate()).isEqualTo(project.isPrivate());
    assertThat(loaded.getCreatedAt()).isNotNull();
    assertThat(db.getDbClient().componentDao().selectOrFailByKey(db.getSession(), DEFAULT_PROJECT_KEY)).isNotNull();

    assertThat(projectIndexers.hasBeenCalled(loaded.uuid(), ProjectIndexer.Cause.PROJECT_CREATION)).isTrue();

    Optional<BranchDto> branch = db.getDbClient().branchDao().selectByUuid(db.getSession(), returned.uuid());
    assertThat(branch).isPresent();
    assertThat(branch.get().getKey()).isEqualTo(BranchDto.DEFAULT_MAIN_BRANCH_NAME);
    assertThat(branch.get().getMergeBranchUuid()).isNull();
    assertThat(branch.get().getBranchType()).isEqualTo(BranchType.LONG);
    assertThat(branch.get().getUuid()).isEqualTo(returned.uuid());
    assertThat(branch.get().getProjectUuid()).isEqualTo(returned.uuid());
  }

  @Test
  public void persist_private_flag_true_when_creating_project() throws Exception {
    OrganizationDto organization = db.organizations().insert();
    NewComponent project = NewComponent.newComponentBuilder()
      .setKey(DEFAULT_PROJECT_KEY)
      .setName(DEFAULT_PROJECT_NAME)
      .setOrganizationUuid(organization.getUuid())
      .setPrivate(true)
      .build();
    ComponentDto returned = underTest.create(db.getSession(), project, null);
    ComponentDto loaded = db.getDbClient().componentDao().selectOrFailByUuid(db.getSession(), returned.uuid());
    assertThat(loaded.isPrivate()).isEqualTo(project.isPrivate());
  }

  @Test
  public void persist_private_flag_false_when_creating_project() throws Exception {
    OrganizationDto organization = db.organizations().insert();
    NewComponent project = NewComponent.newComponentBuilder()
      .setKey(DEFAULT_PROJECT_KEY)
      .setName(DEFAULT_PROJECT_NAME)
      .setOrganizationUuid(organization.getUuid())
      .setPrivate(false)
      .build();
    ComponentDto returned = underTest.create(db.getSession(), project, null);
    ComponentDto loaded = db.getDbClient().componentDao().selectOrFailByUuid(db.getSession(), returned.uuid());
    assertThat(loaded.isPrivate()).isEqualTo(project.isPrivate());
  }

  @Test
  public void create_project_with_branch() throws Exception {
    ComponentDto project = underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(DEFAULT_PROJECT_KEY)
        .setName(DEFAULT_PROJECT_NAME)
        .setBranch("origin/master")
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);

    assertThat(project.getDbKey()).isEqualTo("project-key:origin/master");
  }

  @Test
  public void persist_and_index_when_creating_view() {
    NewComponent view = NewComponent.newComponentBuilder()
      .setKey("view-key")
      .setName("view-name")
      .setQualifier(VIEW)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .build();

    ComponentDto returned = underTest.create(db.getSession(), view, null);

    ComponentDto loaded = db.getDbClient().componentDao().selectOrFailByUuid(db.getSession(), returned.uuid());
    assertThat(loaded.getDbKey()).isEqualTo("view-key");
    assertThat(loaded.name()).isEqualTo("view-name");
    assertThat(loaded.qualifier()).isEqualTo("VW");
    assertThat(projectIndexers.hasBeenCalled(loaded.uuid(), ProjectIndexer.Cause.PROJECT_CREATION)).isTrue();
    Optional<BranchDto> branch = db.getDbClient().branchDao().selectByUuid(db.getSession(), returned.uuid());
    assertThat(branch).isNotPresent();
  }

  @Test
  public void persist_and_index_when_creating_application() {
    NewComponent view = NewComponent.newComponentBuilder()
      .setKey("app-key")
      .setName("app-name")
      .setQualifier(APP)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .build();

    ComponentDto returned = underTest.create(db.getSession(), view, null);

    ComponentDto loaded = db.getDbClient().componentDao().selectOrFailByUuid(db.getSession(), returned.uuid());
    assertThat(loaded.getDbKey()).isEqualTo("app-key");
    assertThat(loaded.name()).isEqualTo("app-name");
    assertThat(loaded.qualifier()).isEqualTo("APP");
    assertThat(projectIndexers.hasBeenCalled(loaded.uuid(), ProjectIndexer.Cause.PROJECT_CREATION)).isTrue();
    Optional<BranchDto> branch = db.getDbClient().branchDao().selectByUuid(db.getSession(), returned.uuid());
    assertThat(branch).isNotPresent();
  }

  @Test
  public void create_application() {
    NewComponent view = NewComponent.newComponentBuilder()
      .setKey("app-key")
      .setName("app-name")
      .setQualifier(APP)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .build();

    ComponentDto returned = underTest.create(db.getSession(), view, null);

    ComponentDto loaded = db.getDbClient().componentDao().selectByKey(db.getSession(), returned.getDbKey()).get();
    assertThat(loaded.getDbKey()).isEqualTo("app-key");
    assertThat(loaded.name()).isEqualTo("app-name");
    assertThat(loaded.qualifier()).isEqualTo("APP");
    assertThat(projectIndexers.hasBeenCalled(loaded.uuid(), ProjectIndexer.Cause.PROJECT_CREATION)).isTrue();
  }

  @Test
  public void apply_default_permission_template() throws Exception {
    int userId = 42;
    NewComponent project = NewComponent.newComponentBuilder()
      .setKey(DEFAULT_PROJECT_KEY)
      .setName(DEFAULT_PROJECT_NAME)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .build();
    ComponentDto dto = underTest.create(db.getSession(), project, userId);

    verify(permissionTemplateService).applyDefault(db.getSession(), dto.getOrganizationUuid(), dto, userId);
  }

  @Test
  public void add_project_to_user_favorites_if_project_creator_is_defined_in_permission_template() throws Exception {
    UserDto userDto = db.users().insertUser();
    NewComponent project = NewComponent.newComponentBuilder()
      .setKey(DEFAULT_PROJECT_KEY)
      .setName(DEFAULT_PROJECT_NAME)
      .setOrganizationUuid(db.getDefaultOrganization().getUuid())
      .build();
    when(permissionTemplateService.hasDefaultTemplateWithPermissionOnProjectCreator(eq(db.getSession()), eq(project.getOrganizationUuid()), any(ComponentDto.class)))
      .thenReturn(true);

    ComponentDto dto = underTest.create(db.getSession(),
      project,
      userDto.getId());

    assertThat(db.favorites().hasFavorite(dto, userDto.getId())).isTrue();
  }

  @Test
  public void does_not_add_project_to_favorite_when_anonymously_created() throws Exception {
    ComponentDto project = underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(DEFAULT_PROJECT_KEY)
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);

    assertThat(db.favorites().hasNoFavorite(project)).isTrue();
  }

  @Test
  public void does_not_add_project_to_favorite_when_project_has_no_permission_on_template() throws Exception {
    ComponentDto project = underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(DEFAULT_PROJECT_KEY)
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);

    assertThat(db.favorites().hasNoFavorite(project)).isTrue();
  }

  @Test
  public void fail_when_project_key_already_exists() throws Exception {
    ComponentDto existing = db.components().insertPrivateProject();

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Could not create Project, key already exists: " + existing.getDbKey());

    underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(existing.getDbKey())
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(existing.getOrganizationUuid())
        .build(),
      null);
  }

  @Test
  public void fail_when_project_key_already_exists_on_other_organization() throws Exception {
    ComponentDto existing = db.components().insertPrivateProject(db.organizations().insert());

    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Could not create Project, key already exists: " + existing.getDbKey());

    underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(existing.getDbKey())
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(existing.getOrganizationUuid())
        .build(),
      null);
  }

  @Test
  public void fail_when_key_has_bad_format() throws Exception {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Malformed key for Project: 1234");

    underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey("1234")
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);
  }

  @Test
  public void properly_fail_when_key_contains_percent_character() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Malformed key for Project: project%Key");

    underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey("project%Key")
        .setName(DEFAULT_PROJECT_NAME)
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);
  }

  @Test
  public void fail_to_create_new_component_on_invalid_branch() {
    expectedException.expect(BadRequestException.class);
    expectedException.expectMessage("Malformed branch for Project: origin?branch. Allowed characters are alphanumeric, '-', '_', '.' and '/', with at least one non-digit.");

    underTest.create(db.getSession(),
      NewComponent.newComponentBuilder()
        .setKey(DEFAULT_PROJECT_KEY)
        .setName(DEFAULT_PROJECT_NAME)
        .setBranch("origin?branch")
        .setOrganizationUuid(db.getDefaultOrganization().getUuid())
        .build(),
      null);
  }
}
