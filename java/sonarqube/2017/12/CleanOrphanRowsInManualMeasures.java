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
import org.sonar.server.platform.db.migration.step.DataChange;
import org.sonar.server.platform.db.migration.step.MassUpdate;
import org.sonar.server.platform.db.migration.step.Select;
import org.sonar.server.platform.db.migration.step.SqlStatement;

/**
 * Deletes rows from table MANUAL_MEASURES which:
 * <ul>
 *   <li>has no component uuid</li>
 *   <li>or reference a non existing component</li>
 *   <li>or reference a disabled component</li>
 *   <li>or reference a component which is neither a project, nor a view, nor a module nor a subview</li>
 * </ul>
 */
public class CleanOrphanRowsInManualMeasures extends DataChange {

  private static final String SCOPE_PROJECT = "PRJ";
  private static final String QUALIFIER_PROJECT = "TRK";
  private static final String QUALIFIER_VIEW = "VW";
  private static final String QUALIFIER_MODULE = "BRC";
  private static final String QUALIFIER_SUBVIEW = "SVW";

  public CleanOrphanRowsInManualMeasures(Database db) {
    super(db);
  }

  @Override
  protected void execute(Context context) throws SQLException {
    MassUpdate massUpdate = context.prepareMassUpdate();
    massUpdate.select("select" +
      "   mm.id" +
      " from manual_measures mm" +
      " where" +
      "   mm.component_uuid is null" +
      "   or not exists (" +
      "     select" +
      "       1" +
      "     from projects p" +
      "     where" +
      "       p.uuid = mm.component_uuid" +
      "       and p.scope = ?" +
      "       and p.qualifier in (?,?,?,?)" +
      "       and p.enabled = ?" +
      "  )")
      .setString(1, SCOPE_PROJECT)
      .setString(2, QUALIFIER_PROJECT)
      .setString(3, QUALIFIER_VIEW)
      .setString(4, QUALIFIER_MODULE)
      .setString(5, QUALIFIER_SUBVIEW)
      .setBoolean(6, true);
    massUpdate.update("delete from manual_measures where id = ?");
    massUpdate.rowPluralName("orphan properties");
    massUpdate.execute(CleanOrphanRowsInManualMeasures::handle);
  }

  private static boolean handle(Select.Row row, SqlStatement update) throws SQLException {
    long id = row.getLong(1);

    update.setLong(1, id);

    return true;
  }
}
