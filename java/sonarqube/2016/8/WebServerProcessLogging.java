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
package org.sonar.server.app;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.ConsoleAppender;
import org.sonar.process.LogbackHelper;
import org.sonar.process.Props;

/**
 * Configure logback for the Web Server process. Logs are written to console, which is
 * forwarded to file logs/sonar.log by the app master process.
 */
public class WebServerProcessLogging extends ServerProcessLogging {

  public WebServerProcessLogging() {
    super("web");
  }

  @Override
  protected void configureAppenders(String logFormat, LoggerContext ctx, LogbackHelper helper, Props props) {
    ConsoleAppender<ILoggingEvent> consoleAppender = helper.newConsoleAppender(ctx, "CONSOLE", logFormat);
    ctx.getLogger(Logger.ROOT_LOGGER_NAME).addAppender(consoleAppender);
  }

}
