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
package org.sonar.ce.db;

import org.junit.Test;
import org.sonar.db.DbSession;
import org.sonar.db.MyBatis;
import org.sonar.db.property.PropertyDto;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verifyNoMoreInteractions;

public class ReadOnlyPropertiesDaoTest {
  private MyBatis myBatis = mock(MyBatis.class);
  private DbSession dbSession = mock(DbSession.class);
  private PropertyDto propertyDto = mock(PropertyDto.class);
  private org.sonar.core.properties.PropertyDto oldPropertyDto = mock(org.sonar.core.properties.PropertyDto.class);
  private ReadOnlyPropertiesDao underTest = new ReadOnlyPropertiesDao(myBatis);

  @Test
  public void insertProperty() {
    underTest.insertProperty(dbSession, propertyDto);

    assertNoInteraction();
  }

  @Test
  public void insertProperty1() {
    underTest.insertProperty(propertyDto);

    assertNoInteraction();
  }

  @Test
  public void deleteProjectProperty() {
    underTest.deleteProjectProperty(null, null);

    assertNoInteraction();

  }

  @Test
  public void deleteProjectProperty1() {
    underTest.deleteProjectProperty(null, null, dbSession);

    assertNoInteraction();

  }

  @Test
  public void deleteProjectProperties() {
    underTest.deleteProjectProperties(null, null);

    assertNoInteraction();

  }

  @Test
  public void deleteProjectProperties1() {
    underTest.deleteProjectProperties(null, null, dbSession);

    assertNoInteraction();

  }

  @Test
  public void deleteGlobalProperties() {
    underTest.deleteGlobalProperties();

    assertNoInteraction();

  }

  @Test
  public void deleteGlobalProperty() {
    underTest.deleteGlobalProperty(null);

    assertNoInteraction();

  }

  @Test
  public void deleteGlobalProperty1() {
    underTest.deleteGlobalProperty(null, dbSession);

    assertNoInteraction();

  }

  @Test
  public void deleteAllProperties() {
    underTest.deleteAllProperties(null);

    assertNoInteraction();

  }

  @Test
  public void insertGlobalProperties() {
    underTest.insertGlobalProperties(null);

    assertNoInteraction();

  }

  @Test
  public void renamePropertyKey() {
    underTest.renamePropertyKey(null, null);

    assertNoInteraction();

  }

  @Test
  public void updateProperties() {
    underTest.updateProperties(null, null, null);

    assertNoInteraction();

  }

  @Test
  public void updateProperties1() {
    underTest.updateProperties(null, null, null, dbSession);

    assertNoInteraction();

  }

  @Test
  public void setProperty() {
    underTest.setProperty(oldPropertyDto);

    assertNoInteraction();

  }

  private void assertNoInteraction() {
    verifyNoMoreInteractions(myBatis, dbSession, propertyDto);
  }
}
