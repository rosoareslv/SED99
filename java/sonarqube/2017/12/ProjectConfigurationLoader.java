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
package org.sonar.server.settings;

import java.util.Map;
import java.util.Set;
import org.sonar.api.config.Configuration;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDto;

public interface ProjectConfigurationLoader {
  /**
   * Loads configuration for the specified components.
   *
   * <p>
   * Returns the applicable component configuration with most specific configuration overriding more global ones
   * (eg. global > project > branch).
   *
   * <p>
   * Any component is accepted but SQ only supports specific properties for projects and branches.
   */
  Map<String, Configuration> loadProjectConfigurations(DbSession dbSession, Set<ComponentDto> projects);
}
