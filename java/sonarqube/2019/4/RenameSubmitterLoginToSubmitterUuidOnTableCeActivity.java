/*
 * SonarQube
 * Copyright (C) 2009-2019 SonarSource SA
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
package org.sonar.server.platform.db.migration.version.v72;

import java.sql.SQLException;
import org.sonar.db.Database;
import org.sonar.server.platform.db.migration.es.MigrationEsClient;
import org.sonar.server.platform.db.migration.sql.RenameColumnsBuilder;
import org.sonar.server.platform.db.migration.step.DdlChange;

import static org.sonar.server.platform.db.migration.def.VarcharColumnDef.newVarcharColumnDefBuilder;

public class RenameSubmitterLoginToSubmitterUuidOnTableCeActivity extends DdlChange {

  private final MigrationEsClient esClient;

  public RenameSubmitterLoginToSubmitterUuidOnTableCeActivity(Database db, MigrationEsClient esClient) {
    super(db);
    this.esClient = esClient;
  }

  @Override
  public void execute(Context context) throws SQLException {
    context.execute(new RenameColumnsBuilder(getDialect(), "ce_activity")
      .renameColumn("submitter_login",
        newVarcharColumnDefBuilder()
          .setColumnName("submitter_uuid")
          .setLimit(255)
          .setIsNullable(true)
          .build())
      .build());

    esClient.deleteIndexes("users");
  }
}
