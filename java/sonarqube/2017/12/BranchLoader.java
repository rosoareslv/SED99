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
package org.sonar.server.computation.task.projectanalysis.component;

import javax.annotation.Nullable;
import org.sonar.api.utils.MessageException;
import org.sonar.scanner.protocol.output.ScannerReport;
import org.sonar.server.computation.task.projectanalysis.analysis.MutableAnalysisMetadataHolder;

import static org.apache.commons.lang.StringUtils.trimToNull;

public class BranchLoader {
  private final MutableAnalysisMetadataHolder metadataHolder;
  private final BranchLoaderDelegate delegate;

  public BranchLoader(MutableAnalysisMetadataHolder metadataHolder) {
    this(metadataHolder, null);
  }

  public BranchLoader(MutableAnalysisMetadataHolder metadataHolder, @Nullable BranchLoaderDelegate delegate) {
    this.metadataHolder = metadataHolder;
    this.delegate = delegate;
  }

  public void load(ScannerReport.Metadata metadata) {
    String deprecatedBranch = trimToNull(metadata.getDeprecatedBranch());
    String branchName = trimToNull(metadata.getBranchName());

    if (deprecatedBranch != null && branchName != null) {
      throw MessageException.of("Properties sonar.branch and sonar.branch.name can't be set together");
    }

    if (delegate != null && deprecatedBranch == null) {
      delegate.load(metadata);
    } else {
      metadataHolder.setBranch(new DefaultBranchImpl(deprecatedBranch));
    }
  }
}
