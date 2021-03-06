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
package org.sonar.server.issue.index;

import com.google.common.base.Function;
import com.google.common.collect.Maps;
import java.util.Map;
import javax.annotation.Nonnull;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.utils.System2;
import org.sonar.db.DbTester;

import static org.assertj.core.api.Assertions.assertThat;


public class IssueResultSetIteratorTest {

  @Rule
  public DbTester dbTester = DbTester.create(System2.INSTANCE);

  private static Map<String, IssueDoc> issuesByKey(IssueResultSetIterator it) {
    return Maps.uniqueIndex(it, new Function<IssueDoc, String>() {
      @Override
      public String apply(@Nonnull IssueDoc issue) {
        return issue.key();
      }
    });
  }

  @Before
  public void setUp() {
    dbTester.truncateTables();
  }

  @Test
  public void iterator_over_one_issue() {
    dbTester.prepareDbUnit(getClass(), "one_issue.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 0L, null);
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(1);

    IssueDoc issue = issuesByKey.get("ABC");
    assertThat(issue.key()).isEqualTo("ABC");
    assertThat(issue.resolution()).isEqualTo("FIXED");
    assertThat(issue.status()).isEqualTo("RESOLVED");
    assertThat(issue.severity()).isEqualTo("BLOCKER");
    assertThat(issue.isManualSeverity()).isFalse();
    assertThat(issue.assignee()).isEqualTo("guy1");
    assertThat(issue.authorLogin()).isEqualTo("guy2");
    assertThat(issue.checksum()).isEqualTo("FFFFF");
    assertThat(issue.line()).isEqualTo(444);
    assertThat(issue.ruleKey()).isEqualTo(RuleKey.of("squid", "AvoidCycles"));
    assertThat(issue.componentUuid()).isEqualTo("FILE1");
    assertThat(issue.projectUuid()).isEqualTo("PROJECT1");
    assertThat(issue.moduleUuid()).isEqualTo("PROJECT1");
    assertThat(issue.modulePath()).isEqualTo(".PROJECT1.");
    assertThat(issue.directoryPath()).isEqualTo("src/main/java");
    assertThat(issue.filePath()).isEqualTo("src/main/java/Action.java");
    assertThat(issue.tags()).containsOnly("tag1", "tag2", "tag3");
    assertThat(issue.effort().toMinutes()).isGreaterThan(0L);
    assertThat(issue.gap()).isEqualTo(2d);
    assertThat(issue.attribute("JIRA")).isEqualTo("http://jira.com");
    assertThat(issue.type().getDbConstant()).isEqualTo(2);
  }

  @Test
  public void iterator_over_issues() {
    dbTester.prepareDbUnit(getClass(), "shared.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 0L, null);
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(4);

    IssueDoc issue = issuesByKey.get("ABC");
    assertThat(issue.key()).isEqualTo("ABC");
    assertThat(issue.assignee()).isEqualTo("guy1");
    assertThat(issue.componentUuid()).isEqualTo("FILE1");
    assertThat(issue.projectUuid()).isEqualTo("PROJECT1");
    assertThat(issue.moduleUuid()).isEqualTo("PROJECT1");
    assertThat(issue.modulePath()).isEqualTo(".PROJECT1.");
    assertThat(issue.directoryPath()).isEqualTo("src/main/java");
    assertThat(issue.filePath()).isEqualTo("src/main/java/Action.java");
    assertThat(issue.tags()).containsOnly("tag1", "tag2", "tag3");
    assertThat(issue.effort().toMinutes()).isGreaterThan(0L);
    assertThat(issue.type().getDbConstant()).isEqualTo(1);

    issue = issuesByKey.get("BCD");
    assertThat(issue.key()).isEqualTo("BCD");
    assertThat(issue.assignee()).isEqualTo("guy1");
    assertThat(issue.componentUuid()).isEqualTo("MODULE1");
    assertThat(issue.projectUuid()).isEqualTo("PROJECT1");
    assertThat(issue.moduleUuid()).isEqualTo("MODULE1");
    assertThat(issue.modulePath()).isEqualTo(".PROJECT1.MODULE1.");
    assertThat(issue.directoryPath()).isNull();
    assertThat(issue.filePath()).isNull();
    assertThat(issue.tags()).containsOnly("tag1", "tag2", "tag3");
    assertThat(issue.effort().toMinutes()).isGreaterThan(0L);
    assertThat(issue.type().getDbConstant()).isEqualTo(2);

    issue = issuesByKey.get("DEF");
    assertThat(issue.key()).isEqualTo("DEF");
    assertThat(issue.assignee()).isEqualTo("guy2");
    assertThat(issue.componentUuid()).isEqualTo("FILE1");
    assertThat(issue.projectUuid()).isEqualTo("PROJECT1");
    assertThat(issue.moduleUuid()).isEqualTo("PROJECT1");
    assertThat(issue.modulePath()).isEqualTo(".PROJECT1.");
    assertThat(issue.directoryPath()).isEqualTo("src/main/java");
    assertThat(issue.filePath()).isEqualTo("src/main/java/Action.java");
    assertThat(issue.tags()).isEmpty();
    assertThat(issue.effort().toMinutes()).isGreaterThan(0L);
    assertThat(issue.type().getDbConstant()).isEqualTo(1);

    issue = issuesByKey.get("EFG");
    assertThat(issue.key()).isEqualTo("EFG");
    assertThat(issue.assignee()).isEqualTo("guy1");
    assertThat(issue.componentUuid()).isEqualTo("DIR1");
    assertThat(issue.projectUuid()).isEqualTo("PROJECT1");
    assertThat(issue.moduleUuid()).isEqualTo("MODULE1");
    assertThat(issue.modulePath()).isEqualTo(".PROJECT1.MODULE1.");
    assertThat(issue.directoryPath()).isEqualTo("src/main/java");
    assertThat(issue.filePath()).isEqualTo("src/main/java");
    assertThat(issue.tags()).isEmpty();
    assertThat(issue.effort().toMinutes()).isGreaterThan(0L);
    assertThat(issue.type().getDbConstant()).isEqualTo(1);
  }

  @Test
  public void iterator_over_issue_from_project() {
    dbTester.prepareDbUnit(getClass(), "many_projects.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 0L, "THE_PROJECT_1");
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(2);
  }

  @Test
  public void iterator_over_issue_from_project_and_date() {
    dbTester.prepareDbUnit(getClass(), "many_projects.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 1_600_000_000_000L, "THE_PROJECT_1");
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(1);
  }

  @Test
  public void extract_directory_path() {
    dbTester.prepareDbUnit(getClass(), "extract_directory_path.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 0L, null);
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(4);

    // File in sub directoy
    assertThat(issuesByKey.get("ABC").directoryPath()).isEqualTo("src/main/java");

    // File in root directoy
    assertThat(issuesByKey.get("DEF").directoryPath()).isEqualTo("/");

    // Module
    assertThat(issuesByKey.get("EFG").filePath()).isNull();

    // Project
    assertThat(issuesByKey.get("FGH").filePath()).isNull();
  }

  @Test
  public void extract_file_path() {
    dbTester.prepareDbUnit(getClass(), "extract_file_path.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 0L, null);
    Map<String, IssueDoc> issuesByKey = issuesByKey(it);
    it.close();

    assertThat(issuesByKey).hasSize(4);

    // File in sub directoy
    assertThat(issuesByKey.get("ABC").filePath()).isEqualTo("src/main/java/Action.java");

    // File in root directoy
    assertThat(issuesByKey.get("DEF").filePath()).isEqualTo("pom.xml");

    // Module
    assertThat(issuesByKey.get("EFG").filePath()).isNull();

    // Project
    assertThat(issuesByKey.get("FGH").filePath()).isNull();
  }

  @Test
  public void select_after_date() {
    dbTester.prepareDbUnit(getClass(), "shared.xml");
    IssueResultSetIterator it = IssueResultSetIterator.create(dbTester.getDbClient(), dbTester.getSession(), 1_420_000_000_000L, null);

    assertThat(it.hasNext()).isTrue();
    IssueDoc issue = it.next();
    assertThat(issue.key()).isEqualTo("DEF");

    assertThat(it.hasNext()).isFalse();
    it.close();
  }
}
