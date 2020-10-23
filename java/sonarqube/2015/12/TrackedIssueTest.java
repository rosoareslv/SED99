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

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.Test;

public class TrackedIssueTest {
  @Test
  public void round_trip() {
    TrackedIssue issue = new TrackedIssue();
    issue.setStartLine(15);

    assertThat(issue.getLine()).isEqualTo(15);
    assertThat(issue.startLine()).isEqualTo(15);
  }

  @Test
  public void hashes() {
    String[] hashArr = new String[] {
      "hash1", "hash2", "hash3"
    };

    FileHashes hashes = FileHashes.create(hashArr);
    TrackedIssue issue = new TrackedIssue(hashes);
    issue.setStartLine(1);
    assertThat(issue.getLineHash()).isEqualTo("hash1");
  }
}
