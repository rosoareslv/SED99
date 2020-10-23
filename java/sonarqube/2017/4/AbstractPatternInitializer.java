/*
 * SonarQube
 * Copyright (C) 2009-2017 SonarSource SA
 * mailto:info AT sonarsource DOT com
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
package org.sonar.scanner.issue.ignore.pattern;

import com.google.common.annotations.VisibleForTesting;
import org.apache.commons.lang.StringUtils;
import org.sonar.api.batch.ScannerSide;
import org.sonar.api.config.Settings;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import static com.google.common.base.MoreObjects.firstNonNull;

@ScannerSide
public abstract class AbstractPatternInitializer {
  private Settings settings;
  private List<IssuePattern> multicriteriaPatterns;

  protected AbstractPatternInitializer(Settings settings) {
    this.settings = settings;
    initPatterns();
  }

  protected Settings getSettings() {
    return settings;
  }

  public List<IssuePattern> getMulticriteriaPatterns() {
    return multicriteriaPatterns;
  }

  public boolean hasConfiguredPatterns() {
    return hasMulticriteriaPatterns();
  }

  public boolean hasMulticriteriaPatterns() {
    return !multicriteriaPatterns.isEmpty();
  }

  @VisibleForTesting
  protected final void initPatterns() {
    // Patterns Multicriteria
    multicriteriaPatterns = new ArrayList<>();
    String patternConf = StringUtils.defaultIfBlank(settings.getString(getMulticriteriaConfigurationKey()), "");
    for (String id : StringUtils.split(patternConf, ',')) {
      String propPrefix = getMulticriteriaConfigurationKey() + "." + id + ".";
      String resourceKeyPattern = settings.getString(propPrefix + "resourceKey");
      String ruleKeyPattern = settings.getString(propPrefix + "ruleKey");
      String lineRange = "*";
      String[] fields = new String[] {resourceKeyPattern, ruleKeyPattern, lineRange};
      PatternDecoder.checkRegularLineConstraints(StringUtils.join(fields, ","), fields);
      Set<LineRange> rangeOfLines = PatternDecoder.decodeRangeOfLines(firstNonNull(lineRange, "*"));
      IssuePattern pattern = new IssuePattern(firstNonNull(resourceKeyPattern, "*"), firstNonNull(ruleKeyPattern, "*"), rangeOfLines);

      multicriteriaPatterns.add(pattern);
    }
  }

  protected abstract String getMulticriteriaConfigurationKey();
}
