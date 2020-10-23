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
package org.sonar.scanner.issue.tracking;

import java.util.function.Function;
import javax.annotation.Nullable;
import org.sonar.api.batch.InstantiationStrategy;
import org.sonar.api.batch.ScannerSide;
import org.sonar.api.batch.bootstrap.ProjectDefinition;
import org.sonar.api.batch.fs.InputComponent;
import org.sonar.api.batch.fs.internal.DefaultInputComponent;
import org.sonar.api.utils.log.Logger;
import org.sonar.api.utils.log.Loggers;
import org.sonar.api.utils.log.Profiler;
import org.sonar.core.component.ComponentKeys;
import org.sonar.scanner.protocol.input.ScannerInput.ServerIssue;
import org.sonar.scanner.repository.ServerIssuesLoader;
import org.sonar.scanner.scan.ImmutableProjectReactor;
import org.sonar.scanner.scan.filesystem.InputComponentStore;
import org.sonar.scanner.storage.Storage;
import org.sonar.scanner.storage.Storages;

@InstantiationStrategy(InstantiationStrategy.PER_BATCH)
@ScannerSide
public class ServerIssueRepository {

  private static final Logger LOG = Loggers.get(ServerIssueRepository.class);
  private static final String LOG_MSG = "Load server issues";

  private final Storages caches;
  private Storage<ServerIssue> issuesCache;
  private final ServerIssuesLoader previousIssuesLoader;
  private final ImmutableProjectReactor reactor;
  private final InputComponentStore resourceCache;

  public ServerIssueRepository(Storages caches, ServerIssuesLoader previousIssuesLoader, ImmutableProjectReactor reactor, InputComponentStore resourceCache) {
    this.caches = caches;
    this.previousIssuesLoader = previousIssuesLoader;
    this.reactor = reactor;
    this.resourceCache = resourceCache;
  }

  public void load() {
    Profiler profiler = Profiler.create(LOG).startInfo(LOG_MSG);
    this.issuesCache = caches.createCache("previousIssues");
    caches.registerValueCoder(ServerIssue.class, new ServerIssueValueCoder());
    previousIssuesLoader.load(reactor.getRoot().getKeyWithBranch(), new SaveIssueConsumer());
    profiler.stopInfo();
  }

  public Iterable<ServerIssue> byComponent(InputComponent component) {
    return issuesCache.values(((DefaultInputComponent) component).batchId());
  }

  private class SaveIssueConsumer implements Function<ServerIssue, Void> {

    @Override
    public Void apply(@Nullable ServerIssue issue) {
      if (issue == null) {
        return null;
      }
      String moduleKeyWithBranch = issue.getModuleKey();
      ProjectDefinition projectDefinition = reactor.getProjectDefinition(moduleKeyWithBranch);
      if (projectDefinition != null) {
        String componentKeyWithoutBranch = ComponentKeys.createEffectiveKey(projectDefinition.getKey(), issue.hasPath() ? issue.getPath() : null);
        DefaultInputComponent r = (DefaultInputComponent) resourceCache.getByKey(componentKeyWithoutBranch);
        if (r != null) {
          issuesCache.put(r.batchId(), issue.getKey(), issue);
          return null;
        }
      }
      // Deleted resource
      issuesCache.put(0, issue.getKey(), issue);
      return null;
    }
  }

  public Iterable<ServerIssue> issuesOnMissingComponents() {
    return issuesCache.values(0);
  }
}
