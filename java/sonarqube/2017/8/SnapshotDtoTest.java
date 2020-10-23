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
package org.sonar.db.component;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.api.utils.DateUtils.parseDate;

public class SnapshotDtoTest {

  @Test
  public void test_getter_and_setter() throws Exception {
    SnapshotDto snapshotDto = new SnapshotDto()
      .setId(10L)
      .setBuildDate(parseDate("2014-07-02").getTime())
      .setComponentUuid("uuid_21")
      .setLast(true)
      .setVersion("1.0")
      .setPeriodMode("mode1")
      .setPeriodParam("param1")
      .setIncremental(true)
      .setPeriodDate(parseDate("2014-06-01").getTime());

    assertThat(snapshotDto.getId()).isEqualTo(10L);
    assertThat(snapshotDto.getBuildDate()).isEqualTo(parseDate("2014-07-02").getTime());
    assertThat(snapshotDto.getComponentUuid()).isEqualTo("uuid_21");
    assertThat(snapshotDto.getLast()).isTrue();
    assertThat(snapshotDto.getVersion()).isEqualTo("1.0");
    assertThat(snapshotDto.getPeriodMode()).isEqualTo("mode1");
    assertThat(snapshotDto.getPeriodModeParameter()).isEqualTo("param1");
    assertThat(snapshotDto.getIncremental()).isTrue();
    assertThat(snapshotDto.getPeriodDate()).isEqualTo(parseDate("2014-06-01").getTime());
  }

}
