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
package org.sonar.api.batch.sensor.internal;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class MockAnalysisModeTest {

  @Test
  public void sanityCheck() {
    MockAnalysisMode mode = new MockAnalysisMode();
    assertThat(mode.isIssues()).isFalse();
    assertThat(mode.isPreview()).isFalse();
    assertThat(mode.isPublish()).isTrue();
    assertThat(mode.isIncremental()).isFalse();
    mode.setPreviewOrIssue(true);
    mode.setIncremental(true);
    assertThat(mode.isIssues()).isTrue();
    assertThat(mode.isPreview()).isTrue();
    assertThat(mode.isPublish()).isFalse();
    assertThat(mode.isIncremental()).isTrue();
  }

}
