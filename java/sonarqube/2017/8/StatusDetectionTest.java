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
package org.sonar.scanner.scan.filesystem;

import com.google.common.collect.HashBasedTable;
import com.google.common.collect.ImmutableTable;
import com.google.common.collect.Table;
import org.junit.Test;
import org.sonar.api.batch.fs.InputFile;
import org.sonar.scanner.repository.FileData;
import org.sonar.scanner.repository.ProjectRepositories;

import static org.assertj.core.api.Assertions.assertThat;

public class StatusDetectionTest {
  @Test
  public void detect_status() {
    Table<String, String, String> t = ImmutableTable.of();
    ProjectRepositories ref = new ProjectRepositories(t, createTable(), null);
    StatusDetection statusDetection = new StatusDetection(ref);

    assertThat(statusDetection.status("foo", "src/Foo.java", "ABCDE")).isEqualTo(InputFile.Status.SAME);
    assertThat(statusDetection.status("foo", "src/Foo.java", "XXXXX")).isEqualTo(InputFile.Status.CHANGED);
    assertThat(statusDetection.status("foo", "src/Other.java", "QWERT")).isEqualTo(InputFile.Status.ADDED);
  }

  private static Table<String, String, FileData> createTable() {
    Table<String, String, FileData> t = HashBasedTable.create();

    t.put("foo", "src/Foo.java", new FileData("ABCDE", "12345789"));
    t.put("foo", "src/Bar.java", new FileData("FGHIJ", "123456789"));

    return t;
  }
}
