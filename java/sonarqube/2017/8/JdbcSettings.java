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
package org.sonar.application.config;

import java.io.File;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.sonar.process.MessageException;
import org.sonar.process.ProcessProperties;
import org.sonar.process.Props;

import static java.lang.String.format;
import static org.apache.commons.lang.StringUtils.isEmpty;
import static org.apache.commons.lang.StringUtils.isNotEmpty;
import static org.sonar.process.ProcessProperties.JDBC_EMBEDDED_PORT;
import static org.sonar.process.ProcessProperties.JDBC_URL;

public class JdbcSettings implements Consumer<Props> {

  private static final int JDBC_EMBEDDED_PORT_DEFAULT_VALUE = 9092;

  enum Provider {
    H2("lib/jdbc/h2"), SQLSERVER("lib/jdbc/mssql"), MYSQL("lib/jdbc/mysql"), ORACLE("extensions/jdbc-driver/oracle"),
    POSTGRESQL("lib/jdbc/postgresql");

    final String path;

    Provider(String path) {
      this.path = path;
    }
  }

  @Override
  public void accept(Props props) {
    File homeDir = props.nonNullValueAsFile(ProcessProperties.PATH_HOME);
    Provider provider = resolveProviderAndEnforceNonnullJdbcUrl(props);
    String url = props.value(JDBC_URL);
    checkUrlParameters(provider, url);
    String driverPath = driverPath(homeDir, provider);
    props.set(ProcessProperties.JDBC_DRIVER_PATH, driverPath);
  }

  String driverPath(File homeDir, Provider provider) {
    String dirPath = provider.path;
    File dir = new File(homeDir, dirPath);
    if (!dir.exists()) {
      throw new MessageException("Directory does not exist: " + dirPath);
    }
    List<File> files = new ArrayList<>(FileUtils.listFiles(dir, new String[] {"jar"}, false));
    if (files.isEmpty()) {
      throw new MessageException("Directory does not contain JDBC driver: " + dirPath);
    }
    if (files.size() > 1) {
      throw new MessageException("Directory must contain only one JAR file: " + dirPath);
    }
    return files.get(0).getAbsolutePath();
  }

  Provider resolveProviderAndEnforceNonnullJdbcUrl(Props props) {
    String url = props.value(JDBC_URL);
    Integer embeddedDatabasePort = props.valueAsInt(JDBC_EMBEDDED_PORT);

    if (embeddedDatabasePort != null) {
      String correctUrl = buildH2JdbcUrl(embeddedDatabasePort);
      warnIfUrlIsSet(embeddedDatabasePort, url, correctUrl);
      props.set(JDBC_URL, correctUrl);
      return Provider.H2;
    }

    if (isEmpty(url)) {
      props.set(JDBC_URL, buildH2JdbcUrl(JDBC_EMBEDDED_PORT_DEFAULT_VALUE));
      props.set(JDBC_EMBEDDED_PORT, String.valueOf(JDBC_EMBEDDED_PORT_DEFAULT_VALUE));
      return Provider.H2;
    }

    Pattern pattern = Pattern.compile("jdbc:(\\w+):.+");
    Matcher matcher = pattern.matcher(url);
    if (!matcher.find()) {
      throw new MessageException(format("Bad format of JDBC URL: %s", url));
    }
    String key = matcher.group(1);
    try {
      return Provider.valueOf(StringUtils.upperCase(key));
    } catch (IllegalArgumentException e) {
      throw new MessageException(format("Unsupported JDBC driver provider: %s", key));
    }
  }

  private static String buildH2JdbcUrl(int embeddedDatabasePort) {
    InetAddress ip = InetAddress.getLoopbackAddress();
    String host;
    if (ip instanceof Inet6Address) {
      host = "[" + ip.getHostAddress() + "]";
    } else {
      host = ip.getHostAddress();
    }
    return format("jdbc:h2:tcp://%s:%d/sonar", host, embeddedDatabasePort);
  }

  void checkUrlParameters(Provider provider, String url) {
    if (Provider.MYSQL.equals(provider)) {
      checkRequiredParameter(url, "useUnicode=true");
      checkRequiredParameter(url, "characterEncoding=utf8");
      checkRecommendedParameter(url, "rewriteBatchedStatements=true");
      checkRecommendedParameter(url, "useConfigs=maxPerformance");
    }
  }

  private static void warnIfUrlIsSet(int port, String existing, String expectedUrl) {
    if (isNotEmpty(existing)) {
      Logger logger = LoggerFactory.getLogger(JdbcSettings.class);
      if (expectedUrl.equals(existing)) {
        logger.warn("To change H2 database port, only property '{}' should be set (which current value is '{}'). " +
          "Remove property '{}' from configuration to remove this warning.",
          JDBC_EMBEDDED_PORT, port,
          JDBC_URL);
      } else {
        logger.warn("Both '{}' and '{}' properties are set. " +
          "The value of property '{}' ('{}') is not consistent with the value of property '{}' ('{}'). " +
          "The value of property '{}' will be ignored and value '{}' will be used instead. " +
          "To remove this warning, either remove property '{}' if your intent was to use the embedded H2 database, otherwise remove property '{}'.",
          JDBC_EMBEDDED_PORT, JDBC_URL,
          JDBC_URL, existing, JDBC_EMBEDDED_PORT, port,
          JDBC_URL, expectedUrl,
          JDBC_URL, JDBC_EMBEDDED_PORT);
      }
    }
  }

  private static void checkRequiredParameter(String url, String val) {
    if (!url.contains(val)) {
      throw new MessageException(format("JDBC URL must have the property '%s'", val));
    }
  }

  private void checkRecommendedParameter(String url, String val) {
    if (!url.contains(val)) {
      LoggerFactory.getLogger(getClass()).warn("JDBC URL is recommended to have the property '{}'", val);
    }
  }
}
