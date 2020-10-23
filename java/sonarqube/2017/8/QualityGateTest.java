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
package org.sonarqube.tests.qualityGate;

import com.google.gson.Gson;
import com.sonar.orchestrator.Orchestrator;
import com.sonar.orchestrator.build.BuildResult;
import com.sonar.orchestrator.build.SonarScanner;
import java.io.File;
import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import org.apache.commons.io.Charsets;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.ClassRule;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.wsclient.qualitygate.NewCondition;
import org.sonar.wsclient.qualitygate.QualityGate;
import org.sonar.wsclient.qualitygate.QualityGateClient;
import org.sonarqube.tests.Category1Suite;
import org.sonarqube.tests.Tester;
import org.sonarqube.ws.MediaTypes;
import org.sonarqube.ws.WsCe;
import org.sonarqube.ws.WsMeasures.Measure;
import org.sonarqube.ws.WsProjects.CreateWsResponse.Project;
import org.sonarqube.ws.WsQualityGates;
import org.sonarqube.ws.WsQualityGates.ProjectStatusWsResponse;
import org.sonarqube.ws.client.GetRequest;
import org.sonarqube.ws.client.PostRequest;
import org.sonarqube.ws.client.WsResponse;
import org.sonarqube.ws.client.qualitygate.ProjectStatusWsRequest;
import org.sonarqube.ws.client.qualitygate.SelectWsRequest;

import static org.apache.commons.lang.RandomStringUtils.randomAlphabetic;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.groups.Tuple.tuple;
import static util.ItUtils.concat;
import static util.ItUtils.extractCeTaskId;
import static util.ItUtils.getMeasure;
import static util.ItUtils.newProjectKey;
import static util.ItUtils.projectDir;

public class QualityGateTest {

  private static final String TASK_STATUS_SUCCESS = "SUCCESS";
  private static final String QG_STATUS_NO_QG = "null";
  private static final String QG_STATUS_OK = "OK";
  private static final String QG_STATUS_ERROR = "ERROR";
  private static final String QG_STATUS_WARN = "WARN";

  @ClassRule
  public static Orchestrator orchestrator = Category1Suite.ORCHESTRATOR;

  @Rule
  public Tester tester = new Tester(orchestrator).disableOrganizations();

  private QualityGate defaultGate;

  @Before
  public void setUp() {
    defaultGate = qgClient().list().defaultGate();
  }

  @After
  public void tearDown() {
    if (defaultGate != null) {
      qgClient().setDefault(defaultGate.id());
    }
  }

  @Test
  public void do_not_compute_status_if_no_gate() throws Exception {
    qgClient().unsetDefault();
    String projectKey = newProjectKey();
    BuildResult buildResult = executeAnalysis(projectKey);

    verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_NO_QG);

    assertThat(getGateStatusMeasure(projectKey)).isNull();
  }

  @Test
  public void status_ok_if_empty_gate() throws Exception {
    QualityGate empty = qgClient().create("Empty");
    qgClient().setDefault(empty.id());

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_OK);

      assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("OK");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(empty.id());
    }
  }

  @Test
  public void test_status_ok() throws IOException {
    QualityGate simple = qgClient().create("SimpleWithHighThreshold");
    qgClient().setDefault(simple.id());
    qgClient().createCondition(NewCondition.create(simple.id()).metricKey("ncloc").operator("GT").warningThreshold("40"));

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_OK);

      assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("OK");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(simple.id());
    }
  }

  @Test
  public void test_status_warning() throws IOException {
    QualityGate simple = qgClient().create("SimpleWithLowThreshold");
    qgClient().setDefault(simple.id());
    qgClient().createCondition(NewCondition.create(simple.id()).metricKey("ncloc").operator("GT").warningThreshold("10"));

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_WARN);

      assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("WARN");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(simple.id());
    }
  }

  @Test
  public void test_status_error() throws IOException {
    QualityGate simple = qgClient().create("SimpleWithLowThreshold");
    qgClient().setDefault(simple.id());
    qgClient().createCondition(NewCondition.create(simple.id()).metricKey("ncloc").operator("GT").errorThreshold("10"));

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_ERROR);

      assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("ERROR");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(simple.id());
    }
  }

  @Test
  public void use_server_settings_instead_of_default_gate() throws IOException {
    QualityGate alert = qgClient().create("AlertWithLowThreshold");
    qgClient().createCondition(NewCondition.create(alert.id()).metricKey("ncloc").operator("GT").warningThreshold("10"));
    QualityGate error = qgClient().create("ErrorWithLowThreshold");
    qgClient().createCondition(NewCondition.create(error.id()).metricKey("ncloc").operator("GT").errorThreshold("10"));

    qgClient().setDefault(alert.id());
    String projectKey = newProjectKey();
    orchestrator.getServer().provisionProject(projectKey, projectKey);
    associateQualityGateToProject(error.id(), projectKey);

    try {
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_ERROR);

      assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("ERROR");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(alert.id());
      qgClient().destroy(error.id());
    }
  }

  @Test
  public void conditions_on_multiple_metric_types() throws IOException {
    QualityGate allTypes = qgClient().create("AllMetricTypes");
    qgClient().createCondition(NewCondition.create(allTypes.id()).metricKey("ncloc").operator("GT").warningThreshold("10"));
    qgClient().createCondition(NewCondition.create(allTypes.id()).metricKey("duplicated_lines_density").operator("GT").warningThreshold("20"));
    qgClient().setDefault(allTypes.id());

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey, "sonar.cpd.xoo.minimumLines", "2", "sonar.cpd.xoo.minimumTokens", "5");

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_WARN);

      Measure alertStatus = getGateStatusMeasure(projectKey);
      assertThat(alertStatus.getValue()).isEqualTo("WARN");

      String qualityGateDetailJson = getMeasure(orchestrator, projectKey, "quality_gate_details").getValue();
      assertThat(QualityGateDetails.parse(qualityGateDetailJson).getConditions())
        .extracting(QualityGateDetails.Conditions::getMetric, QualityGateDetails.Conditions::getOp, QualityGateDetails.Conditions::getWarning)
        .contains(tuple("ncloc", "GT", "10"), tuple("duplicated_lines_density", "GT", "20"));
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(allTypes.id());
    }
  }

  @Test
  public void ad_hoc_build_break_strategy() throws IOException {
    QualityGate simple = qgClient().create("SimpleWithLowThresholdForBuildBreakStrategy");
    qgClient().setDefault(simple.id());
    qgClient().createCondition(NewCondition.create(simple.id()).metricKey("ncloc").operator("GT").errorThreshold("7"));

    try {
      String projectKey = newProjectKey();
      BuildResult buildResult = executeAnalysis(projectKey);

      verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_ERROR);

      String taskId = getTaskIdInLocalReport(projectDir("qualitygate/xoo-sample"));
      String analysisId = getAnalysisId(taskId);

      ProjectStatusWsResponse projectStatusWsResponse = tester.wsClient().qualityGates().projectStatus(new ProjectStatusWsRequest().setAnalysisId(analysisId));
      ProjectStatusWsResponse.ProjectStatus projectStatus = projectStatusWsResponse.getProjectStatus();
      assertThat(projectStatus.getStatus()).isEqualTo(ProjectStatusWsResponse.Status.ERROR);
      assertThat(projectStatus.getConditionsCount()).isEqualTo(1);
      ProjectStatusWsResponse.Condition condition = projectStatus.getConditionsList().get(0);
      assertThat(condition.getMetricKey()).isEqualTo("ncloc");
      assertThat(condition.getErrorThreshold()).isEqualTo("7");
    } finally {
      qgClient().unsetDefault();
      qgClient().destroy(simple.id());
    }
  }

  @Test
  public void does_not_fail_when_condition_is_on_removed_metric() throws Exception {

    // create project
    Project project = tester.projects().generate(null);
    String projectKey = project.getKey();

    // create custom metric
    String customMetricKey = randomAlphabetic(10);
    createCustomIntMetric(customMetricKey);
    try {

      // create quality gate
      WsQualityGates.CreateWsResponse simple = tester.wsClient().qualityGates().create("OnCustomMetric");
      Long qualityGateId = simple.getId();
      try {
        qgClient().createCondition(NewCondition.create(qualityGateId).metricKey(customMetricKey).operator("GT").warningThreshold("40"));

        // delete custom metric
        deleteCustomMetric(customMetricKey);

        // run analysis
        tester.wsClient().qualityGates().associateProject(new SelectWsRequest().setProjectKey(projectKey).setGateId(qualityGateId));
        BuildResult buildResult = executeAnalysis(projectKey);

        // verify quality gate
        verifyQGStatusInPostTask(buildResult, projectKey, TASK_STATUS_SUCCESS, QG_STATUS_OK);
        assertThat(getGateStatusMeasure(projectKey).getValue()).isEqualTo("OK");
      } finally {
        qgClient().destroy(qualityGateId);
      }
    } finally {
      deleteCustomMetric(customMetricKey);
    }
  }

  private BuildResult executeAnalysis(String projectKey, String... keyValueProperties) {
    return orchestrator.executeBuild(SonarScanner.create(
      projectDir("qualitygate/xoo-sample"), concat(keyValueProperties, "sonar.projectKey", projectKey)));
  }

  private void verifyQGStatusInPostTask(BuildResult buildResult, String projectKey, String taskStatus, String qgStatus) throws IOException {
    List<String> logsLines = FileUtils.readLines(orchestrator.getServer().getCeLogs(), Charsets.UTF_8);
    List<String> postTaskLogLines = extractPosttaskPluginLogs(extractCeTaskId(buildResult), logsLines);

    assertThat(postTaskLogLines).hasSize(1);
    assertThat(postTaskLogLines.iterator().next())
      .contains("CeTask[" + taskStatus + "]")
      .contains("Project[" + projectKey + "]")
      .contains("QualityGate[" + qgStatus + "]");
  }

  private String getAnalysisId(String taskId) throws IOException {
    WsResponse activity = tester.wsClient()
      .wsConnector()
      .call(new GetRequest("api/ce/task")
        .setParam("id", taskId)
        .setMediaType(MediaTypes.PROTOBUF));
    WsCe.TaskResponse activityWsResponse = WsCe.TaskResponse.parseFrom(activity.contentStream());
    return activityWsResponse.getTask().getAnalysisId();
  }

  private String getTaskIdInLocalReport(File projectDirectory) throws IOException {
    File metadata = new File(projectDirectory, ".sonar/report-task.txt");
    assertThat(metadata).exists().isFile();
    // verify properties
    Properties props = new Properties();
    props.load(new StringReader(FileUtils.readFileToString(metadata, StandardCharsets.UTF_8)));
    assertThat(props.getProperty("ceTaskId")).isNotEmpty();

    return props.getProperty("ceTaskId");
  }

  private Measure getGateStatusMeasure(String projectKey) {
    return getMeasure(orchestrator, projectKey, "alert_status");
  }

  private static QualityGateClient qgClient() {
    return orchestrator.getServer().adminWsClient().qualityGateClient();
  }

  private void associateQualityGateToProject(long qGateId, String projectKey) {
    tester.wsClient().wsConnector()
      .call(new PostRequest("api/qualitygates/select")
        .setParam("gateId", qGateId)
        .setParam("projectKey", projectKey))
      .failIfNotSuccessful();
  }

  private static List<String> extractPosttaskPluginLogs(String taskUuid, Iterable<String> ceLogs) {
    return StreamSupport.stream(ceLogs.spliterator(), false)
      .filter(s -> s.contains("POSTASKPLUGIN: finished()"))
      .filter(s -> s.contains(taskUuid))
      .collect(Collectors.toList());
  }

  private void createCustomIntMetric(String metricKey) {
    tester.wsClient().wsConnector().call(new PostRequest("api/metrics/create")
      .setParam("key", metricKey)
      .setParam("name", metricKey)
      .setParam("type", "INT"))
      .failIfNotSuccessful();
  }

  private void deleteCustomMetric(String metricKey) {
    tester.wsClient().wsConnector().call(new PostRequest("api/metrics/delete")
      .setParam("keys", metricKey))
      .failIfNotSuccessful();
  }

  static class QualityGateDetails {

    private String level;

    private List<Conditions> conditions = new ArrayList<>();

    String getLevel() {
      return level;
    }

    List<Conditions> getConditions() {
      return conditions;
    }

    public static QualityGateDetails parse(String json) {
      Gson gson = new Gson();
      return gson.fromJson(json, QualityGateDetails.class);
    }

    public static class Conditions {
      private final String metric;
      private final String op;
      private final String warning;
      private final String actual;
      private final String level;

      private Conditions(String metric, String op, String values, String actual, String level) {
        this.metric = metric;
        this.op = op;
        this.warning = values;
        this.actual = actual;
        this.level = level;
      }

      String getMetric() {
        return metric;
      }

      String getOp() {
        return op;
      }

      String getWarning() {
        return warning;
      }

      String getActual() {
        return actual;
      }

      String getLevel() {
        return level;
      }
    }
  }
}
