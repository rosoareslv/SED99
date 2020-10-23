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

import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyZeroInteractions;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.sonar.server.computation.task.projectanalysis.analysis.AnalysisMetadataHolderRule;
import org.sonar.server.computation.task.projectanalysis.duplication.DuplicationMeasures;
import org.sonar.server.computation.task.projectanalysis.duplication.IncrementalDuplicationMeasures;
import org.sonar.server.computation.task.step.ComputationStep;

public class DuplicationMeasuresStepTest extends BaseStepTest {
  @Mock
  private DuplicationMeasures defaultDuplicationMeasures;
  @Mock
  private IncrementalDuplicationMeasures incrementalDuplicationMeasures;
  @Rule
  public AnalysisMetadataHolderRule analysisMetadataHolder = new AnalysisMetadataHolderRule();

  private DuplicationMeasuresStep underTest;

  @Before
  public void before() {
    MockitoAnnotations.initMocks(this);
    underTest = new DuplicationMeasuresStep(analysisMetadataHolder, defaultDuplicationMeasures, incrementalDuplicationMeasures);
  }

  @Test
  public void incremental_analysis_mode() {
    analysisMetadataHolder.setIncrementalAnalysis(true);
    underTest.execute();
    verify(incrementalDuplicationMeasures).execute();
    verifyZeroInteractions(defaultDuplicationMeasures);
  }

  @Test
  public void full_analysis_mode() {
    analysisMetadataHolder.setIncrementalAnalysis(false);
    underTest.execute();
    verify(defaultDuplicationMeasures).execute();
    verifyZeroInteractions(incrementalDuplicationMeasures);
  }

  @Override
  protected ComputationStep step() {
    return underTest;
  }
}
