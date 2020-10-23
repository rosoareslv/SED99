/*
 * SonarQube, open source software quality management tool.
 * Copyright (C) 2008-2014 SonarSource
 * mailto:contact AT sonarsource DOT com
 *
 * SonarQube is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * SonarQube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.batch.issue.tracking;

import javax.annotation.CheckForNull;

import org.sonar.core.issue.tracking.Trackable;
import org.sonar.api.rule.RuleKey;

public class ServerIssueFromWs implements Trackable {

  private org.sonar.batch.protocol.input.BatchInput.ServerIssue dto;

  public ServerIssueFromWs(org.sonar.batch.protocol.input.BatchInput.ServerIssue dto) {
    this.dto = dto;
  }

  public org.sonar.batch.protocol.input.BatchInput.ServerIssue getDto() {
    return dto;
  }

  public String key() {
    return dto.getKey();
  }

  @Override
  public RuleKey getRuleKey() {
    return RuleKey.of(dto.getRuleRepository(), dto.getRuleKey());
  }

  @Override
  @CheckForNull
  public String getLineHash() {
    return dto.hasChecksum() ? dto.getChecksum() : null;
  }

  @Override
  @CheckForNull
  public Integer getLine() {
    return dto.hasLine() ? dto.getLine() : null;
  }

  @Override
  public String getMessage() {
    return dto.hasMsg() ? dto.getMsg() : "";
  }

}
