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
package org.sonar.server.computation.task.projectanalysis.issue;

import org.sonar.core.issue.DefaultIssue;
import org.sonar.server.computation.task.projectanalysis.component.Component;

public class RuleTypeCopier extends IssueVisitor {

  private final RuleRepository ruleRepository;

  public RuleTypeCopier(RuleRepository ruleRepository) {
    this.ruleRepository = ruleRepository;
  }

  @Override
  public void onIssue(Component component, DefaultIssue issue) {
    if (issue.type() == null) {
      Rule rule = ruleRepository.getByKey(issue.ruleKey());
      issue.setType(rule.getType());
    }
  }
}
