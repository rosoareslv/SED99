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
package org.sonar.server.computation.task.projectanalysis.measure;

import static java.util.Objects.requireNonNull;
import static org.sonar.server.computation.task.projectanalysis.component.ComponentFunctions.toReportRef;

import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

import org.sonar.core.util.CloseableIterator;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.measure.MeasureDto;
import org.sonar.db.measure.MeasureQuery;
import org.sonar.scanner.protocol.output.ScannerReport;
import org.sonar.server.computation.task.projectanalysis.batch.BatchReportReader;
import org.sonar.server.computation.task.projectanalysis.component.Component;
import org.sonar.server.computation.task.projectanalysis.measure.MapBasedRawMeasureRepository.OverridePolicy;
import org.sonar.server.computation.task.projectanalysis.metric.Metric;
import org.sonar.server.computation.task.projectanalysis.metric.MetricRepository;
import org.sonar.server.computation.task.projectanalysis.metric.ReportMetricValidator;

import com.google.common.base.Optional;
import com.google.common.collect.SetMultimap;

public class MeasureRepositoryImpl implements MeasureRepository {
  private final MapBasedRawMeasureRepository<Integer> delegate = new MapBasedRawMeasureRepository<>(toReportRef());
  private final DbClient dbClient;
  private final BatchReportReader reportReader;
  private final BatchMeasureToMeasure batchMeasureToMeasure;
  private final MetricRepository metricRepository;
  private final ReportMetricValidator reportMetricValidator;

  private MeasureDtoToMeasure measureTransformer = new MeasureDtoToMeasure();
  private final Set<Integer> loadedComponents = new HashSet<>();

  public MeasureRepositoryImpl(DbClient dbClient, BatchReportReader reportReader, MetricRepository metricRepository,
    ReportMetricValidator reportMetricValidator) {
    this.dbClient = dbClient;
    this.reportReader = reportReader;
    this.reportMetricValidator = reportMetricValidator;
    this.batchMeasureToMeasure = new BatchMeasureToMeasure();
    this.metricRepository = metricRepository;
  }

  @Override
  public Optional<Measure> getBaseMeasure(Component component, Metric metric) {
    // fail fast
    requireNonNull(component);
    requireNonNull(metric);

    try (DbSession dbSession = dbClient.openSession(false)) {
      MeasureQuery query = MeasureQuery.builder().setComponentUuid(component.getUuid()).setMetricKey(metric.getKey()).build();
      java.util.Optional<MeasureDto> measureDto = dbClient.measureDao().selectSingle(dbSession, query);
      if (measureDto.isPresent()) {
        return measureTransformer.toMeasure(measureDto.get(), metric);
      }
      return Optional.absent();
    }
  }

  @Override
  public int loadAsRawMeasures(Collection<Component> components, Collection<Metric> metrics) {
    requireNonNull(components);
    requireNonNull(metrics);

    Map<String, Component> componentsByUuid = components.stream()
      .collect(Collectors.toMap(Component::getUuid, c -> c));
    Map<Integer, Metric> metricsById = metrics.stream()
      .collect(Collectors.toMap(Metric::getId, m -> m));

    List<MeasureDto> measuresDto;
    try (DbSession dbSession = dbClient.openSession(false)) {
      measuresDto = dbClient.measureDao().selectByComponentsAndMetrics(dbSession, componentsByUuid.keySet(), metricsById.keySet());
    }
    
    for (MeasureDto dto : measuresDto) {

      Metric metric = metricsById.get(dto.getMetricId());
      Component component = componentsByUuid.get(dto.getComponentUuid());
      Measure measure = measureTransformer.toMeasure(dto, metric).get();

      delegate.add(component, metric, measure);
    }
    return measuresDto.size();
  }

  @Override
  public Optional<Measure> getRawMeasure(Component component, Metric metric) {
    Optional<Measure> local = delegate.getRawMeasure(component, metric);
    if (local.isPresent()) {
      return local;
    }

    // look up in batch after loading (if not yet loaded) measures from batch
    loadBatchMeasuresForComponent(component);
    return delegate.getRawMeasure(component, metric);
  }

  @Override
  public void add(Component component, Metric metric, Measure measure) {
    delegate.add(component, metric, measure);
  }

  @Override
  public void update(Component component, Metric metric, Measure measure) {
    delegate.update(component, metric, measure);
  }

  @Override
  public Set<Measure> getRawMeasures(Component component, Metric metric) {
    loadBatchMeasuresForComponent(component);
    return delegate.getRawMeasures(component, metric);
  }

  @Override
  public SetMultimap<String, Measure> getRawMeasures(Component component) {
    loadBatchMeasuresForComponent(component);
    return delegate.getRawMeasures(component);
  }

  private void loadBatchMeasuresForComponent(Component component) {
    if (loadedComponents.contains(component.getReportAttributes().getRef())) {
      return;
    }

    try (CloseableIterator<ScannerReport.Measure> readIt = reportReader.readComponentMeasures(component.getReportAttributes().getRef())) {
      while (readIt.hasNext()) {
        ScannerReport.Measure batchMeasure = readIt.next();
        String metricKey = batchMeasure.getMetricKey();
        if (reportMetricValidator.validate(metricKey)) {
          Metric metric = metricRepository.getByKey(metricKey);
          delegate.add(component, metric, batchMeasureToMeasure.toMeasure(batchMeasure, metric).get(), OverridePolicy.DO_NOT_OVERRIDE);
        }
      }
    }
    loadedComponents.add(component.getReportAttributes().getRef());
  }

}
