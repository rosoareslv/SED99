/*
 * SonarQube, open source software quality management tool.
 * Copyright (C) 2008-2014 SonarSource
 * mailto:contact AT sonarsource DOT com
 *
 * SonarQube is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * SonarQube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.server.source.index;

import org.apache.commons.io.Charsets;
import org.apache.commons.lang.StringUtils;
import org.elasticsearch.action.update.UpdateRequest;
import org.sonar.api.utils.text.JsonWriter;
import org.sonar.core.source.db.FileSourceDto;
import org.sonar.core.source.db.FileSourceDto.Type;
import org.sonar.server.db.DbClient;
import org.sonar.server.db.ResultSetIterator;
import org.sonar.server.es.EsUtils;
import org.sonar.server.source.db.FileSourceDb;

import javax.annotation.Nullable;

import java.io.ByteArrayOutputStream;
import java.io.OutputStreamWriter;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/**
 * Scroll over table FILE_SOURCES and directly parse data required to
 * populate the index sourcelines
 */
public class SourceFileResultSetIterator extends ResultSetIterator<SourceFileResultSetIterator.Row> {

  public static class Row {
    private final String fileUuid, projectUuid;
    private final long updatedAt;
    private final List<UpdateRequest> lineUpdateRequests = new ArrayList<>();

    public Row(String projectUuid, String fileUuid, long updatedAt) {
      this.projectUuid = projectUuid;
      this.fileUuid = fileUuid;
      this.updatedAt = updatedAt;
    }

    public String getProjectUuid() {
      return projectUuid;
    }

    public String getFileUuid() {
      return fileUuid;
    }

    public long getUpdatedAt() {
      return updatedAt;
    }

    public List<UpdateRequest> getLineUpdateRequests() {
      return lineUpdateRequests;
    }
  }

  private static final String[] FIELDS = {
    "project_uuid",
    "file_uuid",
    "updated_at",
    "binary_data"
  };

  private static final String SQL_ALL = String.format("SELECT %s FROM file_sources WHERE data_type='%s' ", StringUtils.join(FIELDS, ","), Type.SOURCE);
  private static final String AFTER_DATE_FILTER = " AND updated_at>?";
  private static final String PROJECT_FILTER = " AND project_uuid=?";

  public static SourceFileResultSetIterator create(DbClient dbClient, Connection connection, long afterDate, @Nullable String projectUuid) {
    try {
      String sql = createSQL(afterDate, projectUuid);
      // rows are big, so they are scrolled once at a time (one row in memory at a time)
      PreparedStatement stmt = dbClient.newScrollingSingleRowSelectStatement(connection, sql);
      int index = 1;
      if (afterDate > 0L) {
        stmt.setLong(index, afterDate);
        index++;
      }
      if (projectUuid != null) {
        stmt.setString(index, projectUuid);
      }
      return new SourceFileResultSetIterator(stmt);
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to prepare SQL request to select all file sources", e);
    }
  }

  private static String createSQL(long afterDate, @Nullable String projectUuid) {
    String sql = SQL_ALL;
    if (afterDate > 0L || projectUuid != null) {
      if (afterDate > 0L) {
        sql += AFTER_DATE_FILTER;
      }
      if (projectUuid != null) {
        sql += PROJECT_FILTER;
      }
    }
    return sql;
  }

  private SourceFileResultSetIterator(PreparedStatement stmt) throws SQLException {
    super(stmt);
  }

  @Override
  protected Row read(ResultSet rs) throws SQLException {
    String projectUuid = rs.getString(1);
    String fileUuid = rs.getString(2);
    Date updatedAt = new Date(rs.getLong(3));
    FileSourceDb.Data data = FileSourceDto.decodeSourceData(rs.getBinaryStream(4));
    return toRow(projectUuid, fileUuid, updatedAt, data);
  }

  /**
   * Convert protobuf message to data required for Elasticsearch indexing
   */
  public static Row toRow(String projectUuid, String fileUuid, Date updatedAt, FileSourceDb.Data data) {
    Row result = new Row(projectUuid, fileUuid, updatedAt.getTime());
    for (FileSourceDb.Line line : data.getLinesList()) {
      ByteArrayOutputStream bytes = new ByteArrayOutputStream();

      // all the fields must be present, even if value is null
      JsonWriter writer = JsonWriter.of(new OutputStreamWriter(bytes, Charsets.UTF_8)).setSerializeNulls(true);
      writer.beginObject();
      writer.prop(SourceLineIndexDefinition.FIELD_PROJECT_UUID, projectUuid);
      writer.prop(SourceLineIndexDefinition.FIELD_FILE_UUID, fileUuid);
      writer.prop(SourceLineIndexDefinition.FIELD_LINE, line.getLine());
      writer.prop(SourceLineIndexDefinition.FIELD_UPDATED_AT, EsUtils.formatDateTime(updatedAt));
      writer.prop(SourceLineIndexDefinition.FIELD_SCM_REVISION, line.getScmRevision());
      writer.prop(SourceLineIndexDefinition.FIELD_SCM_AUTHOR, line.getScmAuthor());
      writer.prop(SourceLineIndexDefinition.FIELD_SCM_DATE, EsUtils.formatDateTime(line.hasScmDate() ? new Date(line.getScmDate()) : null));

      // unit tests
      if (line.hasUtLineHits()) {
        writer.prop(SourceLineIndexDefinition.FIELD_UT_LINE_HITS, line.getUtLineHits());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_UT_LINE_HITS).valueObject(null);
      }
      if (line.hasUtConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_UT_CONDITIONS, line.getUtConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_UT_CONDITIONS).valueObject(null);
      }
      if (line.hasUtCoveredConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_UT_COVERED_CONDITIONS, line.getUtCoveredConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_UT_COVERED_CONDITIONS).valueObject(null);
      }

      // IT
      if (line.hasItLineHits()) {
        writer.prop(SourceLineIndexDefinition.FIELD_IT_LINE_HITS, line.getItLineHits());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_IT_LINE_HITS).valueObject(null);
      }
      if (line.hasItConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_IT_CONDITIONS, line.getItConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_IT_CONDITIONS).valueObject(null);
      }
      if (line.hasItCoveredConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_IT_COVERED_CONDITIONS, line.getItCoveredConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_IT_COVERED_CONDITIONS).valueObject(null);
      }

      // Overall coverage
      if (line.hasOverallLineHits()) {
        writer.prop(SourceLineIndexDefinition.FIELD_OVERALL_LINE_HITS, line.getOverallLineHits());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_OVERALL_LINE_HITS).valueObject(null);
      }
      if (line.hasOverallConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_OVERALL_CONDITIONS, line.getOverallConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_OVERALL_CONDITIONS).valueObject(null);
      }
      if (line.hasOverallCoveredConditions()) {
        writer.prop(SourceLineIndexDefinition.FIELD_OVERALL_COVERED_CONDITIONS, line.getOverallCoveredConditions());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_OVERALL_COVERED_CONDITIONS).valueObject(null);
      }

      if (line.hasHighlighting()) {
        writer.prop(SourceLineIndexDefinition.FIELD_HIGHLIGHTING, line.getHighlighting());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_HIGHLIGHTING).valueObject(null);
      }
      if (line.hasSymbols()) {
        writer.prop(SourceLineIndexDefinition.FIELD_SYMBOLS, line.getSymbols());
      } else {
        writer.name(SourceLineIndexDefinition.FIELD_SYMBOLS).valueObject(null);
      }
      writer.name(SourceLineIndexDefinition.FIELD_DUPLICATIONS).valueObject(line.getDuplicationList());
      writer.prop(SourceLineIndexDefinition.FIELD_SOURCE, line.hasSource() ? line.getSource() : null);
      writer.endObject().close();

      // This is an optimization to reduce memory consumption and multiple conversions from Map to JSON.
      // UpdateRequest#doc() and #upsert() take the same parameter values, so:
      // - passing the same Map would execute two JSON serializations
      // - Map is a useless temporarily structure: read JDBC result set -> convert to map -> convert to JSON. Generating
      // directly JSON from result set is more efficient.
      byte[] jsonDoc = bytes.toByteArray();
      UpdateRequest updateRequest = new UpdateRequest(SourceLineIndexDefinition.INDEX, SourceLineIndexDefinition.TYPE, SourceLineIndexDefinition.docKey(fileUuid, line.getLine()))
        .routing(projectUuid)
        .doc(jsonDoc)
        .upsert(jsonDoc);
      result.lineUpdateRequests.add(updateRequest);
    }
    return result;
  }
}
