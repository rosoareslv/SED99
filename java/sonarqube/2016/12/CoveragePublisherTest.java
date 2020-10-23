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
package org.sonar.scanner.report;

import java.io.File;
import java.util.Date;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.sonar.api.batch.fs.internal.DefaultInputFile;
import org.sonar.api.batch.fs.internal.DefaultInputModule;
import org.sonar.api.batch.sensor.measure.internal.DefaultMeasure;
import org.sonar.api.measures.CoreMetrics;
import org.sonar.api.resources.Project;
import org.sonar.core.util.CloseableIterator;
import org.sonar.scanner.index.BatchComponentCache;
import org.sonar.scanner.protocol.output.ScannerReport.LineCoverage;
import org.sonar.scanner.protocol.output.ScannerReportReader;
import org.sonar.scanner.protocol.output.ScannerReportWriter;
import org.sonar.scanner.scan.measure.MeasureCache;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class CoveragePublisherTest {

  @Rule
  public TemporaryFolder temp = new TemporaryFolder();

  private MeasureCache measureCache;
  private CoveragePublisher publisher;

  private org.sonar.api.resources.Resource sampleFile;

  @Before
  public void prepare() {
    Project p = new Project("foo").setAnalysisDate(new Date(1234567L));
    BatchComponentCache resourceCache = new BatchComponentCache();
    sampleFile = org.sonar.api.resources.File.create("src/Foo.php").setEffectiveKey("foo:src/Foo.php");
    resourceCache.add(p, null).setInputComponent(new DefaultInputModule("foo"));
    resourceCache.add(sampleFile, null).setInputComponent(new DefaultInputFile("foo", "src/Foo.php").setLines(5));
    measureCache = mock(MeasureCache.class);
    when(measureCache.byMetric(anyString(), anyString())).thenReturn(null);
    publisher = new CoveragePublisher(resourceCache, measureCache);
  }

  @Test
  public void publishCoverage() throws Exception {

    DefaultMeasure<String> utLineHits = new DefaultMeasure<String>().forMetric(CoreMetrics.COVERAGE_LINE_HITS_DATA).withValue("2=1;3=1;5=0;6=3");
    when(measureCache.byMetric("foo:src/Foo.php", CoreMetrics.COVERAGE_LINE_HITS_DATA_KEY)).thenReturn((DefaultMeasure) utLineHits);

    DefaultMeasure<String> conditionsByLine = new DefaultMeasure<String>().forMetric(CoreMetrics.CONDITIONS_BY_LINE).withValue("3=4");
    when(measureCache.byMetric("foo:src/Foo.php", CoreMetrics.CONDITIONS_BY_LINE_KEY)).thenReturn((DefaultMeasure) conditionsByLine);

    DefaultMeasure<String> coveredConditionsByUts = new DefaultMeasure<String>().forMetric(CoreMetrics.COVERED_CONDITIONS_BY_LINE).withValue("3=2");
    when(measureCache.byMetric("foo:src/Foo.php", CoreMetrics.COVERED_CONDITIONS_BY_LINE_KEY)).thenReturn((DefaultMeasure) coveredConditionsByUts);

    File outputDir = temp.newFolder();
    ScannerReportWriter writer = new ScannerReportWriter(outputDir);

    publisher.publish(writer);

    try (CloseableIterator<LineCoverage> it = new ScannerReportReader(outputDir).readComponentCoverage(2)) {
      assertThat(it.next()).isEqualTo(LineCoverage.newBuilder()
        .setLine(2)
        .setHits(true)
        .build());
      assertThat(it.next()).isEqualTo(LineCoverage.newBuilder()
        .setLine(3)
        .setHits(true)
        .setConditions(4)
        .setCoveredConditions(2)
        .build());
      assertThat(it.next()).isEqualTo(LineCoverage.newBuilder()
        .setLine(5)
        .setHits(false)
        .build());
    }

  }
}
