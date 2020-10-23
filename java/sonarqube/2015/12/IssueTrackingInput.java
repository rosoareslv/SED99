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
package org.sonar.batch.issue.tracking;

import org.sonar.core.issue.tracking.Trackable;
import org.sonar.core.issue.tracking.BlockHashSequence;
import org.sonar.core.issue.tracking.LineHashSequence;

import java.util.Collection;
import java.util.List;

import org.sonar.core.issue.tracking.Input;

public class IssueTrackingInput<T extends Trackable> implements Input<T> {

  private final Collection<T> issues;
  private final LineHashSequence lineHashes;
  private final BlockHashSequence blockHashes;

  public IssueTrackingInput(Collection<T> issues, List<String> hashes) {
    this.issues = issues;
    this.lineHashes = new LineHashSequence(hashes);
    this.blockHashes = BlockHashSequence.create(lineHashes);
  }

  @Override
  public LineHashSequence getLineHashSequence() {
    return lineHashes;
  }

  @Override
  public BlockHashSequence getBlockHashSequence() {
    return blockHashes;
  }

  @Override
  public Collection<T> getIssues() {
    return issues;
  }

}
