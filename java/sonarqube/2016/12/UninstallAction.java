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
package org.sonar.server.plugins.ws;

import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.server.plugins.ServerPluginRepository;
import org.sonar.server.user.UserSession;

import static java.lang.String.format;

/**
 * Implementation of the {@code uninstall} action for the Plugins WebService.
 */
public class UninstallAction implements PluginsWsAction {
  private static final String PARAM_KEY = "key";

  private final ServerPluginRepository pluginRepository;
  private final UserSession userSession;

  public UninstallAction(ServerPluginRepository pluginRepository, UserSession userSession) {
    this.pluginRepository = pluginRepository;
    this.userSession = userSession;
  }

  @Override
  public void define(WebService.NewController controller) {
    WebService.NewAction action = controller.createAction("uninstall")
      .setPost(true)
      .setSince("5.2")
      .setDescription("Uninstalls the plugin specified by its key." +
        "<br/>" +
        "Requires user to be authenticated with Administer System permissions.")
      .setHandler(this);

    action.createParam(PARAM_KEY)
      .setDescription("The key identifying the plugin to uninstall")
      .setRequired(true);
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    userSession.checkIsRoot();

    String key = request.mandatoryParam(PARAM_KEY);
    ensurePluginIsInstalled(key);
    pluginRepository.uninstall(key);
    response.noContent();
  }

  // FIXME should be moved to ServerPluginRepository#uninstall(String)
  private void ensurePluginIsInstalled(String key) {
    if (!pluginRepository.hasPlugin(key)) {
      throw new IllegalArgumentException(format("Plugin [%s] is not installed", key));
    }
  }
}
