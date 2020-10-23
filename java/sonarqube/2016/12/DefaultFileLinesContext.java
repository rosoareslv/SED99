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
package org.sonar.scanner;

import com.google.common.base.MoreObjects;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Maps;
import java.util.Map;
import java.util.Map.Entry;
import org.sonar.api.batch.fs.InputFile;
import org.sonar.api.batch.measure.MetricFinder;
import org.sonar.api.batch.sensor.SensorContext;
import org.sonar.api.batch.sensor.measure.internal.DefaultMeasure;
import org.sonar.api.measures.CoreMetrics;
import org.sonar.api.measures.FileLinesContext;
import org.sonar.api.utils.KeyValueFormat;
import org.sonar.api.utils.KeyValueFormat.Converter;
import org.sonar.scanner.scan.measure.MeasureCache;

import static java.util.stream.Collectors.toMap;

public class DefaultFileLinesContext implements FileLinesContext {

  private final SensorContext context;
  private final InputFile inputFile;
  private final MetricFinder metricFinder;
  private final MeasureCache measureCache;

  /**
   * metric key -> line -> value
   */
  private final Map<String, Map<Integer, Object>> map = Maps.newHashMap();

  public DefaultFileLinesContext(SensorContext context, InputFile inputFile, MetricFinder metricFinder, MeasureCache measureCache) {
    this.context = context;
    this.inputFile = inputFile;
    this.metricFinder = metricFinder;
    this.measureCache = measureCache;
  }

  @Override
  public void setIntValue(String metricKey, int line, int value) {
    Preconditions.checkNotNull(metricKey);
    checkLineRange(line);

    setValue(metricKey, line, value);
  }

  private void checkLineRange(int line) {
    Preconditions.checkArgument(line > 0, "Line number should be positive for file %s.", inputFile);
    Preconditions.checkArgument(line <= inputFile.lines(), "Line %s is out of range for file %s. File has %s lines.", line, inputFile, inputFile.lines());
  }

  @Override
  public Integer getIntValue(String metricKey, int line) {
    Preconditions.checkNotNull(metricKey);
    checkLineRange(line);

    Map lines = map.get(metricKey);
    if (lines == null) {
      // not in memory, so load
      lines = loadData(metricKey, KeyValueFormat.newIntegerConverter());
      map.put(metricKey, lines);
    }
    return (Integer) lines.get(line);
  }

  @Override
  public void setStringValue(String metricKey, int line, String value) {
    Preconditions.checkNotNull(metricKey);
    checkLineRange(line);
    Preconditions.checkNotNull(value);

    setValue(metricKey, line, value);
  }

  @Override
  public String getStringValue(String metricKey, int line) {
    Preconditions.checkNotNull(metricKey);
    checkLineRange(line);

    Map lines = map.get(metricKey);
    if (lines == null) {
      // not in memory, so load
      lines = loadData(metricKey, KeyValueFormat.newStringConverter());
      map.put(metricKey, lines);
    }
    return (String) lines.get(line);
  }

  private Map<Integer, Object> getOrCreateLines(String metricKey) {
    Map<Integer, Object> lines = map.get(metricKey);
    if (lines == null) {
      lines = Maps.newHashMap();
      map.put(metricKey, lines);
    }
    return lines;
  }

  private void setValue(String metricKey, int line, Object value) {
    getOrCreateLines(metricKey).put(line, value);
  }

  @Override
  public void save() {
    for (Map.Entry<String, Map<Integer, Object>> entry : map.entrySet()) {
      String metricKey = entry.getKey();
      Map<Integer, Object> lines = entry.getValue();
      if (shouldSave(lines)) {
        String data = KeyValueFormat.format(optimizeStorage(metricKey, lines));
        context.newMeasure()
          .on(inputFile)
          .forMetric(metricFinder.findByKey(metricKey))
          .withValue(data)
          .save();
        entry.setValue(ImmutableMap.copyOf(lines));
      }
    }
  }

  private static Map optimizeStorage(String metricKey, Map<Integer, Object> lines) {
    // SONAR-7464 Don't store 0 because this is default value anyway
    if (CoreMetrics.NCLOC_DATA_KEY.equals(metricKey) || CoreMetrics.COMMENT_LINES_DATA_KEY.equals(metricKey) || CoreMetrics.EXECUTABLE_LINES_DATA_KEY.equals(metricKey)) {
      return lines.entrySet().stream()
        .filter(entry -> !entry.getValue().equals(0))
        .collect(toMap(Entry<Integer, Object>::getKey, Entry<Integer, Object>::getValue));
    }
    return lines;
  }

  private Map loadData(String metricKey, Converter converter) {
    DefaultMeasure<?> measure = measureCache.byMetric(inputFile.key(), metricKey);
    String data = measure != null ? (String) measure.value() : null;
    if (data != null) {
      return ImmutableMap.copyOf(KeyValueFormat.parse(data, KeyValueFormat.newIntegerConverter(), converter));
    }
    // no such measure
    return ImmutableMap.of();
  }

  /**
   * Checks that measure was not saved.
   *
   * @see #loadData(String, Converter)
   * @see #save()
   */
  private static boolean shouldSave(Map<Integer, Object> lines) {
    return !(lines instanceof ImmutableMap);
  }

  @Override
  public String toString() {
    return MoreObjects.toStringHelper(this)
      .add("map", map)
      .toString();
  }

}
