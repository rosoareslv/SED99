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
package org.sonar.db.component;

public final class ResourceIndexDto {
  private Long id;
  private String key;
  private int position;
  private int nameSize;
  private String componentUuid;
  private String rootComponentUuid;
  private String qualifier;

  public Long getId() {
    return id;
  }

  public ResourceIndexDto setId(Long id) {
    this.id = id;
    return this;
  }

  public String getKey() {
    return key;
  }

  public ResourceIndexDto setKey(String key) {
    this.key = key;
    return this;
  }

  public int getPosition() {
    return position;
  }

  public ResourceIndexDto setPosition(int i) {
    this.position = i;
    return this;
  }

  public String getComponentUuid() {
    return componentUuid;
  }

  public ResourceIndexDto setComponentUuid(String i) {
    this.componentUuid = i;
    return this;
  }

  public String getRootComponentUuid() {
    return rootComponentUuid;
  }

  public ResourceIndexDto setRootComponentUuid(String i) {
    this.rootComponentUuid = i;
    return this;
  }

  public int getNameSize() {
    return nameSize;
  }

  public ResourceIndexDto setNameSize(int i) {
    this.nameSize = i;
    return this;
  }

  public String getQualifier() {
    return qualifier;
  }

  public ResourceIndexDto setQualifier(String qualifier) {
    this.qualifier = qualifier;
    return this;
  }
}
