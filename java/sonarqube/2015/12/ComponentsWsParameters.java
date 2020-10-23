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

package org.sonarqube.ws.client.component;

public class ComponentsWsParameters {
  private ComponentsWsParameters() {
    // static utility class
  }

  //actions
  public static final String ACTION_TREE = "tree";

  // parameters
  public static final String PARAM_QUALIFIERS = "qualifiers";
  public static final String PARAM_LANGUAGE = "language";
  public static final String PARAM_BASE_COMPONENT_ID = "baseComponentId";
  public static final String PARAM_BASE_COMPONENT_KEY = "baseComponentKey";
  public static final String PARAM_STRATEGY = "strategy";
}
