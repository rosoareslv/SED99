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
package org.sonar.server.platform.db.migration.version.v62;

import com.google.common.collect.ImmutableList;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.List;
import org.sonar.api.utils.log.Logger;
import org.sonar.api.utils.log.Loggers;
import org.sonar.db.Database;
import org.sonar.db.DatabaseUtils;
import org.sonar.server.platform.db.migration.sql.DropTableBuilder;
import org.sonar.server.platform.db.migration.step.DdlChange;

import static org.sonar.core.util.stream.MoreCollectors.toList;

public class DropIssueFiltersTables extends DdlChange {

  private static final Logger LOGGER = Loggers.get(DropIssueFiltersTables.class);

  private static final List<String> TABLES_TO_DROP = ImmutableList.of("issue_filters", "issue_filter_favourites");

  private final Database db;

  public DropIssueFiltersTables(Database db) {
    super(db);
    this.db = db;
  }

  @Override
  public void execute(Context context) throws SQLException {
    List<String> tablesToDrop = getEffectiveTablesToDrop();
    LOGGER.info("Removing tables {}", tablesToDrop);
    context.execute(tablesToDrop
      .stream()
      .flatMap(table -> new DropTableBuilder(db.getDialect(), table).build().stream())
      .collect(toList()));
  }

  private List<String> getEffectiveTablesToDrop() throws SQLException {
    try (Connection connection = db.getDataSource().getConnection()) {
      return TABLES_TO_DROP.stream()
        .filter(table -> DatabaseUtils.tableExists(table, connection))
        .collect(toList());
    }
  }
}
