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
package org.sonar.server.issue.ws;

import com.google.common.io.Resources;
import org.sonar.api.server.ws.Request;
import org.sonar.api.server.ws.Response;
import org.sonar.api.server.ws.WebService;
import org.sonar.api.server.ws.WebService.NewAction;
import org.sonar.api.server.ws.WebService.Param;
import org.sonar.api.utils.text.JsonWriter;
import org.sonar.server.issue.IssueService;

import static org.sonarqube.ws.client.issue.IssuesWsParameters.ACTION_AUTHORS;

public class AuthorsAction implements IssuesWsAction {

  private final IssueService service;

  public AuthorsAction(IssueService service) {
    this.service = service;
  }

  @Override
  public void handle(Request request, Response response) throws Exception {
    String query = request.param(Param.TEXT_QUERY);
    int pageSize = request.mandatoryParamAsInt(Param.PAGE_SIZE);

    JsonWriter json = response.newJsonWriter()
      .beginObject()
      .name("authors")
      .beginArray();

    for (String login : service.listAuthors(query, pageSize)) {
      json.value(login);
    }

    json.endArray().endObject().close();
  }

  @Override
  public void define(WebService.NewController controller) {
    NewAction action = controller.createAction(ACTION_AUTHORS)
      .setSince("5.1")
      .setDescription("Search SCM accounts which match a given query")
      .setResponseExample(Resources.getResource(this.getClass(), "example-authors.json"))
      .setHandler(this);

    action.createParam(Param.TEXT_QUERY)
      .setDescription("A pattern to match SCM accounts against")
      .setExampleValue("luke");
    action.createParam(Param.PAGE_SIZE)
      .setDescription("The size of the list to return")
      .setExampleValue("25")
      .setDefaultValue("10");
  }
}
