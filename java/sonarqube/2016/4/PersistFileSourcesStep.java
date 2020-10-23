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
package org.sonar.server.computation.step;

import com.google.common.base.Optional;
import com.google.common.collect.ImmutableMap;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.annotation.CheckForNull;
import javax.annotation.Nullable;
import org.apache.commons.codec.digest.DigestUtils;
import org.apache.commons.lang.ObjectUtils;
import org.apache.ibatis.session.ResultContext;
import org.apache.ibatis.session.ResultHandler;
import org.sonar.api.utils.System2;
import org.sonar.core.util.CloseableIterator;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.MyBatis;
import org.sonar.db.protobuf.DbFileSources;
import org.sonar.db.source.FileSourceDto;
import org.sonar.db.source.FileSourceDto.Type;
import org.sonar.scanner.protocol.output.ScannerReport;
import org.sonar.server.computation.batch.BatchReportReader;
import org.sonar.server.computation.component.Component;
import org.sonar.server.computation.component.CrawlerDepthLimit;
import org.sonar.server.computation.component.DepthTraversalTypeAwareCrawler;
import org.sonar.server.computation.component.TreeRootHolder;
import org.sonar.server.computation.component.TypeAwareVisitorAdapter;
import org.sonar.server.computation.duplication.DuplicationRepository;
import org.sonar.server.computation.scm.Changeset;
import org.sonar.server.computation.scm.ScmInfo;
import org.sonar.server.computation.scm.ScmInfoRepository;
import org.sonar.server.computation.source.ComputeFileSourceData;
import org.sonar.server.computation.source.CoverageLineReader;
import org.sonar.server.computation.source.DuplicationLineReader;
import org.sonar.server.computation.source.HighlightingLineReader;
import org.sonar.server.computation.source.LineReader;
import org.sonar.server.computation.source.RangeOffsetConverter;
import org.sonar.server.computation.source.ScmLineReader;
import org.sonar.server.computation.source.SourceLinesRepository;
import org.sonar.server.computation.source.SymbolsLineReader;

import static org.sonar.server.computation.component.ComponentVisitor.Order.PRE_ORDER;

public class PersistFileSourcesStep implements ComputationStep {

  private final DbClient dbClient;
  private final System2 system2;
  private final TreeRootHolder treeRootHolder;
  private final BatchReportReader reportReader;
  private final SourceLinesRepository sourceLinesRepository;
  private final ScmInfoRepository scmInfoRepository;
  private final DuplicationRepository duplicationRepository;

  public PersistFileSourcesStep(DbClient dbClient, System2 system2, TreeRootHolder treeRootHolder, BatchReportReader reportReader, SourceLinesRepository sourceLinesRepository,
    ScmInfoRepository scmInfoRepository, DuplicationRepository duplicationRepository) {
    this.dbClient = dbClient;
    this.system2 = system2;
    this.treeRootHolder = treeRootHolder;
    this.reportReader = reportReader;
    this.sourceLinesRepository = sourceLinesRepository;
    this.scmInfoRepository = scmInfoRepository;
    this.duplicationRepository = duplicationRepository;
  }

  @Override
  public void execute() {
    // Don't use batch insert for file_sources since keeping all data in memory can produce OOM for big files
    DbSession session = dbClient.openSession(false);
    try {
      new DepthTraversalTypeAwareCrawler(new FileSourceVisitor(session))
        .visit(treeRootHolder.getRoot());
    } finally {
      MyBatis.closeQuietly(session);
    }
  }

  private class FileSourceVisitor extends TypeAwareVisitorAdapter {

    private final DbSession session;

    private Map<String, FileSourceDto> previousFileSourcesByUuid = new HashMap<>();
    private String projectUuid;

    private FileSourceVisitor(DbSession session) {
      super(CrawlerDepthLimit.FILE, PRE_ORDER);
      this.session = session;
    }

    @Override
    public void visitProject(Component project) {
      this.projectUuid = project.getUuid();
      session.select("org.sonar.db.source.FileSourceMapper.selectHashesForProject", ImmutableMap.of("projectUuid", projectUuid, "dataType", Type.SOURCE),
        new ResultHandler() {
          @Override
          public void handleResult(ResultContext context) {
            FileSourceDto dto = (FileSourceDto) context.getResultObject();
            previousFileSourcesByUuid.put(dto.getFileUuid(), dto);
          }
        });
    }

    @Override
    public void visitFile(Component file) {
      int fileRef = file.getReportAttributes().getRef();
      ScannerReport.Component component = reportReader.readComponent(fileRef);
      CloseableIterator<String> linesIterator = sourceLinesRepository.readLines(file);
      LineReaders lineReaders = new LineReaders(reportReader, scmInfoRepository, duplicationRepository, file);
      try {
        ComputeFileSourceData computeFileSourceData = new ComputeFileSourceData(linesIterator, lineReaders.readers(), component.getLines());
        ComputeFileSourceData.Data fileSourceData = computeFileSourceData.compute();
        persistSource(fileSourceData, file.getUuid(), lineReaders.getLatestChange());
      } catch (Exception e) {
        throw new IllegalStateException(String.format("Cannot persist sources of %s", file.getKey()), e);
      } finally {
        linesIterator.close();
        lineReaders.close();
      }
    }

    private void persistSource(ComputeFileSourceData.Data fileSourceData, String componentUuid, @Nullable Changeset latestChange) {
      DbFileSources.Data fileData = fileSourceData.getFileSourceData();

      byte[] data = FileSourceDto.encodeSourceData(fileData);
      String dataHash = DigestUtils.md5Hex(data);
      String srcHash = fileSourceData.getSrcHash();
      String lineHashes = fileSourceData.getLineHashes();
      FileSourceDto previousDto = previousFileSourcesByUuid.get(componentUuid);

      if (previousDto == null) {
        FileSourceDto dto = new FileSourceDto()
          .setProjectUuid(projectUuid)
          .setFileUuid(componentUuid)
          .setDataType(Type.SOURCE)
          .setBinaryData(data)
          .setSrcHash(srcHash)
          .setDataHash(dataHash)
          .setLineHashes(lineHashes)
          .setCreatedAt(system2.now())
          .setUpdatedAt(system2.now())
          .setRevision(computeRevision(latestChange));
        dbClient.fileSourceDao().insert(session, dto);
        session.commit();
      } else {
        // Update only if data_hash has changed or if src_hash is missing or revision is missing (progressive migration)
        boolean binaryDataUpdated = !dataHash.equals(previousDto.getDataHash());
        boolean srcHashUpdated = !srcHash.equals(previousDto.getSrcHash());
        String revision = computeRevision(previousDto, latestChange);
        boolean revisionUpdated = !ObjectUtils.equals(revision, previousDto.getRevision());
        if (binaryDataUpdated || srcHashUpdated || revisionUpdated) {
          previousDto
            .setBinaryData(data)
            .setDataHash(dataHash)
            .setSrcHash(srcHash)
            .setLineHashes(lineHashes)
            .setRevision(revision)
            .setUpdatedAt(system2.now());
          dbClient.fileSourceDao().update(previousDto);
          session.commit();
        }
      }
    }

    @CheckForNull
    private String computeRevision(FileSourceDto previousDto, @Nullable Changeset latestChange) {
      if (latestChange == null) {
        return previousDto.getRevision();
      }
      return latestChange.getRevision();
    }

    @CheckForNull
    private String computeRevision(@Nullable Changeset latestChange) {
      if (latestChange == null) {
        return null;
      }
      return latestChange.getRevision();
    }
  }

  private static class LineReaders {
    private final List<LineReader> readers = new ArrayList<>();
    private final List<CloseableIterator<?>> closeables = new ArrayList<>();
    @CheckForNull
    private final ScmLineReader scmLineReader;

    LineReaders(BatchReportReader reportReader, ScmInfoRepository scmInfoRepository, DuplicationRepository duplicationRepository, Component component) {
      int componentRef = component.getReportAttributes().getRef();
      CloseableIterator<ScannerReport.LineCoverage> coverageIt = reportReader.readComponentCoverage(componentRef);
      closeables.add(coverageIt);
      readers.add(new CoverageLineReader(coverageIt));

      Optional<ScmInfo> scmInfoOptional = scmInfoRepository.getScmInfo(component);
      if (scmInfoOptional.isPresent()) {
        this.scmLineReader = new ScmLineReader(scmInfoOptional.get());
        readers.add(scmLineReader);
      } else {
        this.scmLineReader = null;
      }

      RangeOffsetConverter rangeOffsetConverter = new RangeOffsetConverter();
      CloseableIterator<ScannerReport.SyntaxHighlightingRule> highlightingIt = reportReader.readComponentSyntaxHighlighting(componentRef);
      closeables.add(highlightingIt);
      readers.add(new HighlightingLineReader(component, highlightingIt, rangeOffsetConverter));

      CloseableIterator<ScannerReport.Symbol> symbolsIt = reportReader.readComponentSymbols(componentRef);
      closeables.add(symbolsIt);
      readers.add(new SymbolsLineReader(component, symbolsIt, rangeOffsetConverter));

      readers.add(new DuplicationLineReader(duplicationRepository.getDuplications(component)));
    }

    List<LineReader> readers() {
      return readers;
    }

    void close() {
      for (CloseableIterator<?> reportIterator : closeables) {
        reportIterator.close();
      }
    }

    @CheckForNull
    public Changeset getLatestChange() {
      if (scmLineReader == null) {
        return null;
      }
      return scmLineReader.getLatestChange();
    }
  }

  @Override
  public String getDescription() {
    return "Persist sources";
  }
}
