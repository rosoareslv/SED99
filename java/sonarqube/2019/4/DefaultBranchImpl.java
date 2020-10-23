/*
 * SonarQube
 * Copyright (C) 2009-2019 SonarSource SA
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
package org.sonar.ce.task.projectanalysis.component;

import javax.annotation.Nullable;
import org.sonar.api.utils.MessageException;
import org.sonar.ce.task.projectanalysis.analysis.Branch;
import org.sonar.core.component.ComponentKeys;
import org.sonar.db.component.BranchDto;
import org.sonar.db.component.BranchType;

import static java.lang.String.format;
import static org.apache.commons.lang.StringUtils.isEmpty;
import static org.apache.commons.lang.StringUtils.trimToNull;

/**
 * The default (and legacy) implementation of {@link Branch}. It is used
 * when scanner is configured with parameter "sonar.branch" or when no branch is provided and the branch plugin is not installed.
 * A legacy branch is implemented as a fork of the project, so any branch is considered as "main".
 */
public class DefaultBranchImpl implements Branch {
  private final String branchName;
  private final boolean isLegacyBranch;

  public DefaultBranchImpl() {
    this(null);
  }

  public DefaultBranchImpl(@Nullable String name) {
    this.isLegacyBranch = (name != null);
    this.branchName = (name == null) ? BranchDto.DEFAULT_MAIN_BRANCH_NAME : name;
    if (!ComponentKeys.isValidLegacyBranch(branchName)) {
      throw MessageException.of(format("\"%s\" is not a valid branch name. "
        + "Allowed characters are alphanumeric, '-', '_', '.' and '/'.", branchName));
    }
  }

  @Override
  public BranchType getType() {
    return BranchType.LONG;
  }

  @Override
  public boolean isMain() {
    return true;
  }

  @Override
  public String getMergeBranchUuid() {
    throw new IllegalStateException("Not valid for the main branch");
  }

  @Override
  public boolean isLegacyFeature() {
    return isLegacyBranch;
  }

  @Override
  public String getName() {
    return branchName;
  }

  @Override
  public boolean supportsCrossProjectCpd() {
    // only on regular project, not on branches
    return !isLegacyBranch;
  }

  @Override
  public String getPullRequestKey() {
    throw new IllegalStateException("Only a branch of type PULL_REQUEST can have a pull request id.");
  }

  @Override
  public String getTargetBranchName() {
    throw new IllegalStateException("Only on a short lived branch or pull request");
  }

  @Override
  public String generateKey(String projectKey, @Nullable String fileOrDirPath) {
    if (isLegacyBranch) {
      projectKey = ComponentKeys.createKey(projectKey, branchName);
    }
    if (isEmpty(fileOrDirPath)) {
      return projectKey;
    }
    return ComponentKeys.createEffectiveKey(projectKey, trimToNull(fileOrDirPath));
  }
}
