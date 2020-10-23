/*
 * SonarQube
 * Copyright (C) 2009-2018 SonarSource SA
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
package org.sonarqube.tests.measure;

import com.sonar.orchestrator.Orchestrator;
import org.junit.ClassRule;
import org.junit.runner.RunWith;
import org.junit.runners.Suite;

import static util.ItUtils.newOrchestratorBuilder;
import static util.ItUtils.pluginArtifact;
import static util.ItUtils.xooPlugin;

@RunWith(Suite.class)
@Suite.SuiteClasses({
  ComplexityMeasuresTest.class,
  CustomMeasuresTest.class,
  DecimalScaleMetricTest.class,
  DifferentialPeriodsTest.class,
  MeasuresWsTest.class,
  ProjectActivityPageTest.class,
  ProjectDashboardTest.class,
  ProjectMeasuresPageTest.class,
  SincePreviousVersionHistoryTest.class,
  SinceXDaysHistoryTest.class,
  TimeMachineTest.class
})
public class MeasureSuite {

  @ClassRule
  public static final Orchestrator ORCHESTRATOR = newOrchestratorBuilder()
    .addPlugin(xooPlugin())

    // used by DecimalScaleMetricTest
    .addPlugin(pluginArtifact("batch-plugin"))

    .build();

}
