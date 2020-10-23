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
package org.sonar.server.computation.task.projectanalysis.scm;

import com.google.common.base.Predicate;
import java.util.Arrays;
import javax.annotation.CheckForNull;
import javax.annotation.Nullable;
import javax.annotation.concurrent.Immutable;

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkState;
import static com.google.common.collect.FluentIterable.from;
import static com.google.common.collect.Iterables.isEmpty;
import static java.util.Arrays.asList;

@Immutable
public class ScmInfoImpl implements ScmInfo {

  @CheckForNull
  private final Changeset latestChangeset;
  private final Changeset[] lineChangesets;

  public ScmInfoImpl(Iterable<Changeset> lineChangesets) {
    checkState(!isEmpty(lineChangesets), "A ScmInfo must have at least one Changeset and does not support any null one");
    this.lineChangesets = from(lineChangesets)
      .filter(CheckNotNull.INSTANCE)
      .toArray(Changeset.class);
    this.latestChangeset = computeLatestChangeset(lineChangesets);
  }

  private static Changeset computeLatestChangeset(Iterable<Changeset> lineChangesets) {
    Changeset latestChangeset = null;
    for (Changeset lineChangeset : lineChangesets) {
      if (latestChangeset == null || lineChangeset.getDate() > latestChangeset.getDate()) {
        latestChangeset = lineChangeset;
      }
    }
    return latestChangeset;
  }

  @Override
  public Changeset getLatestChangeset() {
    return latestChangeset;
  }

  @Override
  public Changeset getChangesetForLine(int lineNumber) {
    checkArgument(lineNumber > 0 && lineNumber <= lineChangesets.length, "There's no changeset on line %s", lineNumber);
    return lineChangesets[lineNumber - 1];
  }

  @Override
  public boolean hasChangesetForLine(int lineNumber) {
    return lineNumber <= lineChangesets.length;
  }

  @Override
  public Iterable<Changeset> getAllChangesets() {
    return asList(lineChangesets);
  }

  @Override
  public String toString() {
    return "ScmInfoImpl{" +
      "latestChangeset=" + latestChangeset +
      ", lineChangesets=" + Arrays.toString(lineChangesets) +
      '}';
  }

  private enum CheckNotNull implements Predicate<Changeset> {
    INSTANCE;

    @Override
    public boolean apply(@Nullable Changeset input) {
      checkState(input != null, "Null changeset are not allowed");
      return true;
    }
  }
}
