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
package org.sonar.application.command;

import ch.qos.logback.classic.spi.ILoggingEvent;
import java.io.File;
import java.io.IOException;
import java.util.Properties;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.rules.TemporaryFolder;
import org.mockito.Mockito;
import org.sonar.application.es.EsInstallation;
import org.sonar.process.ProcessId;
import org.sonar.process.ProcessProperties;
import org.sonar.process.Props;
import org.sonar.process.System2;
import org.sonar.application.logging.ListAppender;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.entry;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.when;

public class CommandFactoryImplTest {

  @Rule
  public ExpectedException expectedException = ExpectedException.none();
  @Rule
  public TemporaryFolder temp = new TemporaryFolder();

  private File homeDir;
  private File tempDir;
  private File logsDir;
  private ListAppender listAppender;

  @Before
  public void setUp() throws Exception {
    homeDir = temp.newFolder();
    tempDir = temp.newFolder();
    logsDir = temp.newFolder();
  }

  @After
  public void tearDown() throws Exception {
    if (listAppender != null) {
      ListAppender.detachMemoryAppenderToLoggerOf(CommandFactoryImpl.class, listAppender);
    }
  }

  @Test
  public void constructor_logs_no_warning_if_env_variable_JAVA_TOOL_OPTIONS_is_not_set() {
    System2 system2 = Mockito.mock(System2.class);
    when(system2.getenv(anyString())).thenReturn(null);
    attachMemoryAppenderToLoggerOf(CommandFactoryImpl.class);

    new CommandFactoryImpl(new Props(new Properties()), tempDir, system2);

    assertThat(listAppender.getLogs()).isEmpty();
  }

  @Test
  public void constructor_logs_warning_if_env_variable_JAVA_TOOL_OPTIONS_is_set() {
    System2 system2 = Mockito.mock(System2.class);
    when(system2.getenv("JAVA_TOOL_OPTIONS")).thenReturn("sds");
    attachMemoryAppenderToLoggerOf(CommandFactoryImpl.class);

    new CommandFactoryImpl(new Props(new Properties()), tempDir, system2);

    assertThat(listAppender.getLogs())
      .extracting(ILoggingEvent::getMessage)
      .containsOnly(
        "JAVA_TOOL_OPTIONS is defined but will be ignored. " +
          "Use properties sonar.*.javaOpts and/or sonar.*.javaAdditionalOpts in sonar.properties to change SQ JVM processes options");
  }

  @Test
  public void createEsCommand_throws_ISE_if_es_binary_is_not_found() throws Exception {
    expectedException.expect(IllegalStateException.class);
    expectedException.expectMessage("Cannot find elasticsearch binary");

    newFactory(new Properties()).createEsCommand();
  }

  @Test
  public void createEsCommand_for_unix_returns_command_for_default_settings() throws Exception {
    System2 system2 = Mockito.mock(System2.class);
    when(system2.isOsWindows()).thenReturn(false);
    prepareEsFileSystem();

    Properties props = new Properties();
    props.setProperty("sonar.search.host", "localhost");

    AbstractCommand esCommand = newFactory(props, system2).createEsCommand();
    EsInstallation esConfig = esCommand.getEsInstallation();

    assertThat(esCommand).isInstanceOf(EsScriptCommand.class);
    assertThat(esConfig.getClusterName()).isEqualTo("sonarqube");
    assertThat(esConfig.getHost()).isNotEmpty();
    assertThat(esConfig.getPort()).isEqualTo(9001);
    assertThat(esConfig.getEsJvmOptions().getAll())
      // enforced values
      .contains("-XX:+UseConcMarkSweepGC", "-server", "-Dfile.encoding=UTF-8")
      // default settings
      .contains("-Xms512m", "-Xmx512m", "-XX:+HeapDumpOnOutOfMemoryError");
    File esConfDir = new File(tempDir, "conf/es");
    assertThat(esCommand.getEnvVariables())
      .contains(entry("ES_JVM_OPTIONS", new File(esConfDir, "jvm.options").getAbsolutePath()))
      .containsKey("JAVA_HOME");
    assertThat(esConfig.getEsYmlSettings()).isNotNull();

    assertThat(esConfig.getLog4j2Properties())
      .contains(entry("appender.file_es.fileName", new File(logsDir, "es.log").getAbsolutePath()));

    assertThat(esCommand.getSuppressedEnvVariables()).containsOnly("JAVA_TOOL_OPTIONS");
  }

  @Test
  public void createEsCommand_for_windows_returns_command_for_default_settings() throws Exception {
    System2 system2 = Mockito.mock(System2.class);
    when(system2.isOsWindows()).thenReturn(true);
    prepareEsFileSystem();

    Properties props = new Properties();
    props.setProperty("sonar.search.host", "localhost");

    AbstractCommand esCommand = newFactory(props, system2).createEsCommand();
    EsInstallation esConfig = esCommand.getEsInstallation();

    assertThat(esCommand).isInstanceOf(JavaCommand.class);
    assertThat(esConfig.getClusterName()).isEqualTo("sonarqube");
    assertThat(esConfig.getHost()).isNotEmpty();
    assertThat(esConfig.getPort()).isEqualTo(9001);
    assertThat(esConfig.getEsJvmOptions().getAll())
      // enforced values
      .contains("-XX:+UseConcMarkSweepGC", "-server", "-Dfile.encoding=UTF-8")
      // default settings
      .contains("-Xms512m", "-Xmx512m", "-XX:+HeapDumpOnOutOfMemoryError");
    File esConfDir = new File(tempDir, "conf/es");
    assertThat(esCommand.getEnvVariables())
      .contains(entry("ES_JVM_OPTIONS", new File(esConfDir, "jvm.options").getAbsolutePath()))
      .containsKey("JAVA_HOME");
    assertThat(esConfig.getEsYmlSettings()).isNotNull();

    assertThat(esConfig.getLog4j2Properties())
      .contains(entry("appender.file_es.fileName", new File(logsDir, "es.log").getAbsolutePath()));

    assertThat(esCommand.getSuppressedEnvVariables()).containsOnly("JAVA_TOOL_OPTIONS");
  }

  @Test
  public void createEsCommand_returns_command_for_overridden_settings() throws Exception {
    prepareEsFileSystem();

    Properties props = new Properties();
    props.setProperty("sonar.search.host", "localhost");
    props.setProperty("sonar.cluster.name", "foo");
    props.setProperty("sonar.search.port", "1234");
    props.setProperty("sonar.search.javaOpts", "-Xms10G -Xmx10G");

    AbstractCommand esCommand = newFactory(props).createEsCommand();
    EsInstallation esConfig = esCommand.getEsInstallation();

    assertThat(esConfig.getClusterName()).isEqualTo("foo");
    assertThat(esConfig.getPort()).isEqualTo(1234);
    assertThat(esConfig.getEsJvmOptions().getAll())
      // enforced values
      .contains("-XX:+UseConcMarkSweepGC", "-server", "-Dfile.encoding=UTF-8")
      // user settings
      .contains("-Xms10G", "-Xmx10G")
      // default values disabled
      .doesNotContain("-XX:+HeapDumpOnOutOfMemoryError");
  }

  @Test
  public void createWebCommand_returns_command_for_default_settings() throws Exception {
    JavaCommand command = newFactory(new Properties()).createWebCommand(true);

    assertThat(command.getClassName()).isEqualTo("org.sonar.server.app.WebServer");
    assertThat(command.getWorkDir().getAbsolutePath()).isEqualTo(homeDir.getAbsolutePath());
    assertThat(command.getClasspath())
      .containsExactlyInAnyOrder("./lib/common/*", "./lib/server/*");
    assertThat(command.getJvmOptions().getAll())
      // enforced values
      .contains("-Djava.awt.headless=true", "-Dfile.encoding=UTF-8")
      // default settings
      .contains("-Djava.io.tmpdir=" + tempDir.getAbsolutePath(), "-Dfile.encoding=UTF-8")
      .contains("-Xmx512m", "-Xms128m", "-XX:+HeapDumpOnOutOfMemoryError");
    assertThat(command.getProcessId()).isEqualTo(ProcessId.WEB_SERVER);
    assertThat(command.getEnvVariables())
      .isNotEmpty();
    assertThat(command.getArguments())
      // default settings
      .contains(entry("sonar.web.javaOpts", "-Xmx512m -Xms128m -XX:+HeapDumpOnOutOfMemoryError"))
      .contains(entry("sonar.cluster.enabled", "false"));

    assertThat(command.getSuppressedEnvVariables()).containsOnly("JAVA_TOOL_OPTIONS");
  }

  @Test
  public void createCeCommand_returns_command_for_default_settings() throws Exception {
    JavaCommand command = newFactory(new Properties()).createCeCommand();

    assertThat(command.getClassName()).isEqualTo("org.sonar.ce.app.CeServer");
    assertThat(command.getWorkDir().getAbsolutePath()).isEqualTo(homeDir.getAbsolutePath());
    assertThat(command.getClasspath())
      .containsExactlyInAnyOrder("./lib/common/*", "./lib/server/*", "./lib/ce/*");
    assertThat(command.getJvmOptions().getAll())
      // enforced values
      .contains("-Djava.awt.headless=true", "-Dfile.encoding=UTF-8")
      // default settings
      .contains("-Djava.io.tmpdir=" + tempDir.getAbsolutePath(), "-Dfile.encoding=UTF-8")
      .contains("-Xmx512m", "-Xms128m", "-XX:+HeapDumpOnOutOfMemoryError");
    assertThat(command.getProcessId()).isEqualTo(ProcessId.COMPUTE_ENGINE);
    assertThat(command.getEnvVariables())
      .isNotEmpty();
    assertThat(command.getArguments())
      // default settings
      .contains(entry("sonar.web.javaOpts", "-Xmx512m -Xms128m -XX:+HeapDumpOnOutOfMemoryError"))
      .contains(entry("sonar.cluster.enabled", "false"));

    assertThat(command.getSuppressedEnvVariables()).containsOnly("JAVA_TOOL_OPTIONS");
  }

  @Test
  public void createWebCommand_configures_command_with_overridden_settings() throws Exception {
    Properties props = new Properties();
    props.setProperty("sonar.web.port", "1234");
    props.setProperty("sonar.web.javaOpts", "-Xmx10G");
    JavaCommand command = newFactory(props).createWebCommand(true);

    assertThat(command.getJvmOptions().getAll())
      // enforced values
      .contains("-Djava.awt.headless=true", "-Dfile.encoding=UTF-8")
      // default settings
      .contains("-Djava.io.tmpdir=" + tempDir.getAbsolutePath(), "-Dfile.encoding=UTF-8")
      // overridden values
      .contains("-Xmx10G")
      .doesNotContain("-Xms128m", "-XX:+HeapDumpOnOutOfMemoryError");
    assertThat(command.getArguments())
      // default settings
      .contains(entry("sonar.web.javaOpts", "-Xmx10G"))
      .contains(entry("sonar.cluster.enabled", "false"));

    assertThat(command.getSuppressedEnvVariables()).containsOnly("JAVA_TOOL_OPTIONS");
  }

  @Test
  public void createWebCommand_adds_configured_jdbc_driver_to_classpath() throws Exception {
    Properties props = new Properties();
    File driverFile = temp.newFile();
    props.setProperty("sonar.jdbc.driverPath", driverFile.getAbsolutePath());

    JavaCommand command = newFactory(props).createWebCommand(true);

    assertThat(command.getClasspath())
      .containsExactlyInAnyOrder("./lib/common/*", "./lib/server/*", driverFile.getAbsolutePath());
  }

  private void prepareEsFileSystem() throws IOException {
    FileUtils.touch(new File(homeDir, "elasticsearch/bin/elasticsearch"));
    FileUtils.touch(new File(homeDir, "elasticsearch/bin/elasticsearch.bat"));
  }

  private CommandFactoryImpl newFactory(Properties userProps) throws IOException {
    return newFactory(userProps, System2.INSTANCE);
  }

  private CommandFactoryImpl newFactory(Properties userProps, System2 system2) throws IOException {
    Properties p = new Properties();
    p.setProperty("sonar.path.home", homeDir.getAbsolutePath());
    p.setProperty("sonar.path.temp", tempDir.getAbsolutePath());
    p.setProperty("sonar.path.logs", logsDir.getAbsolutePath());
    p.putAll(userProps);

    Props props = new Props(p);
    ProcessProperties.completeDefaults(props);
    return new CommandFactoryImpl(props, tempDir, system2);
  }

  private <T> void attachMemoryAppenderToLoggerOf(Class<T> loggerClass) {
    this.listAppender = ListAppender.attachMemoryAppenderToLoggerOf(loggerClass);
  }

}
