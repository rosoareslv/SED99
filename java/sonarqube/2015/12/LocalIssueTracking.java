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
package org.sonar.batch.issue.tracking;

import org.sonar.core.issue.tracking.Tracking;
import org.sonar.core.issue.tracking.Input;
import org.sonar.core.issue.tracking.Tracker;
import org.sonar.batch.issue.IssueTransformer;
import org.sonar.api.batch.fs.InputFile.Status;
import org.sonar.batch.analysis.DefaultAnalysisMode;
import com.google.common.annotations.VisibleForTesting;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;

import javax.annotation.CheckForNull;
import javax.annotation.Nullable;

import org.sonar.api.batch.BatchSide;
import org.sonar.api.batch.fs.internal.DefaultInputFile;
import org.sonar.api.batch.rule.ActiveRule;
import org.sonar.api.batch.rule.ActiveRules;
import org.sonar.api.issue.Issue;
import org.sonar.api.resources.ResourceUtils;
import org.sonar.batch.index.BatchComponent;
import org.sonar.batch.protocol.output.BatchReport;
import org.sonar.batch.repository.ProjectRepositories;

@BatchSide
public class LocalIssueTracking {
  private final Tracker<TrackedIssue, ServerIssueFromWs> tracker;
  private final ServerLineHashesLoader lastLineHashes;
  private final ActiveRules activeRules;
  private final ServerIssueRepository serverIssueRepository;
  private final DefaultAnalysisMode mode;

  private boolean hasServerAnalysis;

  public LocalIssueTracking(Tracker<TrackedIssue, ServerIssueFromWs> tracker, ServerLineHashesLoader lastLineHashes,
    ActiveRules activeRules, ServerIssueRepository serverIssueRepository, ProjectRepositories projectRepositories, DefaultAnalysisMode mode) {
    this.tracker = tracker;
    this.lastLineHashes = lastLineHashes;
    this.serverIssueRepository = serverIssueRepository;
    this.mode = mode;
    this.activeRules = activeRules;
    this.hasServerAnalysis = projectRepositories.lastAnalysisDate() != null;
  }

  public void init() {
    if (hasServerAnalysis) {
      serverIssueRepository.load();
    }
  }

  public List<TrackedIssue> trackIssues(BatchComponent component, Collection<BatchReport.Issue> reportIssues, Date analysisDate) {
    List<TrackedIssue> trackedIssues = new LinkedList<>();
    if (hasServerAnalysis) {
      // all the issues that are not closed in db before starting this module scan, including manual issues
      Collection<ServerIssueFromWs> serverIssues = loadServerIssues(component);

      if (shouldCopyServerIssues(component)) {
        // raw issues should be empty, we just need to deal with server issues (SONAR-6931)
        copyServerIssues(serverIssues, trackedIssues);
      } else {

        SourceHashHolder sourceHashHolder = loadSourceHashes(component);
        Collection<TrackedIssue> rIssues = IssueTransformer.toTrackedIssue(component, reportIssues, sourceHashHolder);

        Input<ServerIssueFromWs> baseIssues = createBaseInput(serverIssues, sourceHashHolder);
        Input<TrackedIssue> rawIssues = createRawInput(rIssues, sourceHashHolder);

        Tracking<TrackedIssue, ServerIssueFromWs> track = tracker.track(rawIssues, baseIssues);

        addUnmatchedFromServer(track.getUnmatchedBases(), sourceHashHolder, trackedIssues);
        addUnmatchedFromServer(track.getOpenManualIssuesByLine().values(), sourceHashHolder, trackedIssues);
        mergeMatched(track, trackedIssues, rIssues);
        addUnmatchedFromReport(track.getUnmatchedRaws(), trackedIssues, analysisDate);
      }
    }

    if (hasServerAnalysis && ResourceUtils.isRootProject(component.resource())) {
      // issues that relate to deleted components
      addIssuesOnDeletedComponents(trackedIssues);
    }

    return trackedIssues;
  }

  private static Input<ServerIssueFromWs> createBaseInput(Collection<ServerIssueFromWs> serverIssues, @Nullable SourceHashHolder sourceHashHolder) {
    List<String> refHashes;

    if (sourceHashHolder != null && sourceHashHolder.getHashedReference() != null) {
      refHashes = Arrays.asList(sourceHashHolder.getHashedReference().hashes());
    } else {
      refHashes = new ArrayList<>(0);
    }

    return new IssueTrackingInput<>(serverIssues, refHashes);
  }

  private static Input<TrackedIssue> createRawInput(Collection<TrackedIssue> rIssues, @Nullable SourceHashHolder sourceHashHolder) {
    List<String> baseHashes;
    if (sourceHashHolder != null && sourceHashHolder.getHashedSource() != null) {
      baseHashes = Arrays.asList(sourceHashHolder.getHashedSource().hashes());
    } else {
      baseHashes = new ArrayList<>(0);
    }

    return new IssueTrackingInput<>(rIssues, baseHashes);
  }

  private boolean shouldCopyServerIssues(BatchComponent component) {
    if (!mode.scanAllFiles() && component.isFile()) {
      DefaultInputFile inputFile = (DefaultInputFile) component.inputComponent();
      if (inputFile.status() == Status.SAME) {
        return true;
      }
    }
    return false;
  }

  private void copyServerIssues(Collection<ServerIssueFromWs> serverIssues, List<TrackedIssue> trackedIssues) {
    for (ServerIssueFromWs serverIssue : serverIssues) {
      org.sonar.batch.protocol.input.BatchInput.ServerIssue unmatchedPreviousIssue = serverIssue.getDto();
      TrackedIssue unmatched = IssueTransformer.toTrackedIssue(unmatchedPreviousIssue);

      ActiveRule activeRule = activeRules.find(unmatched.getRuleKey());
      unmatched.setNew(false);

      if (activeRule == null) {
        // rule removed
        IssueTransformer.resolveRemove(unmatched);
      }

      trackedIssues.add(unmatched);
    }
  }

  @CheckForNull
  private SourceHashHolder loadSourceHashes(BatchComponent component) {
    SourceHashHolder sourceHashHolder = null;
    if (component.isFile()) {
      DefaultInputFile file = (DefaultInputFile) component.inputComponent();
      if (file == null) {
        throw new IllegalStateException("Resource " + component.resource() + " was not found in InputPath cache");
      }
      sourceHashHolder = new SourceHashHolder(file, lastLineHashes);
    }
    return sourceHashHolder;
  }

  private Collection<ServerIssueFromWs> loadServerIssues(BatchComponent component) {
    Collection<ServerIssueFromWs> serverIssues = new ArrayList<>();
    for (org.sonar.batch.protocol.input.BatchInput.ServerIssue previousIssue : serverIssueRepository.byComponent(component)) {
      serverIssues.add(new ServerIssueFromWs(previousIssue));
    }
    return serverIssues;
  }

  @VisibleForTesting
  protected void mergeMatched(Tracking<TrackedIssue, ServerIssueFromWs> result, Collection<TrackedIssue> mergeTo, Collection<TrackedIssue> rawIssues) {
    for (Map.Entry<TrackedIssue, ServerIssueFromWs> e : result.getMatchedRaws().entrySet()) {
      org.sonar.batch.protocol.input.BatchInput.ServerIssue dto = e.getValue().getDto();
      TrackedIssue tracked = e.getKey();

      // invariant fields
      tracked.setKey(dto.getKey());

      // non-persisted fields
      tracked.setNew(false);

      // fields to update with old values
      tracked.setResolution(dto.hasResolution() ? dto.getResolution() : null);
      tracked.setStatus(dto.getStatus());
      tracked.setAssignee(dto.hasAssigneeLogin() ? dto.getAssigneeLogin() : null);
      tracked.setCreationDate(new Date(dto.getCreationDate()));

      if (dto.getManualSeverity()) {
        // Severity overriden by user
        tracked.setSeverity(dto.getSeverity().name());
      }
      mergeTo.add(tracked);
    }
  }

  private void addUnmatchedFromServer(Iterable<ServerIssueFromWs> unmatchedIssues, SourceHashHolder sourceHashHolder, Collection<TrackedIssue> mergeTo) {
    for (ServerIssueFromWs unmatchedIssue : unmatchedIssues) {
      org.sonar.batch.protocol.input.BatchInput.ServerIssue unmatchedPreviousIssue = unmatchedIssue.getDto();
      TrackedIssue unmatched = IssueTransformer.toTrackedIssue(unmatchedPreviousIssue);
      if (unmatchedIssue.getRuleKey().isManual() && !Issue.STATUS_CLOSED.equals(unmatchedPreviousIssue.getStatus())) {
        relocateManualIssue(unmatched, unmatchedIssue, sourceHashHolder);
      }
      updateUnmatchedIssue(unmatched, false /* manual issues can be kept open */);
      mergeTo.add(unmatched);
    }
  }

  private static void addUnmatchedFromReport(Iterable<TrackedIssue> rawIssues, Collection<TrackedIssue> trackedIssues, Date analysisDate) {
    for (TrackedIssue rawIssue : rawIssues) {
      rawIssue.setCreationDate(analysisDate);
      trackedIssues.add(rawIssue);
    }
  }

  private void addIssuesOnDeletedComponents(Collection<TrackedIssue> issues) {
    for (org.sonar.batch.protocol.input.BatchInput.ServerIssue previous : serverIssueRepository.issuesOnMissingComponents()) {
      TrackedIssue dead = IssueTransformer.toTrackedIssue(previous);
      updateUnmatchedIssue(dead, true);
      issues.add(dead);
    }
  }

  private void updateUnmatchedIssue(TrackedIssue issue, boolean forceEndOfLife) {
    ActiveRule activeRule = activeRules.find(issue.getRuleKey());
    issue.setNew(false);

    boolean manualIssue = issue.getRuleKey().isManual();
    boolean isRemovedRule = activeRule == null;

    if (isRemovedRule) {
      IssueTransformer.resolveRemove(issue);
    } else if (forceEndOfLife || !manualIssue) {
      IssueTransformer.close(issue);
    }
  }

  private static void relocateManualIssue(TrackedIssue newIssue, ServerIssueFromWs oldIssue, SourceHashHolder sourceHashHolder) {
    Integer previousLine = oldIssue.getLine();
    if (previousLine == null) {
      return;
    }

    Collection<Integer> newLinesWithSameHash = sourceHashHolder.getNewLinesMatching(previousLine);
    if (newLinesWithSameHash.isEmpty()) {
      if (previousLine > sourceHashHolder.getHashedSource().length()) {
        IssueTransformer.resolveRemove(newIssue);
      }
    } else if (newLinesWithSameHash.size() == 1) {
      Integer newLine = newLinesWithSameHash.iterator().next();
      newIssue.setStartLine(newLine);
      newIssue.setEndLine(newLine);
    }
  }
}
