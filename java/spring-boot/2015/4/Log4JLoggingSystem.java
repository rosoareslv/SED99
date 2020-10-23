/*
 * Copyright 2012-2015 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.logging.log4j;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.apache.log4j.Level;
import org.apache.log4j.LogManager;
import org.apache.log4j.Logger;
import org.springframework.boot.logging.LogFile;
import org.springframework.boot.logging.LogLevel;
import org.springframework.boot.logging.LoggingSystem;
import org.springframework.boot.logging.Slf4JLoggingSystem;
import org.springframework.util.Assert;
import org.springframework.util.Log4jConfigurer;
import org.springframework.util.StringUtils;

/**
 * {@link LoggingSystem} for <a href="http://logging.apache.org/log4j/1.2">Log4j</a>.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Andy Wilkinson
 */
public class Log4JLoggingSystem extends Slf4JLoggingSystem {

	private static final Map<LogLevel, Level> LEVELS;
	static {
		Map<LogLevel, Level> levels = new HashMap<LogLevel, Level>();
		levels.put(LogLevel.TRACE, Level.TRACE);
		levels.put(LogLevel.DEBUG, Level.DEBUG);
		levels.put(LogLevel.INFO, Level.INFO);
		levels.put(LogLevel.WARN, Level.WARN);
		levels.put(LogLevel.ERROR, Level.ERROR);
		levels.put(LogLevel.FATAL, Level.ERROR);
		levels.put(LogLevel.OFF, Level.OFF);
		LEVELS = Collections.unmodifiableMap(levels);
	}

	public Log4JLoggingSystem(ClassLoader classLoader) {
		super(classLoader);
	}

	@Override
	protected String[] getStandardConfigLocations() {
		return new String[] { "log4j.xml", "log4j.properties" };
	}

	@Override
	public void beforeInitialize() {
		super.beforeInitialize();
		LogManager.getRootLogger().setLevel(Level.FATAL);
	}

	@Override
	protected void loadDefaults(LogFile logFile) {
		if (logFile != null) {
			loadConfiguration(getPackagedConfigFile("log4j-file.properties"), logFile);
		}
		else {
			loadConfiguration(getPackagedConfigFile("log4j.properties"), logFile);
		}
	}

	@Override
	protected void loadConfiguration(String location, LogFile logFile) {
		Assert.notNull(location, "Location must not be null");
		if (logFile != null) {
			logFile.applyToSystemProperties();
		}
		try {
			Log4jConfigurer.initLogging(location);
		}
		catch (Exception ex) {
			throw new IllegalStateException("Could not initialize Log4J logging from "
					+ location, ex);
		}
	}

	@Override
	protected void reinitialize() {
		loadConfiguration(getSelfInitializationConfig(), null);
	}

	@Override
	public void setLogLevel(String loggerName, LogLevel level) {
		Logger logger = (StringUtils.hasLength(loggerName) ? LogManager
				.getLogger(loggerName) : LogManager.getRootLogger());
		logger.setLevel(LEVELS.get(level));
	}

}
