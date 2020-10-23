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
package org.sonar.scanner.scan;

import com.google.common.collect.Lists;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.sonar.api.batch.bootstrap.ProjectDefinition;
import org.sonar.api.config.Settings;
import org.sonar.api.utils.MessageException;
import org.sonar.scanner.analysis.DefaultAnalysisMode;
import org.sonar.scanner.bootstrap.GlobalSettings;
import org.sonar.scanner.report.AnalysisContextReportPublisher;
import org.sonar.scanner.repository.ProjectRepositories;

/**
 * @since 2.12
 */
public class ModuleSettings extends Settings {

  private final ProjectRepositories projectRepos;
  private final DefaultAnalysisMode analysisMode;
  private final Map<String, String> properties = new HashMap<>();

  public ModuleSettings(GlobalSettings batchSettings, ProjectDefinition moduleDefinition, ProjectRepositories projectSettingsRepo,
    DefaultAnalysisMode analysisMode, AnalysisContextReportPublisher contextReportPublisher) {
    super(batchSettings.getDefinitions(), batchSettings.getEncryption());
    this.projectRepos = projectSettingsRepo;
    this.analysisMode = analysisMode;

    init(moduleDefinition, batchSettings);
    contextReportPublisher.dumpModuleSettings(moduleDefinition);
  }

  private ModuleSettings init(ProjectDefinition moduleDefinition, GlobalSettings batchSettings) {
    addProjectProperties(moduleDefinition, batchSettings);
    addBuildProperties(moduleDefinition);
    return this;
  }

  private void addProjectProperties(ProjectDefinition def, GlobalSettings batchSettings) {
    addProperties(batchSettings.getProperties());
    do {
      if (projectRepos.moduleExists(def.getKeyWithBranch())) {
        addProperties(projectRepos.settings(def.getKeyWithBranch()));
        break;
      }
      def = def.getParent();
    } while (def != null);
  }

  private void addBuildProperties(ProjectDefinition project) {
    List<ProjectDefinition> orderedProjects = getTopDownParentProjects(project);
    for (ProjectDefinition p : orderedProjects) {
      addProperties(p.properties());
    }
  }

  /**
   * From root to given project
   */
  static List<ProjectDefinition> getTopDownParentProjects(ProjectDefinition project) {
    List<ProjectDefinition> result = Lists.newArrayList();
    ProjectDefinition p = project;
    while (p != null) {
      result.add(0, p);
      p = p.getParent();
    }
    return result;
  }

  @Override
  protected Optional<String> get(String key) {
    if (analysisMode.isIssues() && key.endsWith(".secured") && !key.contains(".license")) {
      throw MessageException.of("Access to the secured property '" + key
        + "' is not possible in issues mode. The SonarQube plugin which requires this property must be deactivated in issues mode.");
    }
    return Optional.ofNullable(properties.get(key));
  }

  @Override
  protected void set(String key, String value) {
    properties.put(key, value);
  }

  @Override
  protected void remove(String key) {
    properties.remove(key);
  }

  @Override
  public Map<String, String> getProperties() {
    return properties;
  }
}
