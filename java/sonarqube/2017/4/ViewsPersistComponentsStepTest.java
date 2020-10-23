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
package org.sonar.server.computation.task.projectanalysis.step;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Random;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.Scopes;
import org.sonar.api.utils.System2;
import org.sonar.db.DbClient;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTesting;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.server.computation.task.projectanalysis.analysis.AnalysisMetadataHolderRule;
import org.sonar.server.computation.task.projectanalysis.component.MutableDbIdsRepositoryRule;
import org.sonar.server.computation.task.projectanalysis.component.MutableDisabledComponentsHolder;
import org.sonar.server.computation.task.projectanalysis.component.ProjectViewAttributes;
import org.sonar.server.computation.task.projectanalysis.component.SubViewAttributes;
import org.sonar.server.computation.task.projectanalysis.component.TreeRootHolderRule;
import org.sonar.server.computation.task.projectanalysis.component.ViewsComponent;
import org.sonar.server.computation.task.step.ComputationStep;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.RETURNS_DEEP_STUBS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.db.component.ComponentTesting.newProjectCopy;
import static org.sonar.db.component.ComponentTesting.newPrivateProjectDto;
import static org.sonar.db.component.ComponentTesting.newSubView;
import static org.sonar.server.computation.task.projectanalysis.component.Component.Type.PROJECT_VIEW;
import static org.sonar.server.computation.task.projectanalysis.component.Component.Type.SUBVIEW;
import static org.sonar.server.computation.task.projectanalysis.component.Component.Type.VIEW;
import static org.sonar.server.computation.task.projectanalysis.component.ViewsComponent.builder;

public class ViewsPersistComponentsStepTest extends BaseStepTest {

  private static final SimpleDateFormat DATE_FORMAT = new SimpleDateFormat("yyyy-MM-dd");

  private static final String VIEW_KEY = "VIEW_KEY";
  private static final String VIEW_NAME = "VIEW_NAME";
  private static final String VIEW_DESCRIPTION = "view description";
  private static final String VIEW_UUID = "VIEW_UUID";
  private static final String SUBVIEW_1_KEY = "SUBVIEW_1_KEY";
  private static final String SUBVIEW_1_NAME = "SUBVIEW_1_NAME";
  private static final String SUBVIEW_1_DESCRIPTION = "subview 1 description";
  private static final String SUBVIEW_1_UUID = "SUBVIEW_1_UUID";
  private static final String PROJECT_VIEW_1_KEY = "PV1_KEY";
  private static final String PROJECT_VIEW_1_NAME = "PV1_NAME";
  private static final String PROJECT_VIEW_1_UUID = "PV1_UUID";
  private static final String ORGANIZATION_UUID = "org1";

  @Rule
  public DbTester dbTester = DbTester.create(System2.INSTANCE);
  @Rule
  public TreeRootHolderRule treeRootHolder = new TreeRootHolderRule();
  @Rule
  public MutableDbIdsRepositoryRule dbIdsRepository = MutableDbIdsRepositoryRule.create(treeRootHolder);
  @Rule
  public AnalysisMetadataHolderRule analysisMetadataHolder = new AnalysisMetadataHolderRule()
    .setOrganizationUuid(ORGANIZATION_UUID);

  private System2 system2 = mock(System2.class);
  private DbClient dbClient = dbTester.getDbClient();
  private Date now;
  private ComponentDbTester componentDbTester = new ComponentDbTester(dbTester);
  private MutableDisabledComponentsHolder disabledComponentsHolder = mock(MutableDisabledComponentsHolder.class, RETURNS_DEEP_STUBS);
  private PersistComponentsStep underTest;

  @Before
  public void setup() throws Exception {
    now = DATE_FORMAT.parse("2015-06-02");
    when(system2.now()).thenReturn(now.getTime());

    dbTester.organizations().insertForUuid(ORGANIZATION_UUID);
    underTest = new PersistComponentsStep(dbClient, treeRootHolder, dbIdsRepository, system2, disabledComponentsHolder, analysisMetadataHolder);
  }

  @Override
  protected ComputationStep step() {
    return underTest;
  }

  @Test
  public void persist_empty_view() {
    treeRootHolder.setRoot(createViewBuilder().build());

    underTest.execute();

    assertRowsCountInTableProjects(1);

    ComponentDto projectDto = getComponentFromDb(VIEW_KEY);
    assertDtoIsView(projectDto);
  }

  @Test
  public void persist_existing_empty_view() {
    // most of the time view already exists since its supposed to be created when config is uploaded
    persistComponents(newViewDto(dbTester.organizations().insert()));

    treeRootHolder.setRoot(createViewBuilder().build());

    underTest.execute();

    assertRowsCountInTableProjects(1);

    assertDtoNotUpdated(VIEW_KEY);
  }

  @Test
  public void persist_view_with_projectView() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(dbTester.organizations().insert());
    persistComponents(project);

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(createProjectView1Builder(project, null).build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(3);

    ComponentDto viewDto = getComponentFromDb(VIEW_KEY);
    assertDtoIsView(viewDto);

    ComponentDto pv1Dto = getComponentFromDb(PROJECT_VIEW_1_KEY);
    assertDtoIsProjectView1(pv1Dto, viewDto, viewDto, project);
  }

  @Test
  public void persist_empty_subview() {
    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null).build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(2);

    ComponentDto viewDto = getComponentFromDb(VIEW_KEY);
    assertDtoIsView(viewDto);

    ComponentDto sv1Dto = getComponentFromDb(SUBVIEW_1_KEY);
    assertDtoIsSubView1(viewDto, sv1Dto);
  }

  @Test
  public void persist_empty_subview_having_original_view_uuid() {
    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder("ORIGINAL_UUID").build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(2);

    ComponentDto subView = getComponentFromDb(SUBVIEW_1_KEY);
    assertThat(subView.getCopyResourceUuid()).isEqualTo("ORIGINAL_UUID");
  }

  @Test
  public void persist_existing_empty_subview_under_existing_view() {
    ComponentDto viewDto = newViewDto(dbTester.organizations().insert());
    persistComponents(viewDto);
    persistComponents(ComponentTesting.newSubView(viewDto, SUBVIEW_1_UUID, SUBVIEW_1_KEY).setName(SUBVIEW_1_NAME));

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null).build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(2);

    assertDtoNotUpdated(VIEW_KEY);
    assertDtoNotUpdated(SUBVIEW_1_KEY);
  }

  @Test
  public void persist_empty_subview_under_existing_view() {
    persistComponents(newViewDto(dbTester.organizations().insert()));

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null).build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(2);

    assertDtoNotUpdated(VIEW_KEY);
    assertDtoIsSubView1(getComponentFromDb(VIEW_KEY), getComponentFromDb(SUBVIEW_1_KEY));
  }

  @Test
  public void persist_project_view_under_subview() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(dbTester.organizations().insert());
    persistComponents(project);

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null)
            .addChildren(
              createProjectView1Builder(project, null).build())
            .build())
        .build());

    underTest.execute();

    assertRowsCountInTableProjects(4);

    ComponentDto viewDto = getComponentFromDb(VIEW_KEY);
    assertDtoIsView(viewDto);
    ComponentDto subView1Dto = getComponentFromDb(SUBVIEW_1_KEY);
    assertDtoIsSubView1(viewDto, subView1Dto);
    ComponentDto pv1Dto = getComponentFromDb(PROJECT_VIEW_1_KEY);
    assertDtoIsProjectView1(pv1Dto, viewDto, subView1Dto, project);
  }

  @Test
  public void update_view_name_and_longName() {
    ComponentDto viewDto = newViewDto(dbTester.organizations().insert()).setLongName("another long name").setCreatedAt(now);
    persistComponents(viewDto);

    treeRootHolder.setRoot(createViewBuilder().build());

    underTest.execute();

    // commit functional transaction -> copies B-fields to A-fields
    dbClient.componentDao().applyBChangesForRootComponentUuid(dbTester.getSession(), viewDto.uuid());
    dbTester.commit();

    assertRowsCountInTableProjects(1);
    ComponentDto newViewDto = getComponentFromDb(VIEW_KEY);
    assertDtoIsView(newViewDto);
  }

  @Test
  public void update_project_view() {
    OrganizationDto organizationDto = dbTester.organizations().insert();
    ComponentDto view = newViewDto(organizationDto);
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto);
    persistComponents(view, project);
    ComponentDto projectView = ComponentTesting.newProjectCopy(PROJECT_VIEW_1_UUID, project, view)
      .setOrganizationUuid(ORGANIZATION_UUID)
      .setKey(PROJECT_VIEW_1_KEY)
      .setName("Old name")
      .setCreatedAt(now);
    persistComponents(projectView);

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(createProjectView1Builder(project, null).build())
        .build());

    underTest.execute();

    // commit functional transaction -> copies B-fields to A-fields
    dbClient.componentDao().applyBChangesForRootComponentUuid(dbTester.getSession(), view.uuid());
    dbTester.commit();

    assertRowsCountInTableProjects(3);
    ComponentDto pv1Dto = getComponentFromDb(PROJECT_VIEW_1_KEY);
    assertDtoIsProjectView1(pv1Dto, view, view, project);
  }

  @Test
  public void update_copy_component_uuid_of_project_view() {
    OrganizationDto organizationDto = dbTester.organizations().insert();
    ComponentDto view = newViewDto(organizationDto);
    ComponentDto project1 = newPrivateProjectDto(organizationDto, "P1");
    ComponentDto project2 = newPrivateProjectDto(organizationDto, "P2");
    persistComponents(view, project1, project2);

    // Project view in DB is associated to project1
    ComponentDto projectView = ComponentTesting.newProjectCopy(PROJECT_VIEW_1_UUID, project1, view)
      .setKey(PROJECT_VIEW_1_KEY)
      .setCreatedAt(now);
    persistComponents(projectView);

    treeRootHolder.setRoot(
      createViewBuilder()
        // Project view in the View is linked to the first project2
        .addChildren(createProjectView1Builder(project2, null).build())
        .build());

    underTest.execute();

    // commit functional transaction -> copies B-fields to A-fields
    dbClient.componentDao().applyBChangesForRootComponentUuid(dbTester.getSession(), view.uuid());
    dbTester.commit();

    ComponentDto pv1Dto = getComponentFromDb(PROJECT_VIEW_1_KEY);
    // Project view should now be linked to project2
    assertDtoIsProjectView1(pv1Dto, view, view, project2);
  }

  @Test
  public void update_copy_component_uuid_of_sub_view() {
    OrganizationDto organizationDto = dbTester.organizations().insert();
    ComponentDto view = newViewDto(organizationDto);
    ComponentDto subView = newSubViewDto(view).setCopyComponentUuid("OLD_COPY");
    persistComponents(view, subView);

    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder("NEW_COPY").build())
        .build());

    underTest.execute();

    // commit functional transaction -> copies B-fields to A-fields
    dbClient.componentDao().applyBChangesForRootComponentUuid(dbTester.getSession(), view.uuid());
    dbTester.commit();

    ComponentDto subViewReloaded = getComponentFromDb(SUBVIEW_1_KEY);
    assertThat(subViewReloaded.getCopyResourceUuid()).isEqualTo("NEW_COPY");
  }

  @Test
  public void persists_new_components_as_public_if_root_does_not_exist_yet_out_of_functional_transaction() {
    ComponentDto project = dbTester.components().insertComponent(ComponentTesting.newPrivateProjectDto(dbTester.organizations().insert()));
    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null)
            .addChildren(
              createProjectView1Builder(project, null).build())
            .build())
        .build());

    underTest.execute();

    Stream.of(VIEW_UUID, SUBVIEW_1_UUID, PROJECT_VIEW_1_UUID)
      .forEach(uuid -> assertThat(dbClient.componentDao().selectByUuid(dbTester.getSession(), uuid).get().isPrivate()).isFalse());
  }

  @Test
  public void persists_new_components_with_visibility_of_root_in_db_out_of_functional_transaction() {
    boolean isRootPrivate = new Random().nextBoolean();
    ComponentDto project = dbTester.components().insertComponent(ComponentTesting.newPrivateProjectDto(dbTester.organizations().insert()));
    OrganizationDto organization = dbTester.organizations().insert();
    ComponentDto view = newViewDto(organization).setUuid(VIEW_UUID).setKey(VIEW_KEY).setName("View").setPrivate(isRootPrivate);
    dbTester.components().insertComponent(view);
    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null)
            .addChildren(
              createProjectView1Builder(project, null).build())
            .build())
        .build());

    underTest.execute();

    Stream.of(VIEW_UUID, SUBVIEW_1_UUID, PROJECT_VIEW_1_UUID)
      .forEach(uuid -> assertThat(dbClient.componentDao().selectByUuid(dbTester.getSession(), uuid).get().isPrivate())
        .describedAs("for uuid " + uuid)
        .isEqualTo(isRootPrivate));
  }

  @Test
  public void persists_existing_components_with_visibility_of_root_in_db_out_of_functional_transaction() {
    boolean isRootPrivate = new Random().nextBoolean();
    ComponentDto project = dbTester.components().insertComponent(ComponentTesting.newPrivateProjectDto(dbTester.organizations().insert()));
    OrganizationDto organization = dbTester.organizations().insert();
    ComponentDto view = newViewDto(organization).setUuid(VIEW_UUID).setKey(VIEW_KEY).setName("View").setPrivate(isRootPrivate);
    dbTester.components().insertComponent(view);
    ComponentDto subView = newSubView(view).setUuid("BCDE").setKey("MODULE").setPrivate(!isRootPrivate);
    dbTester.components().insertComponent(subView);
    dbTester.components().insertComponent(newProjectCopy("DEFG", project, view).setKey("DIR").setPrivate(isRootPrivate));
    treeRootHolder.setRoot(
      createViewBuilder()
        .addChildren(
          createSubView1Builder(null)
            .addChildren(
              createProjectView1Builder(project, null).build())
            .build())
        .build());

    underTest.execute();

    Stream.of(VIEW_UUID, SUBVIEW_1_UUID, PROJECT_VIEW_1_UUID, subView.uuid(), "DEFG")
      .forEach(uuid -> assertThat(dbClient.componentDao().selectByUuid(dbTester.getSession(), uuid).get().isPrivate())
        .describedAs("for uuid " + uuid)
        .isEqualTo(isRootPrivate));
  }

  private static ViewsComponent.Builder createViewBuilder() {
    return builder(VIEW, VIEW_KEY)
      .setUuid(VIEW_UUID)
      .setName(VIEW_NAME)
      .setDescription(VIEW_DESCRIPTION);
  }

  private ViewsComponent.Builder createSubView1Builder(@Nullable String originalViewUuid) {
    return builder(SUBVIEW, SUBVIEW_1_KEY)
      .setUuid(SUBVIEW_1_UUID)
      .setName(SUBVIEW_1_NAME)
      .setDescription(SUBVIEW_1_DESCRIPTION)
      .setSubViewAttributes(new SubViewAttributes(originalViewUuid));
  }

  private static ViewsComponent.Builder createProjectView1Builder(ComponentDto project, Long analysisDate) {
    return builder(PROJECT_VIEW, PROJECT_VIEW_1_KEY)
      .setUuid(PROJECT_VIEW_1_UUID)
      .setName(PROJECT_VIEW_1_NAME)
      .setDescription("project view description is not persisted")
      .setProjectViewAttributes(new ProjectViewAttributes(project.uuid(), analysisDate));
  }

  private void persistComponents(ComponentDto... componentDtos) {
    componentDbTester.insertComponents(componentDtos);
  }

  private ComponentDto getComponentFromDb(String componentKey) {
    return dbClient.componentDao().selectByKey(dbTester.getSession(), componentKey).get();
  }

  private void assertRowsCountInTableProjects(int rowCount) {
    assertThat(dbTester.countRowsOfTable("projects")).isEqualTo(rowCount);
  }

  private void assertDtoNotUpdated(String componentKey) {
    assertThat(getComponentFromDb(componentKey).getCreatedAt()).isNotEqualTo(now);
  }

  private ComponentDto newViewDto(OrganizationDto organizationDto) {
    return ComponentTesting.newView(organizationDto, VIEW_UUID)
      .setOrganizationUuid(ORGANIZATION_UUID)
      .setKey(VIEW_KEY)
      .setName(VIEW_NAME);
  }

  private ComponentDto newSubViewDto(ComponentDto rootView) {
    return ComponentTesting.newSubView(rootView, SUBVIEW_1_UUID, SUBVIEW_1_KEY)
      .setName(SUBVIEW_1_NAME);
  }

  /**
   * Assertions to verify the DTO created from {@link #createViewBuilder()}
   */
  private void assertDtoIsView(ComponentDto projectDto) {
    assertThat(projectDto.getOrganizationUuid()).isEqualTo(ORGANIZATION_UUID);
    assertThat(projectDto.name()).isEqualTo(VIEW_NAME);
    assertThat(projectDto.longName()).isEqualTo(VIEW_NAME);
    assertThat(projectDto.description()).isEqualTo(VIEW_DESCRIPTION);
    assertThat(projectDto.path()).isNull();
    assertThat(projectDto.uuid()).isEqualTo(VIEW_UUID);
    assertThat(projectDto.projectUuid()).isEqualTo(VIEW_UUID);
    assertThat(projectDto.getRootUuid()).isEqualTo(VIEW_UUID);
    assertThat(projectDto.moduleUuid()).isNull();
    assertThat(projectDto.moduleUuidPath()).isEqualTo("." + projectDto.uuid() + ".");
    assertThat(projectDto.qualifier()).isEqualTo(Qualifiers.VIEW);
    assertThat(projectDto.scope()).isEqualTo(Scopes.PROJECT);
    assertThat(projectDto.getCopyResourceUuid()).isNull();
    assertThat(projectDto.getCreatedAt()).isEqualTo(now);
  }

  /**
   * Assertions to verify the DTO created from {@link #createProjectView1Builder(ComponentDto, Long)}
   */
  private void assertDtoIsSubView1(ComponentDto viewDto, ComponentDto sv1Dto) {
    assertThat(sv1Dto.getOrganizationUuid()).isEqualTo(ORGANIZATION_UUID);
    assertThat(sv1Dto.name()).isEqualTo(SUBVIEW_1_NAME);
    assertThat(sv1Dto.longName()).isEqualTo(SUBVIEW_1_NAME);
    assertThat(sv1Dto.description()).isEqualTo(SUBVIEW_1_DESCRIPTION);
    assertThat(sv1Dto.path()).isNull();
    assertThat(sv1Dto.uuid()).isEqualTo(SUBVIEW_1_UUID);
    assertThat(sv1Dto.projectUuid()).isEqualTo(viewDto.uuid());
    assertThat(sv1Dto.getRootUuid()).isEqualTo(viewDto.uuid());
    assertThat(sv1Dto.moduleUuid()).isEqualTo(viewDto.uuid());
    assertThat(sv1Dto.moduleUuidPath()).isEqualTo(viewDto.moduleUuidPath() + sv1Dto.uuid() + ".");
    assertThat(sv1Dto.qualifier()).isEqualTo(Qualifiers.SUBVIEW);
    assertThat(sv1Dto.scope()).isEqualTo(Scopes.PROJECT);
    assertThat(sv1Dto.getCopyResourceUuid()).isNull();
    assertThat(sv1Dto.getCreatedAt()).isEqualTo(now);
  }

  private void assertDtoIsProjectView1(ComponentDto pv1Dto, ComponentDto viewDto, ComponentDto parentViewDto, ComponentDto project) {
    assertThat(pv1Dto.getOrganizationUuid()).isEqualTo(ORGANIZATION_UUID);
    assertThat(pv1Dto.name()).isEqualTo(PROJECT_VIEW_1_NAME);
    assertThat(pv1Dto.longName()).isEqualTo(PROJECT_VIEW_1_NAME);
    assertThat(pv1Dto.description()).isNull();
    assertThat(pv1Dto.path()).isNull();
    assertThat(pv1Dto.uuid()).isEqualTo(PROJECT_VIEW_1_UUID);
    assertThat(pv1Dto.projectUuid()).isEqualTo(viewDto.uuid());
    assertThat(pv1Dto.getRootUuid()).isEqualTo(viewDto.uuid());
    assertThat(pv1Dto.moduleUuid()).isEqualTo(parentViewDto.uuid());
    assertThat(pv1Dto.moduleUuidPath()).isEqualTo(parentViewDto.moduleUuidPath() + pv1Dto.uuid() + ".");
    assertThat(pv1Dto.qualifier()).isEqualTo(Qualifiers.PROJECT);
    assertThat(pv1Dto.scope()).isEqualTo(Scopes.FILE);
    assertThat(pv1Dto.getCopyResourceUuid()).isEqualTo(project.uuid());
    assertThat(pv1Dto.getCreatedAt()).isEqualTo(now);
  }

}
