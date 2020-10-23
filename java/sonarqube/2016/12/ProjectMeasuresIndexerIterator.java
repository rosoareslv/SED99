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

package org.sonar.db.measure;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableSet;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import javax.annotation.CheckForNull;
import javax.annotation.Nullable;
import org.apache.commons.lang.StringUtils;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.Scopes;
import org.sonar.core.util.CloseableIterator;
import org.sonar.db.DatabaseUtils;
import org.sonar.db.DbSession;

import static org.sonar.api.measures.CoreMetrics.ALERT_STATUS_KEY;
import static org.sonar.api.measures.Metric.ValueType.BOOL;
import static org.sonar.api.measures.Metric.ValueType.FLOAT;
import static org.sonar.api.measures.Metric.ValueType.INT;
import static org.sonar.api.measures.Metric.ValueType.LEVEL;
import static org.sonar.api.measures.Metric.ValueType.MILLISEC;
import static org.sonar.api.measures.Metric.ValueType.PERCENT;
import static org.sonar.api.measures.Metric.ValueType.RATING;
import static org.sonar.api.measures.Metric.ValueType.WORK_DUR;
import static org.sonar.db.DatabaseUtils.repeatCondition;

public class ProjectMeasuresIndexerIterator extends CloseableIterator<ProjectMeasuresIndexerIterator.ProjectMeasures> {

  private static final Set<String> METRIC_TYPES = ImmutableSet.of(INT.name(), FLOAT.name(), PERCENT.name(), BOOL.name(), MILLISEC.name(), LEVEL.name(), RATING.name(),
    WORK_DUR.name());

  private static final Joiner METRICS_JOINER = Joiner.on("','");

  private static final String SQL_PROJECTS = "SELECT p.uuid, p.kee, p.name, s.uuid, s.created_at FROM projects p " +
    "LEFT OUTER JOIN snapshots s ON s.component_uuid=p.uuid AND s.islast=? " +
    "WHERE p.enabled=? AND p.scope=? AND p.qualifier=?";

  private static final String DATE_FILTER = " AND s.created_at>?";

  private static final String PROJECT_FILTER = " AND p.uuid=?";

  private static final String SQL_METRICS = "SELECT m.id, m.name FROM metrics m " +
    "WHERE m.val_type IN ('" + METRICS_JOINER.join(METRIC_TYPES) + "') " +
    "AND m.enabled=?";

  private static final String SQL_MEASURES = "SELECT pm.metric_id, pm.value, pm.variation_value_1, pm.text_value FROM project_measures pm " +
    "WHERE pm.component_uuid = ? AND pm.analysis_uuid = ? " +
    "AND pm.metric_id IN ({metricIds}) " +
    "AND (pm.value IS NOT NULL OR pm.variation_value_1 IS NOT NULL OR pm.text_value IS NOT NULL) " +
    "AND pm.person_id IS NULL ";

  private final PreparedStatement measuresStatement;
  private final Map<Long, String> metricKeysByIds;
  private final Iterator<Project> projects;

  private ProjectMeasuresIndexerIterator(PreparedStatement measuresStatement, Map<Long, String> metricKeysByIds, List<Project> projects) throws SQLException {
    this.measuresStatement = measuresStatement;
    this.metricKeysByIds = metricKeysByIds;
    this.projects = projects.iterator();
  }

  public static ProjectMeasuresIndexerIterator create(DbSession session, long afterDate, @Nullable String projectUuid) {
    try {
      Map<Long, String> metrics = selectMetricKeysByIds(session);
      List<Project> projects = selectProjects(session, afterDate, projectUuid);
      PreparedStatement projectsStatement = createMeasuresStatement(session, metrics.keySet());
      return new ProjectMeasuresIndexerIterator(projectsStatement, metrics, projects);
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to execute request to select all project measures", e);
    }
  }

  private static Map<Long, String> selectMetricKeysByIds(DbSession session) {
    Map<Long, String> metrics = new HashMap<>();
    try (PreparedStatement stmt = createMetricsStatement(session);
      ResultSet rs = stmt.executeQuery()) {
      while (rs.next()) {
        metrics.put(rs.getLong(1), rs.getString(2));
      }
      return metrics;
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to execute request to select all metrics", e);
    }
  }

  private static PreparedStatement createMetricsStatement(DbSession session) throws SQLException {
    PreparedStatement stmt = session.getConnection().prepareStatement(SQL_METRICS);
    stmt.setBoolean(1, true);
    return stmt;
  }

  private static List<Project> selectProjects(DbSession session, long afterDate, @Nullable String projectUuid) {
    List<Project> projects = new ArrayList<>();
    try (PreparedStatement stmt = createProjectsStatement(session, afterDate, projectUuid);
      ResultSet rs = stmt.executeQuery()) {
      while (rs.next()) {
        long analysisDate = rs.getLong(5);
        Project project = new Project(rs.getString(1), rs.getString(2), rs.getString(3), getString(rs, 4).orElseGet(() -> null), rs.wasNull() ? null : analysisDate);
        projects.add(project);
      }
      return projects;
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to execute request to select all projects", e);
    }
  }

  private static PreparedStatement createProjectsStatement(DbSession session, long afterDate, @Nullable String projectUuid) {
    try {
      String sql = SQL_PROJECTS;
      sql += afterDate <= 0L ? "" : DATE_FILTER;
      sql += projectUuid == null ? "" : PROJECT_FILTER;
      PreparedStatement stmt = session.getConnection().prepareStatement(sql);
      stmt.setBoolean(1, true);
      stmt.setBoolean(2, true);
      stmt.setString(3, Scopes.PROJECT);
      stmt.setString(4, Qualifiers.PROJECT);
      int index = 5;
      if (afterDate > 0L) {
        stmt.setLong(index, afterDate);
        index++;
      }
      if (projectUuid != null) {
        stmt.setString(index, projectUuid);
      }
      return stmt;
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to prepare SQL request to select all project measures", e);
    }
  }

  private static PreparedStatement createMeasuresStatement(DbSession session, Set<Long> metricIds) throws SQLException {
    try {
      String sql = StringUtils.replace(SQL_MEASURES, "{metricIds}", repeatCondition("?", metricIds.size(), ","));
      PreparedStatement stmt = session.getConnection().prepareStatement(sql);
      int index = 3;
      for (Long metricId : metricIds) {
        stmt.setLong(index, metricId);
        index++;
      }
      return stmt;
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to prepare SQL request to select measures", e);
    }
  }

  @Override
  @CheckForNull
  protected ProjectMeasures doNext() {
    if (!projects.hasNext()) {
      return null;
    }
    Project project = projects.next();
    Measures measures = selectMeasures(project.getUuid(), project.getAnalysisUuid());
    return new ProjectMeasures(project, measures);
  }

  private Measures selectMeasures(String projectUuid, @Nullable String analysisUuid) {
    Measures measures = new Measures();
    if (analysisUuid == null || metricKeysByIds.isEmpty()) {
      return measures;
    }
    ResultSet rs = null;
    try {
      measuresStatement.setString(1, projectUuid);
      measuresStatement.setString(2, analysisUuid);
      rs = measuresStatement.executeQuery();
      while (rs.next()) {
        readMeasure(rs, measures);
      }
      return measures;
    } catch (Exception e) {
      throw new IllegalStateException(String.format("Fail to execute request to select measures of project %s, analysis %s", projectUuid, analysisUuid), e);
    } finally {
      DatabaseUtils.closeQuietly(rs);
    }
  }

  private void readMeasure(ResultSet rs, Measures measures) throws SQLException {
    String metricKey = metricKeysByIds.get(rs.getLong(1));
    Optional<Double> value = metricKey.startsWith("new_") ? getDouble(rs, 3) : getDouble(rs, 2);
    if (value.isPresent()) {
      measures.addNumericMeasure(metricKey, value.get());
      return;
    } else if (ALERT_STATUS_KEY.equals(metricKey)) {
      String textValue = rs.getString(4);
      if (!rs.wasNull()) {
        measures.setQualityGateStatus(textValue);
        return;
      }
    }
    throw new IllegalArgumentException("Measure has no value");
  }

  @Override
  protected void doClose() throws Exception {
    measuresStatement.close();
  }

  private static Optional<Double> getDouble(ResultSet rs, int index) {
    try {
      Double value = rs.getDouble(index);
      if (!rs.wasNull()) {
        return Optional.of(value);
      }
      return Optional.empty();
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to get double value", e);
    }
  }

  private static Optional<String> getString(ResultSet rs, int index) {
    try {
      String value = rs.getString(index);
      if (!rs.wasNull()) {
        return Optional.of(value);
      }
      return Optional.empty();
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to get string value", e);
    }
  }

  public static class Project {
    private final String uuid;
    private final String key;
    private final String name;
    private final String analysisUuid;
    private final Long analysisDate;

    public Project(String uuid, String key, String name, @Nullable String analysisUuid, @Nullable Long analysisDate) {
      this.uuid = uuid;
      this.key = key;
      this.name = name;
      this.analysisUuid = analysisUuid;
      this.analysisDate = analysisDate;
    }

    public String getUuid() {
      return uuid;
    }

    public String getKey() {
      return key;
    }

    public String getName() {
      return name;
    }

    @CheckForNull
    public String getAnalysisUuid() {
      return analysisUuid;
    }

    @CheckForNull
    public Long getAnalysisDate() {
      return analysisDate;
    }
  }

  public static class Measures {

    private Map<String, Double> numericMeasures = new HashMap<>();
    private String qualityGateStatus;

    Measures addNumericMeasure(String metricKey, double value) {
      numericMeasures.put(metricKey, value);
      return this;
    }

    public Map<String, Double> getNumericMeasures() {
      return numericMeasures;
    }

    Measures setQualityGateStatus(@Nullable String qualityGateStatus) {
      this.qualityGateStatus = qualityGateStatus;
      return this;
    }

    @CheckForNull
    public String getQualityGateStatus() {
      return qualityGateStatus;
    }
  }

  public static class ProjectMeasures {
    private Project project;
    private Measures measures;

    public ProjectMeasures(Project project, Measures measures) {
      this.project = project;
      this.measures = measures;
    }

    public Project getProject() {
      return project;
    }

    public Measures getMeasures() {
      return measures;
    }

  }

}
