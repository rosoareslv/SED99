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
package org.sonar.ce.log;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.core.FileAppender;
import java.io.File;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import static org.assertj.core.api.Assertions.assertThat;

public class CeFileAppenderFactoryTest {

  @Rule
  public TemporaryFolder temp = new TemporaryFolder();

  @Test
  public void buildAppender() throws Exception {
    File logsDir = temp.newFolder();
    CeFileAppenderFactory factory = new CeFileAppenderFactory(logsDir);

    FileAppender underTest = factory.buildAppender(new LoggerContext(), "uuid_1.log");

    assertThat(new File(underTest.getFile())).isEqualTo(new File(logsDir, "uuid_1.log"));

  }
}
