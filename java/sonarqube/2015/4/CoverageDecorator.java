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
package org.sonar.plugins.core.sensors;

import org.sonar.api.batch.DecoratorContext;
import org.sonar.api.batch.DependsUpon;
import org.sonar.api.measures.CoreMetrics;
import org.sonar.api.measures.MeasureUtils;
import org.sonar.api.measures.Metric;
import org.sonar.batch.sensor.coverage.CoverageConstants;

import java.util.Collection;

public final class CoverageDecorator extends AbstractCoverageDecorator {

  @DependsUpon
  public Collection<Metric> usedMetrics() {
    return CoverageConstants.COVERAGE_METRICS;
  }

  @Override
  protected Metric getGeneratedMetric() {
    return CoreMetrics.COVERAGE;
  }

  @Override
  protected Long countElements(DecoratorContext context) {
    long linesToCover = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.LINES_TO_COVER), 0L);
    long conditions = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.CONDITIONS_TO_COVER), 0L);

    return linesToCover + conditions;
  }

  @Override
  protected long countCoveredElements(DecoratorContext context) {
    long uncoveredLines = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.UNCOVERED_LINES), 0L);
    long lines = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.LINES_TO_COVER), 0L);
    long uncoveredConditions = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.UNCOVERED_CONDITIONS), 0L);
    long conditions = MeasureUtils.getValueAsLong(context.getMeasure(CoreMetrics.CONDITIONS_TO_COVER), 0L);

    return lines + conditions - uncoveredConditions - uncoveredLines;
  }

  @Override
  protected Metric getGeneratedMetricForNewCode() {
    return CoreMetrics.NEW_COVERAGE;
  }

  @Override
  protected Long countElementsForNewCode(DecoratorContext context, int periodIndex) {
    Long newLinesToCover = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_LINES_TO_COVER), periodIndex);
    if (newLinesToCover == null) {
      return null;
    }

    long newConditionsToCover = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_CONDITIONS_TO_COVER), periodIndex, 0L);

    return newLinesToCover + newConditionsToCover;
  }

  @Override
  protected long countCoveredElementsForNewCode(DecoratorContext context, int periodIndex) {
    long newLines = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_LINES_TO_COVER), periodIndex, 0L);
    long newUncoveredLines = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_UNCOVERED_LINES), periodIndex, 0L);
    long newUncoveredConditions = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_UNCOVERED_CONDITIONS), periodIndex, 0L);
    long newConditions = MeasureUtils.getVariationAsLong(context.getMeasure(CoreMetrics.NEW_CONDITIONS_TO_COVER), periodIndex, 0L);

    return newLines + newConditions - newUncoveredConditions - newUncoveredLines;
  }
}
