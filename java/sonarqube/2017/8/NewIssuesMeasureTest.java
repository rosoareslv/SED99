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
package org.sonarqube.tests.issue;

import com.sonar.orchestrator.build.SonarScanner;
import java.util.Map;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.Test;
import org.sonar.wsclient.issue.IssueQuery;
import org.sonarqube.ws.WsMeasures;
import org.sonarqube.ws.WsMeasures.Measure;
import util.ItUtils;

import static java.lang.Integer.parseInt;
import static org.assertj.core.api.Assertions.assertThat;
import static util.ItUtils.getLeakPeriodValue;
import static util.ItUtils.getMeasuresWithVariationsByMetricKey;
import static util.ItUtils.projectDir;
import static util.ItUtils.setServerProperty;

/**
 * SONAR-4564
 */
public class NewIssuesMeasureTest extends AbstractIssueTest {

  @AfterClass
  public static void resetPeriod() {
    ItUtils.resetPeriod(ORCHESTRATOR);
  }

  @Before
  public void cleanUpAnalysisData() {
    ORCHESTRATOR.resetData();
  }

  @Test
  public void new_issues_measures() throws Exception {
    setServerProperty(ORCHESTRATOR, "sonar.leak.period", "previous_analysis");
    ORCHESTRATOR.getServer().provisionProject("sample", "Sample");

    // Execute an analysis in the past with no issue to have a past snapshot
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("sample", "xoo", "empty");
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-sample")).setProperty("sonar.projectDate", "2013-01-01"));

    // Execute a analysis now with some issues
    ItUtils.restoreProfile(ORCHESTRATOR, getClass().getResource("/issue/one-issue-per-line-profile.xml"));
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("sample", "xoo", "one-issue-per-line-profile");
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-sample")));

    assertThat(ORCHESTRATOR.getServer().wsClient().issueClient().find(IssueQuery.create()).list()).isNotEmpty();
    assertThat(getLeakPeriodValue(ORCHESTRATOR, "sample:src/main/xoo/sample/Sample.xoo", "new_violations")).isEqualTo(17);

    // second analysis, with exactly the same profile -> no new issues
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-sample")));

    assertThat(ORCHESTRATOR.getServer().wsClient().issueClient().find(IssueQuery.create()).list()).isNotEmpty();
    assertThat(getLeakPeriodValue(ORCHESTRATOR, "sample:src/main/xoo/sample/Sample.xoo", "new_violations")).isZero();
  }

  @Test
  public void new_issues_measures_should_be_zero_on_project_when_no_new_issues_since_x_days() throws Exception {
    setServerProperty(ORCHESTRATOR, "sonar.leak.period", "30");
    ORCHESTRATOR.getServer().provisionProject("sample", "Sample");
    ItUtils.restoreProfile(ORCHESTRATOR, getClass().getResource("/issue/one-issue-per-line-profile.xml"));
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("sample", "xoo", "one-issue-per-line-profile");

    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-sample"))
      // Analyse a project in the past, with a date older than 30 last days (second period)
      .setProperty("sonar.projectDate", "2013-01-01"));
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-sample")));

    // new issues measures should be to 0 on project on 2 periods as new issues has been created
    assertThat(getLeakPeriodValue(ORCHESTRATOR, "sample", "new_violations")).isZero();
  }

  /**
   * SONAR-3647
   */
  @Test
  public void new_issues_measures_consistent_with_variations() throws Exception {
    setServerProperty(ORCHESTRATOR, "sonar.leak.period", "previous_analysis");
    ORCHESTRATOR.getServer().provisionProject("sample", "Sample");
    ItUtils.restoreProfile(ORCHESTRATOR, getClass().getResource("/issue/one-issue-per-line-profile.xml"));
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("sample", "xoo", "one-issue-per-line-profile");

    // Execute an analysis in the past to have a past snapshot
    // version 1
    ORCHESTRATOR.executeBuilds(SonarScanner.create(projectDir("shared/xoo-history-v1")));

    // version 2 with 2 new violations and 3 more ncloc
    ORCHESTRATOR.executeBuilds(SonarScanner.create(projectDir("shared/xoo-history-v2")));

    assertThat(ORCHESTRATOR.getServer().wsClient().issueClient().find(IssueQuery.create()).list()).isNotEmpty();

    Map<String, Measure> measures = getMeasuresWithVariationsByMetricKey(ORCHESTRATOR, "sample", "new_violations", "violations", "ncloc");
    assertThat(measures.get("new_violations").getPeriods().getPeriodsValueList()).extracting(WsMeasures.PeriodValue::getValue).containsOnly("17");

    Measure violations = measures.get("violations");
    assertThat(parseInt(violations.getValue())).isEqualTo(43);
    assertThat(violations.getPeriods().getPeriodsValueList()).extracting(periodValue -> parseInt(periodValue.getValue())).containsOnly(17);

    Measure ncloc = measures.get("ncloc");
    assertThat(parseInt(ncloc.getValue())).isEqualTo(40);
    assertThat(ncloc.getPeriods().getPeriodsValueList()).extracting(periodValue -> parseInt(periodValue.getValue())).containsOnly(16);
  }

  @Test
  public void new_issues_measures_should_be_correctly_calculated_when_adding_a_new_module() throws Exception {
    setServerProperty(ORCHESTRATOR, "sonar.leak.period", "previous_analysis");
    ORCHESTRATOR.getServer().provisionProject("com.sonarsource.it.samples:multi-modules-sample", "com.sonarsource.it.samples:multi-modules-sample");

    // First analysis without module b
    ItUtils.restoreProfile(ORCHESTRATOR, getClass().getResource("/issue/NewIssuesMeasureTest/profile1.xml"));
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("com.sonarsource.it.samples:multi-modules-sample", "xoo", "profile1");
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-multi-modules-sample"))
      .setProperties("sonar.modules", "module_a"));

    // Second analysis with module b and with a new rule activated to have new issues on module a since last analysis
    ItUtils.restoreProfile(ORCHESTRATOR, getClass().getResource("/issue/NewIssuesMeasureTest/profile2.xml"));
    ORCHESTRATOR.getServer().associateProjectToQualityProfile("com.sonarsource.it.samples:multi-modules-sample", "xoo", "profile2");
    ORCHESTRATOR.executeBuild(SonarScanner.create(projectDir("shared/xoo-multi-modules-sample")));

    assertThat(getLeakPeriodValue(ORCHESTRATOR, "com.sonarsource.it.samples:multi-modules-sample", "new_violations")).isEqualTo(65);
  }

}
