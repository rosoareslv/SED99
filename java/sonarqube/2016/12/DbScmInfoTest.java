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
package org.sonar.server.computation.task.projectanalysis.scm;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.db.protobuf.DbFileSources;
import org.sonar.server.computation.task.projectanalysis.component.Component;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.guava.api.Assertions.assertThat;
import static org.sonar.server.computation.task.projectanalysis.component.ReportComponent.builder;
import static org.sonar.server.source.index.FileSourceTesting.newFakeData;

public class DbScmInfoTest {

  @Rule
  public ExpectedException thrown = ExpectedException.none();

  static final int FILE_REF = 1;
  static final Component FILE = builder(Component.Type.FILE, FILE_REF).setKey("FILE_KEY").setUuid("FILE_UUID").build();

  @Test
  public void create_scm_info_with_some_changesets() throws Exception {
    ScmInfo scmInfo = DbScmInfo.create(FILE, newFakeData(10).build().getLinesList()).get();

    assertThat(scmInfo.getAllChangesets()).hasSize(10);
  }

  @Test
  public void return_changeset_for_a_given_line() throws Exception {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    addLine(fileDataBuilder, 1, "john", 123456789L, "rev-1");
    addLine(fileDataBuilder, 2, "henry", 1234567810L, "rev-2");
    addLine(fileDataBuilder, 3, "henry", 1234567810L, "rev-2");
    addLine(fileDataBuilder, 4, "john", 123456789L, "rev-1");
    fileDataBuilder.build();

    ScmInfo scmInfo = DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get();

    assertThat(scmInfo.getAllChangesets()).hasSize(4);

    Changeset changeset = scmInfo.getChangesetForLine(4);
    assertThat(changeset.getAuthor()).isEqualTo("john");
    assertThat(changeset.getDate()).isEqualTo(123456789L);
    assertThat(changeset.getRevision()).isEqualTo("rev-1");
  }

  @Test
  public void return_same_changeset_objects_for_lines_with_same_revision() throws Exception {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setScmDate(65L).setLine(1);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev2").setScmDate(6541L).setLine(2);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev1").setScmDate(6541L).setLine(3);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setScmDate(6542L).setLine(4);

    ScmInfo scmInfo = DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get();

    assertThat(scmInfo.getAllChangesets()).hasSize(4);

    assertThat(scmInfo.getChangesetForLine(1)).isSameAs(scmInfo.getChangesetForLine(4));
  }

  @Test
  public void return_latest_changeset() throws Exception {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    addLine(fileDataBuilder, 1, "john", 123456789L, "rev-1");
    // Older changeset
    addLine(fileDataBuilder, 2, "henry", 1234567810L, "rev-2");
    addLine(fileDataBuilder, 3, "john", 123456789L, "rev-1");
    fileDataBuilder.build();

    ScmInfo scmInfo = DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get();

    Changeset latestChangeset = scmInfo.getLatestChangeset();
    assertThat(latestChangeset.getAuthor()).isEqualTo("henry");
    assertThat(latestChangeset.getDate()).isEqualTo(1234567810L);
    assertThat(latestChangeset.getRevision()).isEqualTo("rev-2");
  }

  @Test
  public void return_absent_dsm_info_when_no_changeset() throws Exception {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setLine(1);

    assertThat(DbScmInfo.create(FILE, fileDataBuilder.getLinesList())).isAbsent();
  }

  @Test
  public void return_absent_dsm_info_when_changeset_line_has_both_revision_and_date() throws Exception {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setLine(1);
    fileDataBuilder.addLinesBuilder().setScmDate(6541L).setLine(2);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setLine(3);
    fileDataBuilder.addLinesBuilder().setScmAuthor("author").setLine(4);

    assertThat(DbScmInfo.create(FILE, fileDataBuilder.getLinesList())).isAbsent();
  }

  @Test
  public void fail_with_ISE_when_changeset_has_no_field() throws Exception {
    thrown.expect(IllegalStateException.class);
    thrown.expectMessage("Partial scm information stored in DB for component 'ReportComponent{ref=1, key='FILE_KEY', type=FILE}'. " +
      "Not all lines have SCM info. Can not proceed");

    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setScmDate(543L).setLine(1);
    fileDataBuilder.addLinesBuilder().setLine(2);
    fileDataBuilder.build();

    DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get().getAllChangesets();
  }

  @Test
  public void fail_with_ISE_when_changeset_has_only_revision_field() throws Exception {
    thrown.expect(IllegalStateException.class);
    thrown.expectMessage("Partial scm information stored in DB for component 'ReportComponent{ref=1, key='FILE_KEY', type=FILE}'. " +
      "Not all lines have SCM info. Can not proceed");

    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setScmDate(555L).setLine(1);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev-1").setLine(2);
    fileDataBuilder.build();

    DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get().getAllChangesets();
  }

  @Test
  public void fail_with_ISE_when_changeset_has_only_author_field() throws Exception {
    thrown.expect(IllegalStateException.class);
    thrown.expectMessage("Partial scm information stored in DB for component 'ReportComponent{ref=1, key='FILE_KEY', type=FILE}'. " +
      "Not all lines have SCM info. Can not proceed");

    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    fileDataBuilder.addLinesBuilder().setScmAuthor("John").setLine(1);
    fileDataBuilder.addLinesBuilder().setScmRevision("rev").setScmDate(555L).setLine(2);
    fileDataBuilder.build();

    DbScmInfo.create(FILE, fileDataBuilder.getLinesList()).get().getAllChangesets();
  }

  private static void addLine(DbFileSources.Data.Builder dataBuilder, Integer line, String author, Long date, String revision) {
    dataBuilder.addLinesBuilder()
      .setLine(line)
      .setScmAuthor(author)
      .setScmDate(date)
      .setScmRevision(revision);
  }

}
