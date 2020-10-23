/*
 * SonarQube, open source software quality management tool.
 * Copyright (C) 2008-2014 SonarSource
 * mailto:contact AT sonarsource DOT com
 *
 * SonarQube is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * SonarQube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.batch.scan;

import com.google.common.collect.HashBasedTable;
import com.google.common.collect.ImmutableTable;
import com.google.common.collect.Table;
import java.util.Collections;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.CoreProperties;
import org.sonar.api.batch.bootstrap.ProjectDefinition;
import org.sonar.api.batch.bootstrap.ProjectReactor;
import org.sonar.api.config.PropertyDefinitions;
import org.sonar.api.utils.MessageException;
import org.sonar.api.utils.log.LogTester;
import org.sonar.batch.analysis.DefaultAnalysisMode;
import org.sonar.batch.bootstrap.GlobalMode;
import org.sonar.batch.bootstrap.GlobalProperties;
import org.sonar.batch.bootstrap.GlobalSettings;
import org.sonar.batch.protocol.input.GlobalRepositories;
import org.sonar.batch.repository.FileData;
import org.sonar.batch.repository.ProjectRepositories;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class ProjectSettingsTest {

  @Rule
  public ExpectedException thrown = ExpectedException.none();
  @Rule
  public LogTester logTester = new LogTester();

  private ProjectRepositories projectRef;
  private ProjectDefinition project;
  private GlobalSettings bootstrapProps;
  private Table<String, String, FileData> emptyFileData;
  private Table<String, String, String> emptySettings;

  private GlobalMode globalMode;
  private DefaultAnalysisMode mode;

  @Before
  public void prepare() {
    emptyFileData = ImmutableTable.of();
    emptySettings = ImmutableTable.of();
    project = ProjectDefinition.create().setKey("struts");
    globalMode = mock(GlobalMode.class);
    mode = mock(DefaultAnalysisMode.class);
    bootstrapProps = new GlobalSettings(new GlobalProperties(Collections.<String, String>emptyMap()), new PropertyDefinitions(), new GlobalRepositories(), globalMode);
  }

  @Test
  public void should_load_project_props() {
    project.setProperty("project.prop", "project");

    projectRef = new ProjectRepositories(emptySettings, emptyFileData, null);
    ProjectSettings batchSettings = new ProjectSettings(new ProjectReactor(project), bootstrapProps, new PropertyDefinitions(), projectRef, mode);

    assertThat(batchSettings.getString("project.prop")).isEqualTo("project");
  }

  @Test
  public void should_load_project_root_settings() {
    Table<String, String, String> settings = HashBasedTable.create();
    settings.put("struts", "sonar.cpd.cross", "true");
    settings.put("struts", "sonar.java.coveragePlugin", "jacoco");

    projectRef = new ProjectRepositories(settings, emptyFileData, null);
    ProjectSettings batchSettings = new ProjectSettings(new ProjectReactor(project), bootstrapProps, new PropertyDefinitions(), projectRef, mode);
    assertThat(batchSettings.getString("sonar.java.coveragePlugin")).isEqualTo("jacoco");
  }

  @Test
  public void should_load_project_root_settings_on_branch() {
    project.setProperty(CoreProperties.PROJECT_BRANCH_PROPERTY, "mybranch");

    Table<String, String, String> settings = HashBasedTable.create();
    settings.put("struts:mybranch", "sonar.cpd.cross", "true");
    settings.put("struts:mybranch", "sonar.java.coveragePlugin", "jacoco");

    projectRef = new ProjectRepositories(settings, emptyFileData, null);

    ProjectSettings batchSettings = new ProjectSettings(new ProjectReactor(project), bootstrapProps, new PropertyDefinitions(), projectRef, mode);

    assertThat(batchSettings.getString("sonar.java.coveragePlugin")).isEqualTo("jacoco");
  }

  @Test
  public void should_not_fail_when_accessing_secured_properties() {
    Table<String, String, String> settings = HashBasedTable.create();
    settings.put("struts", "sonar.foo.secured", "bar");
    settings.put("struts", "sonar.foo.license.secured", "bar2");

    projectRef = new ProjectRepositories(settings, emptyFileData, null);
    ProjectSettings batchSettings = new ProjectSettings(new ProjectReactor(project), bootstrapProps, new PropertyDefinitions(), projectRef, mode);

    assertThat(batchSettings.getString("sonar.foo.license.secured")).isEqualTo("bar2");
    assertThat(batchSettings.getString("sonar.foo.secured")).isEqualTo("bar");
  }

  @Test
  public void should_fail_when_accessing_secured_properties_in_issues_mode() {
    Table<String, String, String> settings = HashBasedTable.create();
    settings.put("struts", "sonar.foo.secured", "bar");
    settings.put("struts", "sonar.foo.license.secured", "bar2");

    when(mode.isIssues()).thenReturn(true);

    projectRef = new ProjectRepositories(settings, emptyFileData, null);
    ProjectSettings batchSettings = new ProjectSettings(new ProjectReactor(project), bootstrapProps, new PropertyDefinitions(), projectRef, mode);

    assertThat(batchSettings.getString("sonar.foo.license.secured")).isEqualTo("bar2");
    thrown.expect(MessageException.class);
    thrown
      .expectMessage(
        "Access to the secured property 'sonar.foo.secured' is not possible in issues mode. The SonarQube plugin which requires this property must be deactivated in issues mode.");
    batchSettings.getString("sonar.foo.secured");
  }

}
