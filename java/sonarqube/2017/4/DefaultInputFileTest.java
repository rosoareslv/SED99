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
package org.sonar.api.batch.fs.internal;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.stream.Collectors;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.sonar.api.batch.fs.InputFile;
import org.sonar.api.batch.fs.TextRange;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.mock;

public class DefaultInputFileTest {

  @Rule
  public TemporaryFolder temp = new TemporaryFolder();

  @Test
  public void test() throws Exception {
    Path baseDir = temp.newFolder().toPath();

    Metadata metadata = new Metadata(42, 42, "", new int[0], 0);
    DefaultIndexedFile indexedFile = new DefaultIndexedFile("ABCDE", baseDir, "src/Foo.php", InputFile.Type.TEST, 0).setLanguage("php");
    DefaultInputFile inputFile = new DefaultInputFile(indexedFile, (f) -> f.setMetadata(metadata))
      .setStatus(InputFile.Status.ADDED)
      .setCharset(StandardCharsets.ISO_8859_1);

    assertThat(inputFile.relativePath()).isEqualTo("src/Foo.php");
    assertThat(new File(inputFile.relativePath())).isRelative();
    assertThat(inputFile.absolutePath()).endsWith("Foo.php");
    assertThat(new File(inputFile.absolutePath())).isAbsolute();
    assertThat(inputFile.language()).isEqualTo("php");
    assertThat(inputFile.status()).isEqualTo(InputFile.Status.ADDED);
    assertThat(inputFile.type()).isEqualTo(InputFile.Type.TEST);
    assertThat(inputFile.lines()).isEqualTo(42);
    assertThat(inputFile.charset()).isEqualTo(StandardCharsets.ISO_8859_1);
  }

  @Test
  public void test_content() throws IOException {
    Path baseDir = temp.newFolder().toPath();
    Path testFile = baseDir.resolve("src").resolve("Foo.php");
    Files.createDirectories(testFile.getParent());
    Files.write(testFile, "test string".getBytes(StandardCharsets.UTF_8));
    Metadata metadata = new Metadata(42, 30, "", new int[0], 0);

    DefaultInputFile inputFile = new DefaultInputFile(new DefaultIndexedFile("ABCDE", baseDir, "src/Foo.php", InputFile.Type.TEST, 0)
      .setLanguage("php"), f -> f.setMetadata(metadata))
        .setStatus(InputFile.Status.ADDED)
        .setCharset(StandardCharsets.ISO_8859_1);

    assertThat(inputFile.contents()).isEqualTo("test string");
    try (InputStream inputStream = inputFile.inputStream()) {
      String result = new BufferedReader(new InputStreamReader(inputStream)).lines().collect(Collectors.joining());
      assertThat(result).isEqualTo("test string");
    }

  }

  @Test
  public void test_equals_and_hashcode() throws Exception {
    DefaultInputFile f1 = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), (f) -> mock(Metadata.class));
    DefaultInputFile f1a = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), (f) -> mock(Metadata.class));
    DefaultInputFile f2 = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Bar.php"), (f) -> mock(Metadata.class));

    assertThat(f1).isEqualTo(f1);
    assertThat(f1).isEqualTo(f1a);
    assertThat(f1).isNotEqualTo(f2);
    assertThat(f1.equals("foo")).isFalse();
    assertThat(f1.equals(null)).isFalse();

    assertThat(f1.hashCode()).isEqualTo(f1.hashCode());
    assertThat(f1.hashCode()).isEqualTo(f1a.hashCode());
  }

  @Test
  public void test_toString() throws Exception {
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), (f) -> mock(Metadata.class));
    assertThat(file.toString()).isEqualTo("[moduleKey=ABCDE, relative=src/Foo.php, basedir=module]");
  }

  @Test
  public void checkValidPointer() {
    Metadata metadata = new Metadata(2, 2, "", new int[] {0, 10}, 15);
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));
    assertThat(file.newPointer(1, 0).line()).isEqualTo(1);
    assertThat(file.newPointer(1, 0).lineOffset()).isEqualTo(0);
    // Don't fail
    file.newPointer(1, 9);
    file.newPointer(2, 0);
    file.newPointer(2, 5);

    try {
      file.newPointer(0, 1);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("0 is not a valid line for a file");
    }
    try {
      file.newPointer(3, 1);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("3 is not a valid line for pointer. File [moduleKey=ABCDE, relative=src/Foo.php, basedir=module] has 2 line(s)");
    }
    try {
      file.newPointer(1, -1);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("-1 is not a valid line offset for a file");
    }
    try {
      file.newPointer(1, 10);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("10 is not a valid line offset for pointer. File [moduleKey=ABCDE, relative=src/Foo.php, basedir=module] has 9 character(s) at line 1");
    }
  }

  @Test
  public void checkValidPointerUsingGlobalOffset() {
    Metadata metadata = new Metadata(2, 2, "", new int[] {0, 10}, 15);
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));
    assertThat(file.newPointer(0).line()).isEqualTo(1);
    assertThat(file.newPointer(0).lineOffset()).isEqualTo(0);

    assertThat(file.newPointer(9).line()).isEqualTo(1);
    assertThat(file.newPointer(9).lineOffset()).isEqualTo(9);

    assertThat(file.newPointer(10).line()).isEqualTo(2);
    assertThat(file.newPointer(10).lineOffset()).isEqualTo(0);

    assertThat(file.newPointer(15).line()).isEqualTo(2);
    assertThat(file.newPointer(15).lineOffset()).isEqualTo(5);

    try {
      file.newPointer(-1);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("-1 is not a valid offset for a file");
    }

    try {
      file.newPointer(16);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("16 is not a valid offset for file [moduleKey=ABCDE, relative=src/Foo.php, basedir=module]. Max offset is 15");
    }
  }

  @Test
  public void checkValidRange() {
    Metadata metadata = new FileMetadata().readMetadata(new StringReader("bla bla a\nabcde"));
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));

    assertThat(file.newRange(file.newPointer(1, 0), file.newPointer(2, 1)).start().line()).isEqualTo(1);
    // Don't fail
    file.newRange(file.newPointer(1, 0), file.newPointer(1, 1));
    file.newRange(file.newPointer(1, 0), file.newPointer(1, 9));
    file.newRange(file.newPointer(1, 0), file.newPointer(2, 0));
    assertThat(file.newRange(file.newPointer(1, 0), file.newPointer(2, 5))).isEqualTo(file.newRange(0, 15));

    try {
      file.newRange(file.newPointer(1, 0), file.newPointer(1, 0));
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("Start pointer [line=1, lineOffset=0] should be before end pointer [line=1, lineOffset=0]");
    }
    try {
      file.newRange(file.newPointer(1, 0), file.newPointer(1, 10));
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("10 is not a valid line offset for pointer. File [moduleKey=ABCDE, relative=src/Foo.php, basedir=module] has 9 character(s) at line 1");
    }
  }

  @Test
  public void selectLine() {
    Metadata metadata = new FileMetadata().readMetadata(new StringReader("bla bla a\nabcde\n\nabc"));
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));

    assertThat(file.selectLine(1).start().line()).isEqualTo(1);
    assertThat(file.selectLine(1).start().lineOffset()).isEqualTo(0);
    assertThat(file.selectLine(1).end().line()).isEqualTo(1);
    assertThat(file.selectLine(1).end().lineOffset()).isEqualTo(9);

    // Don't fail when selecting empty line
    assertThat(file.selectLine(3).start().line()).isEqualTo(3);
    assertThat(file.selectLine(3).start().lineOffset()).isEqualTo(0);
    assertThat(file.selectLine(3).end().line()).isEqualTo(3);
    assertThat(file.selectLine(3).end().lineOffset()).isEqualTo(0);

    try {
      file.selectLine(5);
      fail();
    } catch (Exception e) {
      assertThat(e).hasMessage("5 is not a valid line for pointer. File [moduleKey=ABCDE, relative=src/Foo.php, basedir=module] has 4 line(s)");
    }
  }

  @Test
  public void checkValidRangeUsingGlobalOffset() {
    Metadata metadata = new Metadata(2, 2, "", new int[] {0, 10}, 15);
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));
    TextRange newRange = file.newRange(10, 13);
    assertThat(newRange.start().line()).isEqualTo(2);
    assertThat(newRange.start().lineOffset()).isEqualTo(0);
    assertThat(newRange.end().line()).isEqualTo(2);
    assertThat(newRange.end().lineOffset()).isEqualTo(3);
  }

  @Test
  public void testRangeOverlap() {
    Metadata metadata = new Metadata(2, 2, "", new int[] {0, 10}, 15);
    DefaultInputFile file = new DefaultInputFile(new DefaultIndexedFile("ABCDE", Paths.get("module"), "src/Foo.php"), f -> f.setMetadata(metadata));
    // Don't fail
    assertThat(file.newRange(file.newPointer(1, 0), file.newPointer(1, 1)).overlap(file.newRange(file.newPointer(1, 0), file.newPointer(1, 1)))).isTrue();
    assertThat(file.newRange(file.newPointer(1, 0), file.newPointer(1, 1)).overlap(file.newRange(file.newPointer(1, 0), file.newPointer(1, 2)))).isTrue();
    assertThat(file.newRange(file.newPointer(1, 0), file.newPointer(1, 1)).overlap(file.newRange(file.newPointer(1, 1), file.newPointer(1, 2)))).isFalse();
    assertThat(file.newRange(file.newPointer(1, 2), file.newPointer(1, 3)).overlap(file.newRange(file.newPointer(1, 0), file.newPointer(1, 2)))).isFalse();
  }
}
