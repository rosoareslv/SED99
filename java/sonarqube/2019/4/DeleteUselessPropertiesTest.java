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
package org.sonar.server.platform.db.migration.version.v63;

import com.google.common.collect.ImmutableMap;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.db.CoreDbTester;

import static java.lang.String.valueOf;
import static org.assertj.core.api.Assertions.assertThat;

public class DeleteUselessPropertiesTest {

  private static final String TABLE_PROPERTIES = "properties";
  private static final int COMPONENT_ID_1 = 125;
  private static final int COMPONENT_ID_2 = 604;

  @Rule
  public CoreDbTester db = CoreDbTester.createForSchema(DeleteUselessPropertiesTest.class, "properties.sql");

  private DeleteUselessProperties underTest = new DeleteUselessProperties(db.database());

  @Test
  public void migration_has_no_effect_on_empty_tables() throws SQLException {
    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_removes_hours_in_day_setting() throws SQLException {
    insertProperty("sonar.technicalDebt.hoursInDay", null);

    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_removes_create_user_setting() throws SQLException {
    insertProperty("sonar.authenticator.createUser", null);

    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_removes_period2_to_period5_settings() throws SQLException {
    insertProperty("sonar.timemachine.period2", null);
    insertProperty("sonar.timemachine.period3", null);
    insertProperty("sonar.timemachine.period4", null);
    insertProperty("sonar.timemachine.period5", null);

    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_removes_period2_to_period5_settings_related_to_component() throws SQLException {
    insertProperty("sonar.timemachine.period2", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period2", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period2", COMPONENT_ID_2);
    insertProperty("sonar.timemachine.period3", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period4", COMPONENT_ID_2);
    insertProperty("sonar.timemachine.period5", COMPONENT_ID_2);

    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_removes_period2_to_period5_settings_related_to_qualifiers() throws SQLException {
    insertProperty("sonar.timemachine.period2.TRK", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period2.TRK", null);
    insertProperty("sonar.timemachine.period2.VW", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period2.DEV", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period2", COMPONENT_ID_1);
    insertProperty("sonar.timemachine.period3", COMPONENT_ID_2);
    insertProperty("sonar.timemachine.period3.TRK", COMPONENT_ID_2);

    underTest.execute();

    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isZero();
  }

  @Test
  public void migration_ignores_not_relevant_settings() throws SQLException {
    insertProperty("sonar.core.serverBaseURL", null);
    insertProperty("sonar.dbcleaner.cleanDirectory", null);
    insertProperty("sonar.dbcleaner.cleanDirectory", COMPONENT_ID_1);
    // Only these settings should be removed
    insertProperty("sonar.timemachine.period4", null);
    insertProperty("sonar.timemachine.period5", null);

    underTest.execute();

    verifyPropertyKeys("sonar.core.serverBaseURL", "sonar.dbcleaner.cleanDirectory");
    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isEqualTo(3);
  }

  @Test
  public void migration_is_reentrant() throws SQLException {
    insertProperty("sonar.core.serverBaseURL", null);
    insertProperty("sonar.dbcleaner.cleanDirectory", null);
    insertProperty("sonar.dbcleaner.cleanDirectory", COMPONENT_ID_1);
    // Only these settings should be removed
    insertProperty("sonar.timemachine.period4", null);
    insertProperty("sonar.timemachine.period5", null);

    underTest.execute();
    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isEqualTo(3);

    underTest.execute();
    assertThat(db.countRowsOfTable(TABLE_PROPERTIES)).isEqualTo(3);
  }

  private void insertProperty(String key, @Nullable Integer componentId) {
    Map<String, Object> values = new HashMap<>(ImmutableMap.of(
      "PROP_KEY", key,
      "IS_EMPTY", false,
      "CREATED_AT", 456789));
    if (componentId != null) {
      values.put("RESOURCE_ID", valueOf(componentId));
    }
    db.executeInsert(TABLE_PROPERTIES, values);
  }

  private void verifyPropertyKeys(String... propertyKeys) {
    List<Map<String, Object>> rows = db.select("select prop_key from " + TABLE_PROPERTIES);
    Set<String> result = rows.stream().map(cols -> (String)cols.get("PROP_KEY")).collect(Collectors.toSet());
    assertThat(result).containsOnly(propertyKeys);
  }

}
