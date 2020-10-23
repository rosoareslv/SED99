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

package org.sonar.server.component.index;

import org.junit.Before;
import org.junit.Test;
import org.sonar.db.component.ComponentDto;
import org.sonar.server.es.textsearch.ComponentTextSearchFeature;

import static com.google.common.collect.ImmutableSet.of;
import static java.util.Collections.singletonList;
import static org.sonar.api.resources.Qualifiers.PROJECT;

public class ComponentIndexFeatureFavoriteTest extends ComponentIndexTest {

  @Before
  public void before() {
    features.set(ComponentTextSearchFeature.PREFIX, ComponentTextSearchFeature.FAVORITE);
  }

  @Test
  public void scoring_cares_about_favorites() {
    ComponentDto project1 = indexProject("sonarqube", "SonarQube");
    ComponentDto project2 = indexProject("recent", "SonarQube Recently");

    ComponentIndexQuery query1 = ComponentIndexQuery.builder()
      .setQuery("SonarQube")
      .setQualifiers(singletonList(PROJECT))
      .setFavoriteKeys(of(project1.getKey()))
      .build();
    assertSearch(query1).containsExactly(uuids(project1, project2));

    ComponentIndexQuery query2 = ComponentIndexQuery.builder()
      .setQuery("SonarQube")
      .setQualifiers(singletonList(PROJECT))
      .setFavoriteKeys(of(project2.getKey()))
      .build();
    assertSearch(query2).containsExactly(uuids(project2, project1));
  }
}
