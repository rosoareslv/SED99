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
package org.sonar.api.measures;

import java.util.List;

/**
 * @since 2.0
 * @deprecated since 5.2. Aggregation of measures is provided by {@link org.sonar.api.ce.measure.MeasureComputer}. {@link org.sonar.api.batch.Decorator}
 * and {@link Formula} are no more supported.
 */
@Deprecated
public class WeightedMeanAggregationFormula implements Formula {

  public WeightedMeanAggregationFormula(Metric weightingMetric, boolean zeroIfNoValues) {
  }

  @Override
  public List<Metric> dependsUponMetrics() {
    throw fail();
  }

  @Override
  public Measure calculate(FormulaData data, FormulaContext context) {
    throw fail();
  }

  private static RuntimeException fail() {
    throw new UnsupportedOperationException(
      "Unsupported since version 5.2. Decorators and formulas are not used anymore for aggregation measures. Please use org.sonar.api.ce.measure.MeasureComputer.");
  }
}
