/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.common.logging;

import org.apache.logging.log4j.Level;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.core.Appender;
import org.apache.logging.log4j.core.LoggerContext;
import org.apache.logging.log4j.core.appender.ConsoleAppender;
import org.apache.logging.log4j.core.appender.CountingNoOpAppender;
import org.apache.logging.log4j.core.config.Configurator;
import org.apache.logging.log4j.message.ParameterizedMessage;
import org.elasticsearch.cli.UserException;
import org.elasticsearch.common.io.PathUtils;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.env.Environment;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.hamcrest.RegexMatcher;

import javax.management.MBeanServerPermission;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.AccessControlException;
import java.security.Permission;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.startsWith;

public class EvilLoggerTests extends ESTestCase {

    @Override
    public void tearDown() throws Exception {
        LoggerContext context = (LoggerContext) LogManager.getContext(false);
        Configurator.shutdown(context);
        super.tearDown();
    }

    public void testLocationInfoTest() throws IOException, UserException {
        setupLogging("location_info");

        final Logger testLogger = ESLoggerFactory.getLogger("test");

        testLogger.error("This is an error message");
        testLogger.warn("This is a warning message");
        testLogger.info("This is an info message");
        testLogger.debug("This is a debug message");
        testLogger.trace("This is a trace message");
        final String path = System.getProperty("es.logs") + ".log";
        final List<String> events = Files.readAllLines(PathUtils.get(path));
        assertThat(events.size(), equalTo(5));
        final String location = "org.elasticsearch.common.logging.EvilLoggerTests.testLocationInfoTest";
        // the first message is a warning for unsupported configuration files
        assertLogLine(events.get(0), Level.ERROR, location, "This is an error message");
        assertLogLine(events.get(1), Level.WARN, location, "This is a warning message");
        assertLogLine(events.get(2), Level.INFO, location, "This is an info message");
        assertLogLine(events.get(3), Level.DEBUG, location, "This is a debug message");
        assertLogLine(events.get(4), Level.TRACE, location, "This is a trace message");
    }

    public void testDeprecationLogger() throws IOException, UserException {
        setupLogging("deprecation");

        final DeprecationLogger deprecationLogger = new DeprecationLogger(ESLoggerFactory.getLogger("deprecation"));

        deprecationLogger.deprecated("This is a deprecation message");
        final String deprecationPath = System.getProperty("es.logs") + "_deprecation.log";
        final List<String> deprecationEvents = Files.readAllLines(PathUtils.get(deprecationPath));
        assertThat(deprecationEvents.size(), equalTo(1));
        assertLogLine(
            deprecationEvents.get(0),
            Level.WARN,
            "org.elasticsearch.common.logging.DeprecationLogger.deprecated",
            "This is a deprecation message");
        assertWarnings("This is a deprecation message");
    }

    public void testFindAppender() throws IOException, UserException {
        setupLogging("find_appender");

        final Logger hasConsoleAppender = ESLoggerFactory.getLogger("has_console_appender");

        final Appender testLoggerConsoleAppender = Loggers.findAppender(hasConsoleAppender, ConsoleAppender.class);
        assertNotNull(testLoggerConsoleAppender);
        assertThat(testLoggerConsoleAppender.getName(), equalTo("console"));
        final Logger hasCountingNoOpAppender = ESLoggerFactory.getLogger("has_counting_no_op_appender");
        assertNull(Loggers.findAppender(hasCountingNoOpAppender, ConsoleAppender.class));
        final Appender countingNoOpAppender = Loggers.findAppender(hasCountingNoOpAppender, CountingNoOpAppender.class);
        assertThat(countingNoOpAppender.getName(), equalTo("counting_no_op"));
    }

    public void testPrefixLogger() throws IOException, IllegalAccessException, UserException {
        setupLogging("prefix");

        final String prefix = randomBoolean() ? null : randomAsciiOfLength(16);
        final Logger logger = Loggers.getLogger("prefix", prefix);
        logger.info("test");
        logger.info("{}", "test");
        final Exception e = new Exception("exception");
        logger.info(new ParameterizedMessage("{}", "test"), e);

        final String path = System.getProperty("es.logs") + ".log";
        final List<String> events = Files.readAllLines(PathUtils.get(path));

        final StringWriter sw = new StringWriter();
        final PrintWriter pw = new PrintWriter(sw);
        e.printStackTrace(pw);
        final int stackTraceLength = sw.toString().split(System.getProperty("line.separator")).length;
        final int expectedLogLines = 3;
        assertThat(events.size(), equalTo(expectedLogLines + stackTraceLength));
        for (int i = 0; i < expectedLogLines; i++) {
            if (prefix == null) {
                assertThat(events.get(i), startsWith("test"));
            } else {
                assertThat(events.get(i), startsWith("[" + prefix + "] test"));
            }
        }
    }


    public void testLog4jShutdownHack() {
        final AtomicBoolean denied = new AtomicBoolean();
        final SecurityManager sm = System.getSecurityManager();
        try {
            System.setSecurityManager(new SecurityManager() {
                @Override
                public void checkPermission(Permission perm) {
                    // just grant all permissions to Log4j, except we deny MBeanServerPermission
                    // "createMBeanServer" as this will trigger the Log4j bug
                    if (perm instanceof MBeanServerPermission && "createMBeanServer".equals(perm.getName())) {
                        // without the hack in place, Log4j will try to get an MBean server which we will deny
                        // with the hack in place, this permission should never be requested by Log4j
                        denied.set(true);
                        throw new AccessControlException("denied");
                    }
                }

                @Override
                public void checkPropertyAccess(String key) {
                    /*
                     * grant access to all properties; this is so that Log4j can check if its usage
                     * of JMX is disabled or not by reading log4j2.disable.jmx but there are other
                     * properties that Log4j will try to read as well and its simpler to just grant
                     * them all
                     */
                }
            });

            // this will trigger the bug without the hack
            LoggerContext context = (LoggerContext) LogManager.getContext(false);
            Configurator.shutdown(context);

            // Log4j should have never requested permissions to create an MBean server
            assertFalse(denied.get());
        } finally {
            System.setSecurityManager(sm);
        }
    }

    private void setupLogging(final String config) throws IOException, UserException {
        final Path configDir = getDataPath(config);
        // need to set custom path.conf so we can use a custom log4j2.properties file for the test
        final Settings settings = Settings.builder()
                .put(Environment.PATH_CONF_SETTING.getKey(), configDir.toAbsolutePath())
                .put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString())
                .build();
        final Environment environment = new Environment(settings);
        LogConfigurator.configure(environment);
    }

    private void assertLogLine(final String logLine, final Level level, final String location, final String message) {
        final Matcher matcher = Pattern.compile("\\[(.*)\\]\\[(.*)\\(.*\\)\\] (.*)").matcher(logLine);
        assertTrue(logLine, matcher.matches());
        assertThat(matcher.group(1), equalTo(level.toString()));
        assertThat(matcher.group(2), RegexMatcher.matches(location));
        assertThat(matcher.group(3), RegexMatcher.matches(message));
    }

}
