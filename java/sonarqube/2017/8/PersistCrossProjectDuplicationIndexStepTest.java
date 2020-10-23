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
package org.sonar.server.computation.task.projectanalysis.step;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.sonar.api.utils.System2;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.duplication.DuplicationUnitDto;
import org.sonar.scanner.protocol.output.ScannerReport;
import org.sonar.server.computation.task.projectanalysis.analysis.Analysis;
import org.sonar.server.computation.task.projectanalysis.analysis.AnalysisMetadataHolderRule;
import org.sonar.server.computation.task.projectanalysis.batch.BatchReportReaderRule;
import org.sonar.server.computation.task.projectanalysis.component.TreeRootHolderRule;
import org.sonar.server.computation.task.projectanalysis.component.Component.Status;
import org.sonar.server.computation.task.projectanalysis.component.Component;
import org.sonar.server.computation.task.projectanalysis.component.ReportComponent;
import org.sonar.server.computation.task.projectanalysis.duplication.CrossProjectDuplicationStatusHolder;
import org.sonar.server.computation.task.step.ComputationStep;

import static java.util.Collections.singletonList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.when;

public class PersistCrossProjectDuplicationIndexStepTest {

  private static final int FILE_1_REF = 2;
  private static final int FILE_2_REF = 3;
  private static final String FILE_2_UUID = "file2";

  private static final Component FILE_1 = ReportComponent.builder(Component.Type.FILE, FILE_1_REF).build();
  private static final Component FILE_2 = ReportComponent.builder(Component.Type.FILE, FILE_2_REF)
    .setStatus(Status.SAME).setUuid(FILE_2_UUID).build();

  private static final Component PROJECT = ReportComponent.builder(Component.Type.PROJECT, 1)
    .addChildren(FILE_1)
    .addChildren(FILE_2)
    .build();

  private static final ScannerReport.CpdTextBlock CPD_TEXT_BLOCK = ScannerReport.CpdTextBlock.newBuilder()
    .setHash("a8998353e96320ec")
    .setStartLine(30)
    .setEndLine(45)
    .build();
  private static final String ANALYSIS_UUID = "analysis uuid";
  private static final String BASE_ANALYSIS_UUID = "base analysis uuid";

  @Rule
  public DbTester dbTester = DbTester.create(System2.INSTANCE);
  @Rule
  public BatchReportReaderRule reportReader = new BatchReportReaderRule();
  @Rule
  public TreeRootHolderRule treeRootHolder = new TreeRootHolderRule().setRoot(PROJECT);
  @Rule
  public AnalysisMetadataHolderRule analysisMetadataHolder = new AnalysisMetadataHolderRule();

  @Mock
  CrossProjectDuplicationStatusHolder crossProjectDuplicationStatusHolder;
  @Mock
  Analysis baseAnalysis;

  DbClient dbClient = dbTester.getDbClient();

  ComputationStep underTest;

  @Before
  public void setUp() throws Exception {
    MockitoAnnotations.initMocks(this);
    when(baseAnalysis.getUuid()).thenReturn(BASE_ANALYSIS_UUID);
    analysisMetadataHolder.setUuid(ANALYSIS_UUID);
    analysisMetadataHolder.setBaseAnalysis(baseAnalysis);
    underTest = new PersistCrossProjectDuplicationIndexStep(crossProjectDuplicationStatusHolder, dbClient, treeRootHolder, analysisMetadataHolder, reportReader);
  }

  @Test
  public void copy_base_analysis_in_incremental_mode() {
    when(crossProjectDuplicationStatusHolder.isEnabled()).thenReturn(true);
    DuplicationUnitDto dup = new DuplicationUnitDto();
    dup.setAnalysisUuid(BASE_ANALYSIS_UUID);
    dup.setComponentUuid(FILE_2_UUID);
    dup.setEndLine(0);
    dup.setHash("asd");
    dup.setStartLine(0);
    dup.setId(1);
    dup.setIndexInFile(1);
    try (DbSession session = dbTester.getSession()) {
      dbClient.duplicationDao().insert(session, dup);
      session.commit();
    }
    assertThat(dbTester.countRowsOfTable("duplications_index")).isEqualTo(1);
    underTest.execute();

    Map<String, Object> dto = dbTester.selectFirst("select HASH, START_LINE, END_LINE, INDEX_IN_FILE, COMPONENT_UUID, ANALYSIS_UUID "
      + "from duplications_index where analysis_uuid = '" + ANALYSIS_UUID + "'");
    assertThat(dto.get("HASH")).isEqualTo("asd");
    assertThat(dto.get("START_LINE")).isEqualTo(0L);
    assertThat(dto.get("END_LINE")).isEqualTo(0L);
    assertThat(dto.get("INDEX_IN_FILE")).isEqualTo(0L);
    assertThat(dto.get("COMPONENT_UUID")).isEqualTo(FILE_2.getUuid());
    assertThat(dto.get("ANALYSIS_UUID")).isEqualTo(ANALYSIS_UUID);

  }

  @Test
  public void persist_cpd_text_block() throws Exception {
    when(crossProjectDuplicationStatusHolder.isEnabled()).thenReturn(true);
    reportReader.putDuplicationBlocks(FILE_1_REF, singletonList(CPD_TEXT_BLOCK));

    underTest.execute();

    Map<String, Object> dto = dbTester.selectFirst("select HASH, START_LINE, END_LINE, INDEX_IN_FILE, COMPONENT_UUID, ANALYSIS_UUID from duplications_index");
    assertThat(dto.get("HASH")).isEqualTo(CPD_TEXT_BLOCK.getHash());
    assertThat(dto.get("START_LINE")).isEqualTo(30L);
    assertThat(dto.get("END_LINE")).isEqualTo(45L);
    assertThat(dto.get("INDEX_IN_FILE")).isEqualTo(0L);
    assertThat(dto.get("COMPONENT_UUID")).isEqualTo(FILE_1.getUuid());
    assertThat(dto.get("ANALYSIS_UUID")).isEqualTo(ANALYSIS_UUID);
  }

  @Test
  public void persist_many_cpd_text_blocks() throws Exception {
    when(crossProjectDuplicationStatusHolder.isEnabled()).thenReturn(true);
    reportReader.putDuplicationBlocks(FILE_1_REF, Arrays.asList(
      CPD_TEXT_BLOCK,
      ScannerReport.CpdTextBlock.newBuilder()
        .setHash("b1234353e96320ff")
        .setStartLine(20)
        .setEndLine(15)
        .build()));

    underTest.execute();

    List<Map<String, Object>> dtos = dbTester.select("select HASH, START_LINE, END_LINE, INDEX_IN_FILE, COMPONENT_UUID, ANALYSIS_UUID from duplications_index");
    assertThat(dtos).extracting("HASH").containsOnly(CPD_TEXT_BLOCK.getHash(), "b1234353e96320ff");
    assertThat(dtos).extracting("START_LINE").containsOnly(30L, 20L);
    assertThat(dtos).extracting("END_LINE").containsOnly(45L, 15L);
    assertThat(dtos).extracting("INDEX_IN_FILE").containsOnly(0L, 1L);
    assertThat(dtos).extracting("COMPONENT_UUID").containsOnly(FILE_1.getUuid());
    assertThat(dtos).extracting("ANALYSIS_UUID").containsOnly(ANALYSIS_UUID);
  }

  @Test
  public void nothing_to_persist_when_no_cpd_text_blocks_in_report() throws Exception {
    when(crossProjectDuplicationStatusHolder.isEnabled()).thenReturn(true);
    reportReader.putDuplicationBlocks(FILE_1_REF, Collections.<ScannerReport.CpdTextBlock>emptyList());

    underTest.execute();

    assertThat(dbTester.countRowsOfTable("duplications_index")).isEqualTo(0);
  }

  @Test
  public void nothing_to_do_when_cross_project_duplication_is_disabled() throws Exception {
    when(crossProjectDuplicationStatusHolder.isEnabled()).thenReturn(false);
    reportReader.putDuplicationBlocks(FILE_1_REF, singletonList(CPD_TEXT_BLOCK));

    underTest.execute();

    assertThat(dbTester.countRowsOfTable("duplications_index")).isEqualTo(0);
  }

}
