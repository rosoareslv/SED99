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

import org.sonar.api.batch.AnalysisMode;

public class MockAnalysisMode implements AnalysisMode {
  private boolean previewOrIssue = false;
  private boolean incremental = false;

  @Override
  public boolean isPreview() {
    return previewOrIssue;
  }

  public void setPreviewOrIssue(boolean value) {
    this.previewOrIssue = value;
  }

  @Override
  public boolean isIssues() {
    return this.previewOrIssue;
  }

  @Override
  public boolean isPublish() {
    return !previewOrIssue;
  }

  @Override
  public boolean isIncremental() {
    return incremental;
  }

  public void setIncremental(boolean incremental) {
    this.incremental = incremental;
  }
}
