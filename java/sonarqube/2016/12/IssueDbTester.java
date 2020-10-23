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

package org.sonar.db.issue;

import java.util.Arrays;
import javax.annotation.Nullable;
import org.sonar.core.issue.DefaultIssueComment;
import org.sonar.core.issue.FieldDiffs;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.rule.RuleDto;

import static org.sonar.db.component.ComponentTesting.newFileDto;
import static org.sonar.db.issue.IssueTesting.newDto;
import static org.sonar.db.rule.RuleTesting.newRuleDto;

public class IssueDbTester {

  private final DbTester db;

  public IssueDbTester(DbTester db) {
    this.db = db;
  }

  public IssueDto insertIssue(IssueDto issueDto) {
    db.getDbClient().issueDao().insert(db.getSession(), issueDto);
    db.commit();
    return issueDto;
  }

  public IssueDto insertIssue() {
    RuleDto rule = db.rules().insertRule(newRuleDto());
    ComponentDto project = db.components().insertProject();
    ComponentDto file = db.components().insertComponent(newFileDto(project));
    return insertIssue(newDto(rule, file, project));
  }

  public IssueChangeDto insertChange(IssueChangeDto issueChangeDto) {
    db.getDbClient().issueChangeDao().insert(db.getSession(), issueChangeDto);
    db.commit();
    return issueChangeDto;
  }

  public IssueChangeDto insertComment(IssueDto issueDto, @Nullable String login, String text) {
    IssueChangeDto issueChangeDto = IssueChangeDto.of(DefaultIssueComment.create(issueDto.getKey(), login, text));
    return insertChange(issueChangeDto);
  }

  public void insertFieldDiffs(IssueDto issueDto, FieldDiffs... diffs) {
    Arrays.stream(diffs).forEach(diff -> db.getDbClient().issueChangeDao().insert(db.getSession(), IssueChangeDto.of(issueDto.getKey(), diff)));
    db.commit();
  }

}
