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
package org.sonarqube.ws.client.setting;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;

import static org.assertj.core.api.Assertions.assertThat;

public class ValuesRequestTest {

  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  ValuesRequest.Builder underTest = ValuesRequest.builder();

  @Test
  public void create_request_with_no_component() {
    ValuesRequest result = underTest.setKeys("sonar.debt").build();

    assertThat(result.getComponent()).isNull();
    assertThat(result.getKeys()).containsOnly("sonar.debt");
  }

  @Test
  public void create_request_with_no_keys() {
    ValuesRequest result = underTest.build();

    assertThat(result.getComponent()).isNull();
    assertThat(result.getKeys()).isEmpty();
  }

  @Test
  public void create_request_with_component() {
    ValuesRequest result = underTest.setKeys("sonar.debt").setComponent("projectKey").build();

    assertThat(result.getComponent()).isEqualTo("projectKey");
    assertThat(result.getKeys()).containsOnly("sonar.debt");
  }

}
