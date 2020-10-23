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
package org.sonar.db.version.v55;

import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.utils.System2;
import org.sonar.db.DbTester;
import org.sonar.db.version.MigrationStep;

import static org.assertj.core.api.Assertions.assertThat;

public class DeleteMeasuresWithRuleIdTest {

  @Rule
  public DbTester db = DbTester.createForSchema(System2.INSTANCE, DeleteMeasuresWithRuleIdTest.class, "schema.sql");

  MigrationStep migration;

  @Before
  public void setUp() {
    migration = new DeleteMeasuresWithRuleId(db.database());
  }

  @Test
  public void delete_measures_with_rule_id() throws Exception {
    db.prepareDbUnit(getClass(), "before.xml");

    migration.execute();

    List<Map<String, Object>> rows = db.select("select id from project_measures");
    assertThat(rows).hasSize(1);
    assertThat(rows.get(0).get("ID")).isEqualTo(10L);
  }

}
