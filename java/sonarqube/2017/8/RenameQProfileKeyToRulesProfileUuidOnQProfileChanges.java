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
package org.sonar.server.platform.db.migration.version.v65;

import java.sql.SQLException;
import org.sonar.db.Database;
import org.sonar.server.platform.db.migration.def.VarcharColumnDef;
import org.sonar.server.platform.db.migration.sql.RenameColumnsBuilder;
import org.sonar.server.platform.db.migration.step.DdlChange;

public class RenameQProfileKeyToRulesProfileUuidOnQProfileChanges extends DdlChange {

  private static final String TABLE_NAME = "qprofile_changes";
  private static final String OLD_COLUMN = "qprofile_key";
  private static final String NEW_COLUMN = "rules_profile_uuid";

  public RenameQProfileKeyToRulesProfileUuidOnQProfileChanges(Database db) {
    super(db);
  }

  @Override
  public void execute(Context context) throws SQLException {
    VarcharColumnDef newColumn = VarcharColumnDef.newVarcharColumnDefBuilder()
      .setColumnName(NEW_COLUMN)
      .setLimit(255)
      .setIsNullable(false)
      .build();

    context.execute(
      new RenameColumnsBuilder(getDialect(), TABLE_NAME)
        .renameColumn(OLD_COLUMN, newColumn)
        .build());
  }
}
