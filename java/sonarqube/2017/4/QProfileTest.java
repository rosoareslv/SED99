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
package org.sonar.scanner.rule;

import org.junit.Test;
import static org.assertj.core.api.Assertions.*;

public class QProfileTest {
  @Test
  public void testEquals() {
    QProfile q1 = new QProfile();
    QProfile q2 = new QProfile();
    QProfile q3 = new QProfile();

    q1.setKey("k1");
    q1.setName("name1");

    q2.setKey("k1");
    q2.setName("name2");

    q3.setKey("k3");
    q3.setName("name3");

    assertThat(q1).isEqualTo(q2);
    assertThat(q1).isNotEqualTo(q3);
    assertThat(q2).isNotEqualTo(q3);
    assertThat(q1).isNotEqualTo(null);
    assertThat(q1).isNotEqualTo("str");

    assertThat(q1.hashCode()).isEqualTo(q2.hashCode());
    assertThat(q1.hashCode()).isNotEqualTo(q3.hashCode());
    assertThat(q2.hashCode()).isNotEqualTo(q3.hashCode());
  }
}
