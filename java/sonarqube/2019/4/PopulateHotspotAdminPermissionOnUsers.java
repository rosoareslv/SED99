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
package org.sonar.server.platform.db.migration.version.v73;

import java.sql.SQLException;
import org.sonar.db.Database;
import org.sonar.server.platform.db.migration.SupportsBlueGreen;
import org.sonar.server.platform.db.migration.step.DataChange;
import org.sonar.server.platform.db.migration.step.MassUpdate;
import org.sonar.server.platform.db.migration.step.Select;
import org.sonar.server.platform.db.migration.step.SqlStatement;

@SupportsBlueGreen
public class PopulateHotspotAdminPermissionOnUsers extends DataChange {
  private static final String ISSUE_ADMIN_ROLE = "issueadmin";
  private static final String HOTSPOT_ADMIN_ROLE = "securityhotspotadmin";

  public PopulateHotspotAdminPermissionOnUsers(Database db) {
    super(db);
  }

  @Override
  protected void execute(Context context) throws SQLException {
    MassUpdate massUpdate = context.prepareMassUpdate();
    massUpdate.select("SELECT user_id, resource_id, organization_uuid" +
      "  FROM user_roles ur1" +
      "  WHERE role = ?" +
      "  AND NOT EXISTS (" +
      "    SELECT id" +
      "    FROM user_roles ur2" +
      "    WHERE role = ?" +
      "    AND ur1.user_id = ur2.user_id" +
      "    AND ur1.resource_id = ur2.resource_id" +
      "    AND ur1.organization_uuid = ur2.organization_uuid)")
      .setString(1, ISSUE_ADMIN_ROLE)
      .setString(2, HOTSPOT_ADMIN_ROLE);
    massUpdate.update("INSERT INTO user_roles (user_id, resource_id, organization_uuid, role) values (?,?,?,?)");
    massUpdate.rowPluralName("user roles");
    massUpdate.execute(PopulateHotspotAdminPermissionOnUsers::handle);
  }

  private static boolean handle(Select.Row row, SqlStatement update) throws SQLException {
    int userId = row.getInt(1);
    int resourceId = row.getInt(2);
    String organizationUuid = row.getString(3);

    update.setInt(1, userId);
    update.setInt(2, resourceId);
    update.setString(3, organizationUuid);
    update.setString(4, HOTSPOT_ADMIN_ROLE);
    return true;
  }
}
