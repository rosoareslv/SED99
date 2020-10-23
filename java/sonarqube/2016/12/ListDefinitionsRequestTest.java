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

package org.sonarqube.ws.client.setting;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;

import static org.assertj.core.api.Assertions.assertThat;

public class ListDefinitionsRequestTest {

  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  ListDefinitionsRequest.Builder underTest = ListDefinitionsRequest.builder();

  @Test
  public void create_request_with_no_component() {
    ListDefinitionsRequest result = underTest.build();

    assertThat(result.getComponentId()).isNull();
    assertThat(result.getComponentKey()).isNull();
  }

  @Test
  public void create_request_with_component_id() {
    ListDefinitionsRequest result = underTest.setComponentId("projectId").build();

    assertThat(result.getComponentId()).isEqualTo("projectId");
    assertThat(result.getComponentKey()).isNull();
  }

  @Test
  public void create_request_with_component_key() {
    ListDefinitionsRequest result = underTest.setComponentKey("projectKey").build();

    assertThat(result.getComponentId()).isNull();
    assertThat(result.getComponentKey()).isEqualTo("projectKey");
  }

}
