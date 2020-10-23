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
package org.sonar.batch.cpd;

import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.sonar.api.CoreProperties;
import org.sonar.api.batch.fs.FileSystem;
import org.sonar.api.batch.fs.internal.DefaultFileSystem;
import org.sonar.api.batch.fs.internal.DefaultInputFile;
import org.sonar.api.config.Settings;
import org.sonar.batch.cpd.index.SonarDuplicationsIndex;
import org.sonar.batch.index.BatchComponentCache;
import org.sonar.duplications.block.Block;

import java.io.File;
import java.io.IOException;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyZeroInteractions;

public class JavaCpdIndexerTest {
  private static final String JAVA = "java";

  @Mock
  private SonarDuplicationsIndex index;

  @Captor
  private ArgumentCaptor<List<Block>> blockCaptor;

  private Settings settings;
  private JavaCpdIndexer engine;
  private DefaultInputFile file;

  @Rule
  public TemporaryFolder temp = new TemporaryFolder();

  @Before
  public void setUp() throws IOException {
    MockitoAnnotations.initMocks(this);

    File baseDir = temp.newFolder();
    DefaultFileSystem fs = new DefaultFileSystem(baseDir);
    file = new DefaultInputFile("foo", "src/ManyStatements.java").setLanguage(JAVA);
    fs.add(file);
    BatchComponentCache batchComponentCache = new BatchComponentCache();
    batchComponentCache.add(org.sonar.api.resources.File.create("src/Foo.java").setEffectiveKey("foo:src/ManyStatements.java"), null).setInputComponent(file);
    File ioFile = file.file();
    FileUtils.copyURLToFile(this.getClass().getResource("ManyStatements.java"), ioFile);

    settings = new Settings();
    engine = new JavaCpdIndexer(fs, settings, index);
  }

  @Test
  public void languageSupported() {
    JavaCpdIndexer engine = new JavaCpdIndexer(mock(FileSystem.class), new Settings(), index);
    assertThat(engine.isLanguageSupported(JAVA)).isTrue();
    assertThat(engine.isLanguageSupported("php")).isFalse();
  }

  @Test
  public void testExclusions() {
    settings.setProperty(CoreProperties.CPD_EXCLUSIONS, "**");
    engine.index(JAVA);
    verifyZeroInteractions(index);
  }

  @Test
  public void testJavaIndexing() throws Exception {
    engine.index(JAVA);

    verify(index).insert(eq(file), blockCaptor.capture());
    List<Block> blockList = blockCaptor.getValue();

    assertThat(blockList).hasSize(26);
  }
}
