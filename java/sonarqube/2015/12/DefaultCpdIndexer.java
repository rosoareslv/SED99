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

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Lists;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.sonar.api.CoreProperties;
import org.sonar.api.batch.CpdMapping;
import org.sonar.api.batch.fs.FilePredicates;
import org.sonar.api.batch.fs.FileSystem;
import org.sonar.api.batch.fs.InputFile;
import org.sonar.api.batch.fs.internal.DefaultInputFile;
import org.sonar.api.config.Settings;
import org.sonar.batch.cpd.index.SonarDuplicationsIndex;
import org.sonar.duplications.block.Block;
import org.sonar.duplications.internal.pmd.TokenizerBridge;

public class DefaultCpdIndexer extends CpdIndexer {

  private static final Logger LOG = LoggerFactory.getLogger(DefaultCpdIndexer.class);

  private final CpdMappings mappings;
  private final FileSystem fs;
  private final Settings settings;
  private final SonarDuplicationsIndex index;

  public DefaultCpdIndexer(CpdMappings mappings, FileSystem fs, Settings settings, SonarDuplicationsIndex index) {
    this.mappings = mappings;
    this.fs = fs;
    this.settings = settings;
    this.index = index;
  }

  @Override
  public boolean isLanguageSupported(String language) {
    return true;
  }

  @Override
  public void index(String languageKey) {
    CpdMapping mapping = mappings.getMapping(languageKey);
    if (mapping == null) {
      LOG.debug("No CpdMapping for language " + languageKey);
      return;
    }

    String[] cpdExclusions = settings.getStringArray(CoreProperties.CPD_EXCLUSIONS);
    logExclusions(cpdExclusions, LOG);
    FilePredicates p = fs.predicates();
    List<InputFile> sourceFiles = Lists.newArrayList(fs.inputFiles(p.and(
      p.hasType(InputFile.Type.MAIN),
      p.hasLanguage(languageKey),
      p.doesNotMatchPathPatterns(cpdExclusions))));
    if (sourceFiles.isEmpty()) {
      return;
    }

    // Create index
    populateIndex(languageKey, sourceFiles, mapping, index);
  }

  private void populateIndex(String languageKey, List<InputFile> sourceFiles, CpdMapping mapping, SonarDuplicationsIndex index) {
    TokenizerBridge bridge = new TokenizerBridge(mapping.getTokenizer(), fs.encoding().name(), getBlockSize(languageKey));
    for (InputFile inputFile : sourceFiles) {
      LOG.debug("Populating index from {}", inputFile);
      String resourceEffectiveKey = ((DefaultInputFile) inputFile).key();
      List<Block> blocks = bridge.chunk(resourceEffectiveKey, inputFile.file());
      index.insert(inputFile, blocks);
    }
  }

  @VisibleForTesting
  int getBlockSize(String languageKey) {
    int blockSize = settings.getInt("sonar.cpd." + languageKey + ".minimumLines");
    if (blockSize == 0) {
      blockSize = getDefaultBlockSize(languageKey);
    }
    return blockSize;
  }

  @VisibleForTesting
  static int getDefaultBlockSize(String languageKey) {
    if ("cobol".equals(languageKey)) {
      return 30;
    } else if ("abap".equals(languageKey)) {
      return 20;
    } else {
      return 10;
    }
  }

}
