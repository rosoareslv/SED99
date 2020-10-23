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
package org.sonar.server.computation.task.projectanalysis.scm;

import com.google.common.collect.ImmutableList;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import javax.annotation.Nullable;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.utils.System2;
import org.sonar.api.utils.log.LogTester;
import org.sonar.core.hash.SourceHashComputer;
import org.sonar.db.DbTester;
import org.sonar.db.protobuf.DbFileSources;
import org.sonar.db.source.FileSourceDto;
import org.sonar.scanner.protocol.output.ScannerReport;
import org.sonar.server.computation.task.projectanalysis.analysis.Analysis;
import org.sonar.server.computation.task.projectanalysis.analysis.AnalysisMetadataHolderRule;
import org.sonar.server.computation.task.projectanalysis.analysis.Branch;
import org.sonar.server.computation.task.projectanalysis.batch.BatchReportReaderRule;
import org.sonar.server.computation.task.projectanalysis.component.Component;
import org.sonar.server.computation.task.projectanalysis.component.MergeBranchComponentUuids;
import org.sonar.server.computation.task.projectanalysis.scm.ScmInfoRepositoryImpl.NoScmInfo;
import org.sonar.server.computation.task.projectanalysis.source.SourceHashRepositoryImpl;
import org.sonar.server.computation.task.projectanalysis.source.SourceLinesRepositoryImpl;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.api.utils.log.LoggerLevel.TRACE;
import static org.sonar.server.computation.task.projectanalysis.component.ReportComponent.builder;

public class ScmInfoDbLoaderTest {
  static final int FILE_REF = 1;
  static final Component FILE = builder(Component.Type.FILE, FILE_REF).setKey("FILE_KEY").setUuid("FILE_UUID").build();
  static final long DATE_1 = 123456789L;
  static final long DATE_2 = 1234567810L;

  static Analysis baseProjectAnalysis = new Analysis.Builder()
    .setId(1)
    .setUuid("uuid_1")
    .setCreatedAt(123456789L)
    .build();

  @Rule
  public LogTester logTester = new LogTester();
  @Rule
  public AnalysisMetadataHolderRule analysisMetadataHolder = new AnalysisMetadataHolderRule();
  @Rule
  public DbTester dbTester = DbTester.create(System2.INSTANCE);
  @Rule
  public BatchReportReaderRule reportReader = new BatchReportReaderRule();

  private Branch branch = mock(Branch.class);
  private SourceHashRepositoryImpl sourceHashRepository = new SourceHashRepositoryImpl(new SourceLinesRepositoryImpl(reportReader));
  private MergeBranchComponentUuids mergeBranchComponentUuids = mock(MergeBranchComponentUuids.class);

  private ScmInfoDbLoader underTest = new ScmInfoDbLoader(analysisMetadataHolder, dbTester.getDbClient(), sourceHashRepository, mergeBranchComponentUuids);

  @Test
  public void returns_ScmInfo_from_DB_if_hashes_are_the_same() throws Exception {
    analysisMetadataHolder.setBaseAnalysis(baseProjectAnalysis);
    analysisMetadataHolder.setBranch(null);

    addFileSourceInDb("henry", DATE_1, "rev-1", computeSourceHash(1));
    addFileSourceInReport(1);

    ScmInfo scmInfo = underTest.getScmInfoFromDb(FILE);
    assertThat(scmInfo.getAllChangesets()).hasSize(1);

    assertThat(logTester.logs(TRACE)).containsOnly("Reading SCM info from db for file 'FILE_UUID'");
  }

  @Test
  public void read_from_merge_branch_if_no_base() {
    analysisMetadataHolder.setBaseAnalysis(null);
    analysisMetadataHolder.setBranch(branch);
    String mergeFileUuid = "mergeFileUuid";
    when(branch.getMergeBranchUuid()).thenReturn(Optional.of("mergeBranchUuid"));

    when(mergeBranchComponentUuids.getUuid(FILE.getKey())).thenReturn(mergeFileUuid);
    addFileSourceInDb("henry", DATE_1, "rev-1", computeSourceHash(1), mergeFileUuid);
    addFileSourceInReport(1);

    ScmInfo scmInfo = underTest.getScmInfoFromDb(FILE);
    assertThat(scmInfo.getAllChangesets()).hasSize(1);
    assertThat(logTester.logs(TRACE)).containsOnly("Reading SCM info from db for file 'mergeFileUuid'");
  }

  @Test
  public void returns_absent_when_branch_and_source_is_different() {
    analysisMetadataHolder.setBaseAnalysis(null);
    analysisMetadataHolder.setBranch(branch);
    String mergeFileUuid = "mergeFileUuid";
    when(branch.getMergeBranchUuid()).thenReturn(Optional.of("mergeBranchUuid"));

    when(mergeBranchComponentUuids.getUuid(FILE.getKey())).thenReturn(mergeFileUuid);
    addFileSourceInDb("henry", DATE_1, "rev-1", computeSourceHash(1) + "dif", mergeFileUuid);
    addFileSourceInReport(1);

    assertThat(underTest.getScmInfoFromDb(FILE)).isEqualTo(NoScmInfo.INSTANCE);
    assertThat(logTester.logs(TRACE)).containsOnly("Reading SCM info from db for file 'mergeFileUuid'");
  }

  @Test
  public void returns_absent_when__hashes_are_not_the_same() throws Exception {
    analysisMetadataHolder.setBaseAnalysis(baseProjectAnalysis);
    analysisMetadataHolder.setBranch(null);

    addFileSourceInReport(1);
    addFileSourceInDb("henry", DATE_1, "rev-1", computeSourceHash(1) + "_different");

    assertThat(underTest.getScmInfoFromDb(FILE)).isEqualTo(NoScmInfo.INSTANCE);
    assertThat(logTester.logs(TRACE)).containsOnly("Reading SCM info from db for file 'FILE_UUID'");
  }

  @Test
  public void not_read_in_db_on_first_analysis_and_no_merge_branch() throws Exception {
    Branch branch = mock(Branch.class);
    when(branch.getMergeBranchUuid()).thenReturn(Optional.empty());
    analysisMetadataHolder.setBaseAnalysis(null);
    analysisMetadataHolder.setBranch(branch);

    addFileSourceInReport(1);

    assertThat(underTest.getScmInfoFromDb(FILE)).isEqualTo(NoScmInfo.INSTANCE);
    assertThat(logTester.logs(TRACE)).isEmpty();
  }

  private static List<String> generateLines(int lineCount) {
    ImmutableList.Builder<String> builder = ImmutableList.builder();
    for (int i = 0; i < lineCount; i++) {
      builder.add("line " + i);
    }
    return builder.build();
  }

  private static String computeSourceHash(int lineCount) {
    SourceHashComputer sourceHashComputer = new SourceHashComputer();
    Iterator<String> lines = generateLines(lineCount).iterator();
    while (lines.hasNext()) {
      sourceHashComputer.addLine(lines.next(), lines.hasNext());
    }
    return sourceHashComputer.getHash();
  }

  private void addFileSourceInDb(@Nullable String author, @Nullable Long date, @Nullable String revision, String srcHash) {
    addFileSourceInDb(author, date, revision, srcHash, FILE.getUuid());
  }

  private void addFileSourceInDb(@Nullable String author, @Nullable Long date, @Nullable String revision, String srcHash, String fileUuid) {
    DbFileSources.Data.Builder fileDataBuilder = DbFileSources.Data.newBuilder();
    DbFileSources.Line.Builder builder = fileDataBuilder.addLinesBuilder()
      .setLine(1);
    if (author != null) {
      builder.setScmAuthor(author);
    }
    if (date != null) {
      builder.setScmDate(date);
    }
    if (revision != null) {
      builder.setScmRevision(revision);
    }
    dbTester.getDbClient().fileSourceDao().insert(dbTester.getSession(), new FileSourceDto()
      .setFileUuid(fileUuid)
      .setProjectUuid("PROJECT_UUID")
      .setSourceData(fileDataBuilder.build())
      .setSrcHash(srcHash));
    dbTester.commit();
  }

  private void addFileSourceInReport(int lineCount) {
    reportReader.putFileSourceLines(FILE_REF, generateLines(lineCount));
    reportReader.putComponent(ScannerReport.Component.newBuilder()
      .setRef(FILE_REF)
      .setLines(lineCount)
      .build());
  }
}
