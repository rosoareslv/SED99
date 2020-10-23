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
package org.sonar.ce.property;

import java.util.List;
import org.sonar.api.CoreProperties;
import org.sonar.api.PropertyType;
import org.sonar.api.config.PropertyDefinition;
import org.sonar.ce.log.CeLogging;

import static java.util.Arrays.asList;

public class CePropertyDefinitions {
  private CePropertyDefinitions() {
    // only statics
  }

  public static List<PropertyDefinition> all() {
    return asList(
      PropertyDefinition.builder(CeLogging.MAX_LOGS_PROPERTY)
        .name("Compute Engine Log Retention")
        .description("Number of tasks to keep logs for a given project. Once the number of logs exceeds this limit, oldest logs are purged.")
        .type(PropertyType.INTEGER)
        .defaultValue("10")
        .category(CoreProperties.CATEGORY_GENERAL)
        .build());
  }
}
