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
package org.sonar.api.scan.filesystem;

import com.google.common.base.Joiner;
import java.io.File;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import javax.annotation.CheckForNull;
import org.apache.commons.io.FilenameUtils;
import org.sonar.api.batch.ScannerSide;
import org.sonar.api.utils.PathUtils;

/**
 * @since 3.5
 */
@ScannerSide
public class PathResolver {

  public File relativeFile(File dir, String path) {
    return dir.toPath().resolve(path).normalize().toFile();
  }

  public List<File> relativeFiles(File dir, List<String> paths) {
    List<File> result = new ArrayList<>();
    for (String path : paths) {
      result.add(relativeFile(dir, path));
    }
    return result;
  }

  /**
   * @deprecated since 6.0 was used when component keys were relative to source dirs
   */
  @Deprecated
  @CheckForNull
  public RelativePath relativePath(Collection<File> dirs, File file) {
    List<String> stack = new ArrayList<>();
    File cursor = file;
    while (cursor != null) {
      File parentDir = parentDir(dirs, cursor);
      if (parentDir != null) {
        return new RelativePath(parentDir, Joiner.on("/").join(stack));
      }
      stack.add(0, cursor.getName());
      cursor = cursor.getParentFile();
    }
    return null;
  }

  /**
   * Similar to {@link Path#relativize(Path)} except that:
   *   <ul>
   *   <li>null is returned if file is not a child of dir
   *   <li>the resulting path is converted to use Unix separators
   *   </ul> 
   * @since 6.0
   */
  @CheckForNull
  public String relativePath(Path dir, Path file) {
    Path baseDir = dir.normalize();
    Path path = file.normalize();
    if (!path.startsWith(baseDir)) {
      return null;
    }
    try {
      Path relativized = baseDir.relativize(path);
      return FilenameUtils.separatorsToUnix(relativized.toString());
    } catch (IllegalArgumentException e) {
      return null;
    }
  }

  @CheckForNull
  public String relativePath(File dir, File file) {
    return relativePath(dir.toPath(), file.toPath());
  }

  @CheckForNull
  private static File parentDir(Collection<File> dirs, File cursor) {
    for (File dir : dirs) {
      if (PathUtils.canonicalPath(dir).equals(PathUtils.canonicalPath(cursor))) {
        return dir;
      }
    }
    return null;
  }

  /**
   * @deprecated since 6.0 was used when component keys were relative to source dirs
   */
  @Deprecated
  public static final class RelativePath {
    private File dir;
    private String path;

    public RelativePath(File dir, String path) {
      this.dir = dir;
      this.path = path;
    }

    public File dir() {
      return dir;
    }

    public String path() {
      return path;
    }
  }
}
