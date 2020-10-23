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
package org.sonar.process;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Map;
import java.util.Properties;

/**
 * Constants shared by search, web server and app processes.
 * They are almost all the properties defined in conf/sonar.properties.
 */
public class ProcessProperties {
  public static final String CLUSTER_ENABLED = "sonar.cluster.enabled";
  public static final String CLUSTER_CE_DISABLED = "sonar.cluster.ce.disabled";
  public static final String CLUSTER_SEARCH_DISABLED = "sonar.cluster.search.disabled";
  public static final String CLUSTER_SEARCH_HOSTS = "sonar.cluster.search.hosts";
  public static final String CLUSTER_WEB_DISABLED = "sonar.cluster.web.disabled";
  public static final String CLUSTER_HOSTS = "sonar.cluster.hosts";
  public static final String CLUSTER_PORT = "sonar.cluster.port";
  public static final String CLUSTER_NETWORK_INTERFACES = "sonar.cluster.networkInterfaces";
  public static final String CLUSTER_NAME = "sonar.cluster.name";
  public static final String HAZELCAST_LOG_LEVEL = "sonar.log.level.app.hazelcast";
  public static final String CLUSTER_WEB_LEADER = "sonar.cluster.web.startupLeader";
  // Internal property used by sonar-application to share the local endpoint of Hazelcast
  public static final String CLUSTER_LOCALENDPOINT = "sonar.cluster.hazelcast.localEndPoint";
  // Internal property used by sonar-application to share the local UUID of the Hazelcast member
  public static final String CLUSTER_MEMBERUUID = "sonar.cluster.hazelcast.memberUUID";

  public static final String JDBC_URL = "sonar.jdbc.url";
  public static final String JDBC_DRIVER_PATH = "sonar.jdbc.driverPath";
  public static final String JDBC_MAX_ACTIVE = "sonar.jdbc.maxActive";
  public static final String JDBC_MAX_IDLE = "sonar.jdbc.maxIdle";
  public static final String JDBC_MIN_IDLE = "sonar.jdbc.minIdle";
  public static final String JDBC_MAX_WAIT = "sonar.jdbc.maxWait";
  public static final String JDBC_MIN_EVICTABLE_IDLE_TIME_MILLIS = "sonar.jdbc.minEvictableIdleTimeMillis";
  public static final String JDBC_TIME_BETWEEN_EVICTION_RUNS_MILLIS = "sonar.jdbc.timeBetweenEvictionRunsMillis";
  public static final String JDBC_EMBEDDED_PORT = "sonar.embeddedDatabase.port";

  public static final String PATH_DATA = "sonar.path.data";
  public static final String PATH_HOME = "sonar.path.home";
  public static final String PATH_LOGS = "sonar.path.logs";
  public static final String PATH_TEMP = "sonar.path.temp";
  public static final String PATH_WEB = "sonar.path.web";

  public static final String SEARCH_HOST = "sonar.search.host";
  public static final String SEARCH_PORT = "sonar.search.port";
  public static final String SEARCH_HTTP_PORT = "sonar.search.httpPort";
  public static final String SEARCH_JAVA_OPTS = "sonar.search.javaOpts";
  public static final String SEARCH_JAVA_ADDITIONAL_OPTS = "sonar.search.javaAdditionalOpts";
  public static final String SEARCH_REPLICAS = "sonar.search.replicas";
  public static final String SEARCH_MINIMUM_MASTER_NODES = "sonar.search.minimumMasterNodes";
  public static final String SEARCH_INITIAL_STATE_TIMEOUT = "sonar.search.initialStateTimeout";

  public static final String WEB_JAVA_OPTS = "sonar.web.javaOpts";

  public static final String WEB_JAVA_ADDITIONAL_OPTS = "sonar.web.javaAdditionalOpts";
  public static final String CE_JAVA_OPTS = "sonar.ce.javaOpts";
  public static final String CE_JAVA_ADDITIONAL_OPTS = "sonar.ce.javaAdditionalOpts";

  /**
   * Used by Orchestrator to ask for shutdown of monitor process
   */
  public static final String ENABLE_STOP_COMMAND = "sonar.enableStopCommand";

  public static final String WEB_ENFORCED_JVM_ARGS = "-Djava.awt.headless=true -Dfile.encoding=UTF-8";

  public static final String CE_ENFORCED_JVM_ARGS = "-Djava.awt.headless=true -Dfile.encoding=UTF-8";

  public static final String HTTP_PROXY_HOST = "http.proxyHost";
  public static final String HTTPS_PROXY_HOST = "https.proxyHost";
  public static final String HTTP_PROXY_PORT = "http.proxyPort";
  public static final String HTTPS_PROXY_PORT = "https.proxyPort";
  public static final String HTTP_PROXY_USER = "http.proxyUser";
  public static final String HTTP_PROXY_PASSWORD = "http.proxyPassword";

  private ProcessProperties() {
    // only static stuff
  }

  public static void completeDefaults(Props props) {
    // init string properties
    for (Map.Entry<Object, Object> entry : defaults().entrySet()) {
      props.setDefault(entry.getKey().toString(), entry.getValue().toString());
    }

    fixPortIfZero(props, SEARCH_HOST, SEARCH_PORT);
  }

  private static void fixPortIfZero(Props props, String addressPropertyKey, String portPropertyKey) {
    String port = props.value(portPropertyKey);
    if ("0".equals(port)) {
      String address = props.nonNullValue(addressPropertyKey);
      try {
        props.set(portPropertyKey, String.valueOf(NetworkUtils.getNextAvailablePort(InetAddress.getByName(address))));
      } catch (UnknownHostException e) {
        throw new IllegalStateException("Cannot resolve address [" + address + "] set by property [" + addressPropertyKey + "]", e);
      }
    }
  }

  public static Properties defaults() {
    Properties defaults = new Properties();
    defaults.put(SEARCH_HOST, InetAddress.getLoopbackAddress().getHostAddress());
    defaults.put(SEARCH_PORT, "9001");
    defaults.put(SEARCH_JAVA_OPTS, "-Xms512m" +
      " -Xmx512m"  +
      " -XX:+UseConcMarkSweepGC"  +
      " -XX:CMSInitiatingOccupancyFraction=75"  +
      " -XX:+UseCMSInitiatingOccupancyOnly"  +
      " -XX:+AlwaysPreTouch"  +
      " -server"  +
      " -Xss1m"  +
      " -Djava.awt.headless=true"  +
      " -Dfile.encoding=UTF-8"  +
      " -Djna.nosys=true"  +
      " -Djdk.io.permissionsUseCanonicalPath=true"  +
      " -Dio.netty.noUnsafe=true"  +
      " -Dio.netty.noKeySetOptimization=true"  +
      " -Dio.netty.recycler.maxCapacityPerThread=0"  +
      " -Dlog4j.shutdownHookEnabled=false"  +
      " -Dlog4j2.disable.jmx=true"  +
      " -Dlog4j.skipJansi=true"  +
      " -XX:+HeapDumpOnOutOfMemoryError");
    defaults.put(SEARCH_JAVA_ADDITIONAL_OPTS, "");

    defaults.put(PATH_DATA, "data");
    defaults.put(PATH_LOGS, "logs");
    defaults.put(PATH_TEMP, "temp");
    defaults.put(PATH_WEB, "web");

    defaults.put(WEB_JAVA_OPTS, "-Xmx512m -Xms128m -XX:+HeapDumpOnOutOfMemoryError");
    defaults.put(WEB_JAVA_ADDITIONAL_OPTS, "");
    defaults.put(CE_JAVA_OPTS, "-Xmx512m -Xms128m -XX:+HeapDumpOnOutOfMemoryError");
    defaults.put(CE_JAVA_ADDITIONAL_OPTS, "");
    defaults.put(JDBC_MAX_ACTIVE, "60");
    defaults.put(JDBC_MAX_IDLE, "5");
    defaults.put(JDBC_MIN_IDLE, "2");
    defaults.put(JDBC_MAX_WAIT, "5000");
    defaults.put(JDBC_MIN_EVICTABLE_IDLE_TIME_MILLIS, "600000");
    defaults.put(JDBC_TIME_BETWEEN_EVICTION_RUNS_MILLIS, "30000");

    defaults.put(CLUSTER_ENABLED, "false");
    defaults.put(CLUSTER_CE_DISABLED, "false");
    defaults.put(CLUSTER_WEB_DISABLED, "false");
    defaults.put(CLUSTER_SEARCH_DISABLED, "false");
    defaults.put(CLUSTER_NAME, "sonarqube");
    defaults.put(CLUSTER_NETWORK_INTERFACES, "");
    defaults.put(CLUSTER_HOSTS, "");
    defaults.put(CLUSTER_PORT, "9003");
    defaults.put(HAZELCAST_LOG_LEVEL, "WARN");

    return defaults;
  }
}
