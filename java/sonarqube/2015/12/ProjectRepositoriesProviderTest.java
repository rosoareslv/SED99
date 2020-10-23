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
package org.sonar.batch.repository;

import java.util.Date;

import org.sonar.batch.repository.FileData;
import com.google.common.collect.Table;
import com.google.common.collect.HashBasedTable;
import org.apache.commons.lang.mutable.MutableBoolean;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.sonar.api.batch.bootstrap.ProjectKey;
import org.sonar.batch.analysis.DefaultAnalysisMode;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

public class ProjectRepositoriesProviderTest {
  private ProjectRepositoriesProvider provider;
  private ProjectRepositories project;

  @Mock
  private ProjectRepositoriesLoader loader;
  @Mock
  private ProjectKey projectKey;
  @Mock
  private DefaultAnalysisMode mode;

  @Before
  public void setUp() {
    MockitoAnnotations.initMocks(this);

    Table<String, String, String> t1 = HashBasedTable.create();
    Table<String, String, FileData> t2 = HashBasedTable.create();

    project = new ProjectRepositories(t1, t2, new Date());
    provider = new ProjectRepositoriesProvider();

    when(projectKey.get()).thenReturn("key");
  }

  @Test
  public void testNonAssociated() {
    when(mode.isNotAssociated()).thenReturn(true);
    ProjectRepositories repo = provider.provide(loader, projectKey, mode);

    assertThat(repo.exists()).isEqualTo(false);
    verify(mode).isNotAssociated();
    verifyNoMoreInteractions(loader, projectKey, mode);
  }

  @Test
  public void singleton() {
    when(mode.isNotAssociated()).thenReturn(true);
    ProjectRepositories repo = provider.provide(loader, projectKey, mode);

    assertThat(repo.exists()).isEqualTo(false);
    verify(mode).isNotAssociated();
    verifyNoMoreInteractions(loader, projectKey, mode);

    repo = provider.provide(loader, projectKey, mode);
    verifyNoMoreInteractions(loader, projectKey, mode);
  }

  @Test
  public void testValidation() {
    when(mode.isNotAssociated()).thenReturn(false);
    when(mode.isIssues()).thenReturn(true);
    when(loader.load(eq("key"), eq(true), any(MutableBoolean.class))).thenReturn(project);

    provider.provide(loader, projectKey, mode);
  }

  @Test
  public void testAssociated() {
    when(mode.isNotAssociated()).thenReturn(false);
    when(mode.isIssues()).thenReturn(false);
    when(loader.load(eq("key"), eq(false), any(MutableBoolean.class))).thenReturn(project);

    ProjectRepositories repo = provider.provide(loader, projectKey, mode);

    assertThat(repo.exists()).isEqualTo(true);
    assertThat(repo.lastAnalysisDate()).isNotNull();

    verify(mode).isNotAssociated();
    verify(mode, times(2)).isIssues();
    verify(projectKey).get();
    verify(loader).load(eq("key"), eq(false), any(MutableBoolean.class));
    verifyNoMoreInteractions(loader, projectKey, mode);
  }
}
