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
package org.sonar.server.computation.scm;

/**
 * Represents the Scm information for a specific file.
 */
public interface ScmInfo {

  /**
   * Get most recent ChangeSet of the file. Can never be null
   */
  Changeset getLatestChangeset();

  /**
   * Get ChangeSet of the file for given line
   *
   * @throws IllegalArgumentException if there is no Changeset for the specified line, ie. the line number in invalid (either less than 1 or > linecount of the file)
   */
  Changeset getChangesetForLine(int lineNumber);

  /**
   * Check if there's a ChangeSet for given line
   */
  boolean hasChangesetForLine(int lineNumber);

  /**
   * Return all ChangeSets, in order, for all lines of the file
   */
  Iterable<Changeset> getAllChangesets();

}
