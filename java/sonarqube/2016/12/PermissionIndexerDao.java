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
package org.sonar.server.permission.index;

import com.google.common.collect.Lists;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.apache.commons.dbutils.DbUtils;
import org.apache.commons.lang.StringUtils;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;

import static org.sonar.db.DatabaseUtils.executeLargeInputs;
import static org.sonar.db.DatabaseUtils.repeatCondition;

/**
 * No streaming because of union of joins -> no need to use ResultSetIterator
 */
public class PermissionIndexerDao {

  public static final class Dto {
    private final String projectUuid;
    private final long updatedAt;
    private final List<Long> users = Lists.newArrayList();
    private final List<String> groups = Lists.newArrayList();

    public Dto(String projectUuid, long updatedAt) {
      this.projectUuid = projectUuid;
      this.updatedAt = updatedAt;
    }

    public String getProjectUuid() {
      return projectUuid;
    }

    public long getUpdatedAt() {
      return updatedAt;
    }

    public List<Long> getUsers() {
      return users;
    }

    public Dto addUser(Long s) {
      users.add(s);
      return this;
    }

    public Dto addGroup(String s) {
      groups.add(s);
      return this;
    }

    public List<String> getGroups() {
      return groups;
    }
  }

  private static final String SQL_TEMPLATE = "SELECT " +
    "  project_authorization.project as project, " +
    "  project_authorization.user_id as user_id, " +
    "  project_authorization.permission_group as permission_group, " +
    "  project_authorization.updated_at as updated_at " +
    "FROM ( " +

    // project is returned when no authorization
    "      SELECT " +
    "      projects.uuid AS project, " +
    "      projects.authorization_updated_at AS updated_at, " +
    "      NULL AS user_id, " +
    "      NULL  AS permission_group " +
    "      FROM projects " +
    "      WHERE " +
    "        projects.qualifier = 'TRK' " +
    "        AND projects.copy_component_uuid is NULL " +
    "        {projectsCondition} " +
    "      UNION " +

    // users

    "      SELECT " +
    "      projects.uuid AS project, " +
    "      projects.authorization_updated_at AS updated_at, " +
    "      user_roles.user_id  AS user_id, " +
    "      NULL  AS permission_group " +
    "      FROM projects " +
    "      INNER JOIN user_roles ON user_roles.resource_id = projects.id AND user_roles.role = 'user' " +
    "      WHERE " +
    "        projects.qualifier = 'TRK' " +
    "        AND projects.copy_component_uuid is NULL " +
    "        {projectsCondition} " +
    "      UNION " +

    // groups without Anyone

    "      SELECT " +
    "      projects.uuid AS project, " +
    "      projects.authorization_updated_at AS updated_at, " +
    "      NULL  AS user_id, " +
    "      groups.name  AS permission_group " +
    "      FROM projects " +
    "      INNER JOIN group_roles ON group_roles.resource_id = projects.id AND group_roles.role = 'user' " +
    "      INNER JOIN groups ON groups.id = group_roles.group_id " +
    "      WHERE " +
    "        projects.qualifier = 'TRK' " +
    "        AND projects.copy_component_uuid is NULL " +
    "        {projectsCondition} " +
    "        AND group_id IS NOT NULL " +
    "      UNION " +

    // Anyone groups

    "      SELECT " +
    "      projects.uuid AS project, " +
    "      projects.authorization_updated_at AS updated_at, " +
    "      NULL         AS user_id, " +
    "      'Anyone'     AS permission_group " +
    "      FROM projects " +
    "      INNER JOIN group_roles ON group_roles.resource_id = projects.id AND group_roles.role='user' " +
    "      WHERE " +
    "        projects.qualifier = 'TRK' " +
    "        AND projects.copy_component_uuid is NULL " +
    "        {projectsCondition} " +
    "        AND group_roles.group_id IS NULL " +
    "    ) project_authorization";

  List<Dto> selectAll(DbClient dbClient, DbSession session) {
    return doSelectByProjects(dbClient, session, Collections.emptyList());
  }

  List<Dto> selectByProjects(DbClient dbClient, DbSession session, List<String> projectUuids) {
    return executeLargeInputs(projectUuids, subProjectUuids -> doSelectByProjects(dbClient, session, subProjectUuids));
  }

  private static List<Dto> doSelectByProjects(DbClient dbClient, DbSession session, List<String> projectUuids) {
    try {
      Map<String, Dto> dtosByProjectUuid = new HashMap<>();
      PreparedStatement stmt = null;
      ResultSet rs = null;
      try {
        stmt = createStatement(dbClient, session, projectUuids);
        rs = stmt.executeQuery();
        while (rs.next()) {
          processRow(rs, dtosByProjectUuid);
        }
        return new ArrayList<>(dtosByProjectUuid.values());
      } finally {
        DbUtils.closeQuietly(rs);
        DbUtils.closeQuietly(stmt);
      }
    } catch (SQLException e) {
      throw new IllegalStateException("Fail to select authorizations", e);
    }
  }

  private static PreparedStatement createStatement(DbClient dbClient, DbSession session, List<String> projectUuids) throws SQLException {
    String sql;
    if (!projectUuids.isEmpty()) {
      sql = StringUtils.replace(SQL_TEMPLATE, "{projectsCondition}", " AND (" + repeatCondition("projects.uuid = ?", projectUuids.size(), "OR") + ")");
    } else {
      sql = StringUtils.replace(SQL_TEMPLATE, "{projectsCondition}", "");
    }
    PreparedStatement stmt = dbClient.getMyBatis().newScrollingSelectStatement(session, sql);
    if (!projectUuids.isEmpty()) {
      int index = 1;
      for (int i = 1; i <= 4; i++) {
        for (int uuidIndex = 0; uuidIndex < projectUuids.size(); uuidIndex++) {
          stmt.setString(index, projectUuids.get(uuidIndex));
          index++;
        }
      }
    }
    return stmt;
  }

  private static void processRow(ResultSet rs, Map<String, Dto> dtosByProjectUuid) throws SQLException {
    String projectUuid = rs.getString(1);
    String group = rs.getString(3);

    Dto dto = dtosByProjectUuid.get(projectUuid);
    if (dto == null) {
      long updatedAt = rs.getLong(4);
      dto = new Dto(projectUuid, updatedAt);
      dtosByProjectUuid.put(projectUuid, dto);
    }
    Long userId = rs.getLong(2);
    if (!rs.wasNull()) {
      dto.addUser(userId);
    }
    if (StringUtils.isNotBlank(group)) {
      dto.addGroup(group);
    }
  }
}
