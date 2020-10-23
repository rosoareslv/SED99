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
package org.sonar.api.scan.issue.filter;

import org.sonar.api.ExtensionPoint;
import org.sonar.api.batch.BatchSide;

@BatchSide
@ExtensionPoint
/**
 * @since 5.3
 */
public interface IssueFilter {

  /**
   * The <code>accept</code> method is called for each {@link FilterableIssue} created during analysis, to check if it has to be persisted. Examples of use cases are:
   * <ul>
   *  <li>Ignoring or enforcing rules on specific resources</li>
   *  <li>Switching-off an issue based on its context (<code>//NOSONAR</code> comments, semantic annotations)</li>
   * </ul>
   * The <code>chain</code> parameter allows for fine control of the filtering logic: it is each filter's duty to either pass the issue to the next filter, by calling
   * the {@link IssueFilterChain#accept} method, or return directly if the issue has to be accepted or not
   * @param issue the issue being filtered
   * @param chain the rest of the filters
   * @return <code>true</code> to accept the issue, <code>false</code> to reject it, {@link IssueFilterChain#accept} to let the other filters decide.
   */
  boolean accept(FilterableIssue issue, IssueFilterChain chain);
}
