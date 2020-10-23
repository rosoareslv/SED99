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
import org.apache.logging.log4j.core.LoggerContext;
import org.apache.logging.log4j.core.config.AbstractConfiguration;
import org.apache.logging.log4j.core.config.Configurator;
import org.apache.logging.log4j.core.config.builder.api.ConfigurationBuilder;
import org.apache.logging.log4j.core.config.builder.api.ConfigurationBuilderFactory;
import org.apache.logging.log4j.core.config.builder.impl.BuiltConfiguration;
import org.apache.logging.log4j.core.config.composite.CompositeConfiguration;
import org.apache.logging.log4j.core.config.properties.PropertiesConfiguration;
import org.apache.logging.log4j.core.config.properties.PropertiesConfigurationFactory;
import org.elasticsearch.cli.ExitCodes;
import org.elasticsearch.cli.UserException;
import org.elasticsearch.cluster.ClusterName;
import org.elasticsearch.common.SuppressForbidden;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.env.Environment;

import java.io.IOException;
import java.nio.file.FileVisitOption;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

public class LogConfigurator {

    /**
     * Configure logging without reading a log4j2.properties file, effectively configuring the
     * status logger and all loggers to the console.
     *
     * @param settings for configuring logger.level and individual loggers
     */
    public static void configureWithoutConfig(final Settings settings) {
        Objects.requireNonNull(settings);
        // we initialize the status logger immediately otherwise Log4j will complain when we try to get the context
        configureStatusLogger();
        configureLoggerLevels(settings);
    }

    /**
     * Configure logging reading from any log4j2.properties found in the config directory and its
     * subdirectories from the specified environment. Will also configure logging to point the logs
     * directory from the specified environment.
     *
     * @param environment the environment for reading configs and the logs path
     * @throws IOException   if there is an issue readings any log4j2.properties in the config
     *                       directory
     * @throws UserException if there are no log4j2.properties in the specified configs path
     */
    public static void configure(final Environment environment) throws IOException, UserException {
        Objects.requireNonNull(environment);
        configure(environment.settings(), environment.configFile(), environment.logsFile());
    }

    private static void configure(final Settings settings, final Path configsPath, final Path logsPath) throws IOException, UserException {
        Objects.requireNonNull(settings);
        Objects.requireNonNull(configsPath);
        Objects.requireNonNull(logsPath);

        setLogConfigurationSystemProperty(logsPath, settings);
        // we initialize the status logger immediately otherwise Log4j will complain when we try to get the context
        configureStatusLogger();

        final LoggerContext context = (LoggerContext) LogManager.getContext(false);

        final List<AbstractConfiguration> configurations = new ArrayList<>();
        final PropertiesConfigurationFactory factory = new PropertiesConfigurationFactory();
        final Set<FileVisitOption> options = EnumSet.of(FileVisitOption.FOLLOW_LINKS);
        Files.walkFileTree(configsPath, options, Integer.MAX_VALUE, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                if (file.getFileName().toString().equals("log4j2.properties")) {
                    configurations.add((PropertiesConfiguration) factory.getConfiguration(context, file.toString(), file.toUri()));
                }
                return FileVisitResult.CONTINUE;
            }
        });

        if (configurations.isEmpty()) {
            throw new UserException(
                    ExitCodes.CONFIG,
                    "no log4j2.properties found; tried [" + configsPath + "] and its subdirectories");
        }

        context.start(new CompositeConfiguration(configurations));

        configureLoggerLevels(settings);
    }

    private static void configureStatusLogger() {
        final ConfigurationBuilder<BuiltConfiguration> builder = ConfigurationBuilderFactory.newConfigurationBuilder();
        builder.setStatusLevel(Level.ERROR);
        Configurator.initialize(builder.build());
    }

    private static void configureLoggerLevels(Settings settings) {
        if (ESLoggerFactory.LOG_DEFAULT_LEVEL_SETTING.exists(settings)) {
            final Level level = ESLoggerFactory.LOG_DEFAULT_LEVEL_SETTING.get(settings);
            Loggers.setLevel(ESLoggerFactory.getRootLogger(), level);
        }

        final Map<String, String> levels = settings.filter(ESLoggerFactory.LOG_LEVEL_SETTING::match).getAsMap();
        for (String key : levels.keySet()) {
            final Level level = ESLoggerFactory.LOG_LEVEL_SETTING.getConcreteSetting(key).get(settings);
            Loggers.setLevel(ESLoggerFactory.getLogger(key.substring("logger.".length())), level);
        }
    }


    @SuppressForbidden(reason = "sets system property for logging configuration")
    private static void setLogConfigurationSystemProperty(final Path logsPath, final Settings settings) {
        System.setProperty("es.logs", logsPath.resolve(ClusterName.CLUSTER_NAME_SETTING.get(settings).value()).toString());
    }

}
