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
package org.sonar.db.version;

import com.google.common.collect.ImmutableSet;
import java.util.Set;

public final class SqTables {
  /**
   * These tables are still involved in DB migrations, so potentially
   * incorrect collation must be fixed so that joins with other
   * tables are possible.
   *
   * @see org.sonar.db.charset.ColumnDef#isInSonarQubeTable()
   */
  public static final Set<String> OLD_DROPPED_TABLES = ImmutableSet.of(
    "active_dashboards",
    "activities",
    "dashboards",
    "issue_filters",
    "issue_filter_favourites",
    "measure_filters",
    "measure_filter_favourites",
    "widgets",
    "widget_properties");

  /**
   * List of all the tables.
   * This list is hardcoded because we didn't succeed in using java.sql.DatabaseMetaData#getTables() in the same way
   * for all the supported databases, particularly due to Oracle results.
   */
  public static final Set<String> TABLES = ImmutableSet.of(
    "active_rules",
    "active_rule_parameters",
    "authors",
    "ce_activity",
    "ce_queue",
    "ce_task_input",
    "ce_scanner_context",
    "duplications_index",
    "events",
    "file_sources",
    "groups",
    "groups_users",
    "group_roles",
    "internal_properties",
    "issues",
    "issue_changes",
    "loaded_templates",
    "manual_measures",
    "metrics",
    "notifications",
    "organizations",
    "permission_templates",
    "perm_templates_users",
    "perm_templates_groups",
    "perm_tpl_characteristics",
    "quality_gates",
    "quality_gate_conditions",
    "projects",
    "project_links",
    "project_measures",
    "project_qprofiles",
    "properties",
    "qprofile_changes",
    "resource_index",
    "rules",
    "rules_parameters",
    "rules_profiles",
    "rule_repositories",
    "schema_migrations",
    "snapshots",
    "users",
    "user_roles",
    "user_tokens",
    "webhook_deliveries");

  private SqTables() {
    // prevents instantiation
  }
}
