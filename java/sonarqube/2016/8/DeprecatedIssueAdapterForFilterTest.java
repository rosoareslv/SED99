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
package org.sonar.scanner.issue;

import java.util.Date;
import org.junit.Test;
import org.sonar.api.issue.Issue;
import org.sonar.api.resources.Project;
import org.sonar.api.rule.RuleKey;
import org.sonar.scanner.issue.DeprecatedIssueAdapterForFilter;
import org.sonar.scanner.protocol.Constants.Severity;
import org.sonar.scanner.protocol.output.ScannerReport.TextRange;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.fail;

public class DeprecatedIssueAdapterForFilterTest {

  private static final String PROJECT_KEY = "foo";
  private static final Date ANALYSIS_DATE = new Date();
  private static final String COMPONENT_KEY = "foo:src/Foo.java";

  @Test
  public void improve_coverage() {
    DeprecatedIssueAdapterForFilter issue = new DeprecatedIssueAdapterForFilter(new Project(PROJECT_KEY).setAnalysisDate(ANALYSIS_DATE),
      org.sonar.scanner.protocol.output.ScannerReport.Issue.newBuilder()
        .setRuleRepository("repo")
        .setRuleKey("key")
        .setSeverity(Severity.BLOCKER)
        .setMsg("msg")
        .build(),
      COMPONENT_KEY);
    DeprecatedIssueAdapterForFilter issue2 = new DeprecatedIssueAdapterForFilter(new Project(PROJECT_KEY).setAnalysisDate(ANALYSIS_DATE),
      org.sonar.scanner.protocol.output.ScannerReport.Issue.newBuilder()
        .setRuleRepository("repo")
        .setRuleKey("key")
        .setSeverity(Severity.BLOCKER)
        .setMsg("msg")
        .setTextRange(TextRange.newBuilder().setStartLine(1))
        .setGap(2.0)
        .build(),
      COMPONENT_KEY);

    try {
      issue.key();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    assertThat(issue.componentKey()).isEqualTo(COMPONENT_KEY);
    assertThat(issue.ruleKey()).isEqualTo(RuleKey.of("repo", "key"));

    try {
      issue.language();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    assertThat(issue.severity()).isEqualTo("BLOCKER");
    assertThat(issue.message()).isEqualTo("msg");
    assertThat(issue.line()).isNull();
    assertThat(issue2.line()).isEqualTo(1);
    assertThat(issue.effortToFix()).isNull();
    assertThat(issue2.effortToFix()).isEqualTo(2.0);
    assertThat(issue.status()).isEqualTo(Issue.STATUS_OPEN);
    assertThat(issue.resolution()).isNull();

    try {
      issue.reporter();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    assertThat(issue.assignee()).isNull();
    assertThat(issue.creationDate()).isEqualTo(ANALYSIS_DATE);
    assertThat(issue.updateDate()).isNull();
    assertThat(issue.closeDate()).isNull();
    assertThat(issue.attribute(PROJECT_KEY)).isNull();

    try {
      issue.authorLogin();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    try {
      issue.comments();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    try {
      issue.isNew();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    try {
      issue.debt();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    assertThat(issue.projectKey()).isEqualTo(PROJECT_KEY);

    try {
      issue.projectUuid();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    try {
      issue.componentUuid();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }

    try {
      issue.tags();
      fail("Should be unsupported");
    } catch (Exception e) {
      assertThat(e).isExactlyInstanceOf(UnsupportedOperationException.class).hasMessage("Not available for issues filters");
    }
  }

}
