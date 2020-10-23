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
package org.sonar.scanner.mediumtest;

import org.sonar.api.rule.RuleKey;
import org.sonar.api.utils.DateUtils;
import org.sonar.batch.bootstrapper.Batch;
import org.sonar.batch.bootstrapper.EnvironmentInformation;
import org.sonar.batch.bootstrapper.IssueListener;
import org.sonar.batch.bootstrapper.LogOutput;
import com.google.common.collect.Table;
import com.google.common.collect.HashBasedTable;
import org.sonarqube.ws.QualityProfiles.SearchWsResponse.QualityProfile;
import org.apache.commons.io.FileUtils;

import javax.annotation.Nullable;

import org.sonarqube.ws.Rules.ListResponse.Rule;
import org.sonar.api.server.rule.RulesDefinition.Repository;
import org.sonar.api.server.rule.RulesDefinition;
import org.sonar.scanner.issue.tracking.ServerLineHashesLoader;
import org.sonar.scanner.mediumtest.FakePluginInstaller;
import org.sonar.scanner.mediumtest.TaskResult;
import org.sonar.scanner.protocol.input.GlobalRepositories;
import org.sonar.scanner.protocol.input.ScannerInput.ServerIssue;
import org.sonar.scanner.report.ReportPublisher;
import org.sonar.scanner.repository.FileData;
import org.sonar.scanner.repository.GlobalRepositoriesLoader;
import org.sonar.scanner.repository.ProjectRepositories;
import org.sonar.scanner.repository.ProjectRepositoriesLoader;
import org.sonar.scanner.repository.QualityProfileLoader;
import org.sonar.scanner.repository.ServerIssuesLoader;
import org.sonar.scanner.rule.ActiveRulesLoader;
import org.sonar.scanner.rule.LoadedActiveRule;
import org.sonar.scanner.rule.RulesLoader;
import com.google.common.base.Function;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import org.sonar.api.CoreProperties;
import org.sonar.api.Plugin;
import org.sonar.api.batch.debt.internal.DefaultDebtModel;
import org.sonar.api.measures.CoreMetrics;
import org.sonar.api.measures.Metric;

/**
 * Main utility class for writing batch medium tests.
 * 
 */
public class BatchMediumTester {

  public static final String MEDIUM_TEST_ENABLED = "sonar.mediumTest.enabled";
  private Batch batch;
  private static Path workingDir = null;
  private static Path globalWorkingDir = null;

  private static void createWorkingDirs() throws IOException {
    destroyWorkingDirs();

    workingDir = java.nio.file.Files.createTempDirectory("mediumtest-working-dir");
    globalWorkingDir = java.nio.file.Files.createTempDirectory("mediumtest-global-working-dir");
  }

  private static void destroyWorkingDirs() throws IOException {
    if (workingDir != null) {
      FileUtils.deleteDirectory(workingDir.toFile());
      workingDir = null;
    }

    if (globalWorkingDir != null) {
      FileUtils.deleteDirectory(globalWorkingDir.toFile());
      globalWorkingDir = null;
    }

  }

  public static BatchMediumTesterBuilder builder() {
    try {
      createWorkingDirs();
    } catch (IOException e) {
      e.printStackTrace();
    }

    BatchMediumTesterBuilder builder = new BatchMediumTesterBuilder().registerCoreMetrics();
    builder.bootstrapProperties.put(MEDIUM_TEST_ENABLED, "true");
    builder.bootstrapProperties.put(ReportPublisher.KEEP_REPORT_PROP_KEY, "true");
    builder.bootstrapProperties.put(CoreProperties.WORKING_DIRECTORY, workingDir.toString());
    builder.bootstrapProperties.put("sonar.userHome", globalWorkingDir.toString());
    return builder;
  }

  public static class BatchMediumTesterBuilder {
    private final FakeGlobalRepositoriesLoader globalRefProvider = new FakeGlobalRepositoriesLoader();
    private final FakeProjectRepositoriesLoader projectRefProvider = new FakeProjectRepositoriesLoader();
    private final FakePluginInstaller pluginInstaller = new FakePluginInstaller();
    private final FakeServerIssuesLoader serverIssues = new FakeServerIssuesLoader();
    private final FakeServerLineHashesLoader serverLineHashes = new FakeServerLineHashesLoader();
    private final Map<String, String> bootstrapProperties = new HashMap<>();
    private final FakeRulesLoader rulesLoader = new FakeRulesLoader();
    private final FakeQualityProfileLoader qualityProfiles = new FakeQualityProfileLoader();
    private final FakeActiveRulesLoader activeRules = new FakeActiveRulesLoader();
    private boolean associated = true;
    private LogOutput logOutput = null;

    public BatchMediumTester build() {
      return new BatchMediumTester(this);
    }

    public BatchMediumTesterBuilder setAssociated(boolean associated) {
      this.associated = associated;
      return this;
    }

    public BatchMediumTesterBuilder setLogOutput(LogOutput logOutput) {
      this.logOutput = logOutput;
      return this;
    }

    public BatchMediumTesterBuilder registerPlugin(String pluginKey, File location) {
      pluginInstaller.add(pluginKey, location);
      return this;
    }

    public BatchMediumTesterBuilder registerPlugin(String pluginKey, Plugin instance) {
      pluginInstaller.add(pluginKey, instance);
      return this;
    }

    public BatchMediumTesterBuilder registerCoreMetrics() {
      for (Metric<?> m : CoreMetrics.getMetrics()) {
        registerMetric(m);
      }
      return this;
    }

    public BatchMediumTesterBuilder registerMetric(Metric<?> metric) {
      globalRefProvider.add(metric);
      return this;
    }

    public BatchMediumTesterBuilder addQProfile(String language, String name) {
      qualityProfiles.add(language, name);
      return this;
    }

    public BatchMediumTesterBuilder addRule(Rule rule) {
      rulesLoader.addRule(rule);
      return this;
    }

    public BatchMediumTesterBuilder addRule(String key, String repoKey, String internalKey, String name) {
      Rule.Builder builder = Rule.newBuilder();
      builder.setKey(key);
      builder.setRepository(repoKey);
      if (internalKey != null) {
        builder.setInternalKey(internalKey);
      }
      builder.setName(name);

      rulesLoader.addRule(builder.build());
      return this;
    }

    public BatchMediumTesterBuilder addRules(RulesDefinition rulesDefinition) {
      RulesDefinition.Context context = new RulesDefinition.Context();
      rulesDefinition.define(context);
      List<Repository> repositories = context.repositories();
      for (Repository repo : repositories) {
        for (RulesDefinition.Rule rule : repo.rules()) {
          this.addRule(rule.key(), rule.repository().key(), rule.internalKey(), rule.name());
        }
      }
      return this;
    }

    public BatchMediumTesterBuilder addDefaultQProfile(String language, String name) {
      addQProfile(language, name);
      globalRefProvider.globalSettings().put("sonar.profile." + language, name);
      return this;
    }

    public BatchMediumTesterBuilder setPreviousAnalysisDate(Date previousAnalysis) {
      projectRefProvider.setLastAnalysisDate(previousAnalysis);
      return this;
    }

    public BatchMediumTesterBuilder bootstrapProperties(Map<String, String> props) {
      bootstrapProperties.putAll(props);
      return this;
    }

    public BatchMediumTesterBuilder activateRule(LoadedActiveRule activeRule) {
      activeRules.addActiveRule(activeRule);
      return this;
    }

    public BatchMediumTesterBuilder addActiveRule(String repositoryKey, String ruleKey, @Nullable String templateRuleKey, String name, @Nullable String severity,
      @Nullable String internalKey, @Nullable String languag) {
      LoadedActiveRule r = new LoadedActiveRule();

      r.setInternalKey(internalKey);
      r.setRuleKey(RuleKey.of(repositoryKey, ruleKey));
      r.setName(name);
      r.setTemplateRuleKey(templateRuleKey);
      r.setLanguage(languag);
      r.setSeverity(severity);

      activeRules.addActiveRule(r);
      return this;
    }

    public BatchMediumTesterBuilder addFileData(String moduleKey, String path, FileData fileData) {
      projectRefProvider.addFileData(moduleKey, path, fileData);
      return this;
    }

    public BatchMediumTesterBuilder setLastBuildDate(Date d) {
      projectRefProvider.setLastAnalysisDate(d);
      return this;
    }

    public BatchMediumTesterBuilder mockServerIssue(ServerIssue issue) {
      serverIssues.getServerIssues().add(issue);
      return this;
    }

    public BatchMediumTesterBuilder mockLineHashes(String fileKey, String[] lineHashes) {
      serverLineHashes.byKey.put(fileKey, lineHashes);
      return this;
    }

  }

  public void start() {
    batch.start();
  }

  public void stop() {
    batch.stop();
    try {
      destroyWorkingDirs();
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  public void syncProject(String projectKey) {
    batch.syncProject(projectKey);
  }

  private BatchMediumTester(BatchMediumTesterBuilder builder) {
    Batch.Builder batchBuilder = Batch.builder()
      .setEnableLoggingConfiguration(true)
      .addComponents(
        new EnvironmentInformation("mediumTest", "1.0"),
        builder.pluginInstaller,
        builder.globalRefProvider,
        builder.qualityProfiles,
        builder.rulesLoader,
        builder.projectRefProvider,
        builder.activeRules,
        new DefaultDebtModel())
      .setBootstrapProperties(builder.bootstrapProperties)
      .setLogOutput(builder.logOutput);

    if (builder.associated) {
      batchBuilder.addComponents(
        builder.serverIssues);
    }
    batch = batchBuilder.build();
  }

  public TaskBuilder newTask() {
    return new TaskBuilder(this);
  }

  public TaskBuilder newScanTask(File sonarProps) {
    Properties prop = new Properties();
    try (Reader reader = new InputStreamReader(new FileInputStream(sonarProps), StandardCharsets.UTF_8)) {
      prop.load(reader);
    } catch (Exception e) {
      throw new IllegalStateException("Unable to read configuration file", e);
    }
    TaskBuilder builder = new TaskBuilder(this);
    builder.property("sonar.projectBaseDir", sonarProps.getParentFile().getAbsolutePath());
    for (Map.Entry<Object, Object> entry : prop.entrySet()) {
      builder.property(entry.getKey().toString(), entry.getValue().toString());
    }
    return builder;
  }

  public static class TaskBuilder {
    private final Map<String, String> taskProperties = new HashMap<>();
    private BatchMediumTester tester;
    private IssueListener issueListener = null;

    public TaskBuilder(BatchMediumTester tester) {
      this.tester = tester;
    }

    public TaskResult start() {
      TaskResult result = new TaskResult();
      Map<String, String> props = new HashMap<>();
      props.putAll(taskProperties);
      if (issueListener != null) {
        tester.batch.executeTask(props, result, issueListener);
      } else {
        tester.batch.executeTask(props, result);
      }
      return result;
    }

    public TaskBuilder properties(Map<String, String> props) {
      taskProperties.putAll(props);
      return this;
    }

    public TaskBuilder property(String key, String value) {
      taskProperties.put(key, value);
      return this;
    }

    public TaskBuilder setIssueListener(IssueListener issueListener) {
      this.issueListener = issueListener;
      return this;
    }
  }

  private static class FakeRulesLoader implements RulesLoader {
    private List<org.sonarqube.ws.Rules.ListResponse.Rule> rules = new LinkedList<>();

    public FakeRulesLoader addRule(Rule rule) {
      rules.add(rule);
      return this;
    }

    @Override
    public List<Rule> load() {
      return rules;
    }
  }

  private static class FakeActiveRulesLoader implements ActiveRulesLoader {
    private List<LoadedActiveRule> activeRules = new LinkedList<>();

    public void addActiveRule(LoadedActiveRule activeRule) {
      this.activeRules.add(activeRule);
    }

    @Override
    public List<LoadedActiveRule> load(String qualityProfileKey) {
      return activeRules;
    }
  }

  private static class FakeGlobalRepositoriesLoader implements GlobalRepositoriesLoader {

    private int metricId = 1;

    private GlobalRepositories ref = new GlobalRepositories();

    @Override
    public GlobalRepositories load() {
      return ref;
    }

    public Map<String, String> globalSettings() {
      return ref.globalSettings();
    }

    public FakeGlobalRepositoriesLoader add(Metric<?> metric) {
      Boolean optimizedBestValue = metric.isOptimizedBestValue();
      ref.metrics().add(new org.sonar.scanner.protocol.input.Metric(metricId,
        metric.key(),
        metric.getType().name(),
        metric.getDescription(),
        metric.getDirection(),
        metric.getName(),
        metric.getQualitative(),
        metric.getUserManaged(),
        metric.getWorstValue(),
        metric.getBestValue(),
        optimizedBestValue != null ? optimizedBestValue : false));
      metricId++;
      return this;
    }

  }

  private static class FakeProjectRepositoriesLoader implements ProjectRepositoriesLoader {

    private Table<String, String, FileData> fileDataTable = HashBasedTable.create();
    private Date lastAnalysisDate;

    @Override
    public ProjectRepositories load(String projectKey, boolean isIssuesMode) {
      Table<String, String, String> settings = HashBasedTable.create();
      return new ProjectRepositories(settings, fileDataTable, lastAnalysisDate);
    }

    public FakeProjectRepositoriesLoader addFileData(String moduleKey, String path, FileData fileData) {
      fileDataTable.put(moduleKey, path, fileData);
      return this;
    }

    public FakeProjectRepositoriesLoader setLastAnalysisDate(Date d) {
      lastAnalysisDate = d;
      return this;
    }

  }

  private static class FakeQualityProfileLoader implements QualityProfileLoader {

    private List<QualityProfile> qualityProfiles = new LinkedList<>();

    public void add(String language, String name) {
      qualityProfiles.add(QualityProfile.newBuilder()
        .setLanguage(language)
        .setKey(name)
        .setName(name)
        .setRulesUpdatedAt(DateUtils.formatDateTime(new Date(1234567891212L)))
        .build());
    }

    @Override
    public List<QualityProfile> load(String projectKey, String profileName) {
      return qualityProfiles;
    }

    @Override
    public List<QualityProfile> loadDefault(String profileName) {
      return qualityProfiles;
    }
  }

  private static class FakeServerIssuesLoader implements ServerIssuesLoader {

    private List<ServerIssue> serverIssues = new ArrayList<>();

    public List<ServerIssue> getServerIssues() {
      return serverIssues;
    }

    @Override
    public void load(String componentKey, Function<ServerIssue, Void> consumer) {
      for (ServerIssue serverIssue : serverIssues) {
        consumer.apply(serverIssue);
      }
    }
  }

  private static class FakeServerLineHashesLoader implements ServerLineHashesLoader {
    private Map<String, String[]> byKey = new HashMap<>();

    @Override
    public String[] getLineHashes(String fileKey) {
      if (byKey.containsKey(fileKey)) {
        return byKey.get(fileKey);
      } else {
        throw new IllegalStateException("You forgot to mock line hashes for " + fileKey);
      }
    }
  }

}
