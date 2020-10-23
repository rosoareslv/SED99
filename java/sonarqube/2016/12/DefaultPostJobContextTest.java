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
package org.sonar.scanner.postjob;

import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.sonar.api.batch.AnalysisMode;
import org.sonar.api.batch.fs.InputFile;
import org.sonar.api.batch.postjob.issue.PostJobIssue;
import org.sonar.api.batch.rule.Severity;
import org.sonar.api.config.Settings;
import org.sonar.api.config.MapSettings;
import org.sonar.api.resources.File;
import org.sonar.scanner.index.BatchComponentCache;
import org.sonar.scanner.issue.IssueCache;
import org.sonar.scanner.issue.tracking.TrackedIssue;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class DefaultPostJobContextTest {

  private IssueCache issueCache;
  private BatchComponentCache resourceCache;
  private DefaultPostJobContext context;
  private Settings settings;
  private AnalysisMode analysisMode;

  @Before
  public void prepare() {
    issueCache = mock(IssueCache.class);
    resourceCache = new BatchComponentCache();
    settings = new MapSettings();
    analysisMode = mock(AnalysisMode.class);
    context = new DefaultPostJobContext(settings, issueCache, resourceCache, analysisMode);
  }

  @Test
  public void testIssues() {
    when(analysisMode.isIssues()).thenReturn(true);

    assertThat(context.settings()).isSameAs(settings);

    TrackedIssue defaultIssue = new TrackedIssue();
    defaultIssue.setComponentKey("foo:src/Foo.php");
    defaultIssue.setGap(2.0);
    defaultIssue.setNew(true);
    defaultIssue.setKey("xyz");
    defaultIssue.setStartLine(1);
    defaultIssue.setMessage("msg");
    defaultIssue.setSeverity("BLOCKER");
    when(issueCache.all()).thenReturn(Arrays.asList(defaultIssue));

    PostJobIssue issue = context.issues().iterator().next();
    assertThat(issue.componentKey()).isEqualTo("foo:src/Foo.php");
    assertThat(issue.isNew()).isTrue();
    assertThat(issue.key()).isEqualTo("xyz");
    assertThat(issue.line()).isEqualTo(1);
    assertThat(issue.message()).isEqualTo("msg");
    assertThat(issue.severity()).isEqualTo(Severity.BLOCKER);
    assertThat(issue.inputComponent()).isNull();

    InputFile inputPath = mock(InputFile.class);
    resourceCache.add(File.create("src/Foo.php").setEffectiveKey("foo:src/Foo.php"), null).setInputComponent(inputPath);
    assertThat(issue.inputComponent()).isEqualTo(inputPath);

  }
}
