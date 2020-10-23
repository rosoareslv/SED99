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
package it.ui;

import com.sonar.orchestrator.Orchestrator;
import it.Category4Suite;
import java.util.Map;
import org.junit.ClassRule;
import org.junit.Rule;
import org.junit.Test;
import org.sonarqube.ws.client.GetRequest;
import org.sonarqube.ws.client.WsResponse;
import pageobjects.Navigation;
import util.ItUtils;

import static com.codeborne.selenide.Condition.hasText;

public class UiTest {

  @ClassRule
  public static final Orchestrator ORCHESTRATOR = Category4Suite.ORCHESTRATOR;

  @Rule
  public Navigation nav = Navigation.get(ORCHESTRATOR);

  @Test
  public void footer_contains_information() {
    nav.getFooter()
      .should(hasText("Documentation"))
      .should(hasText("SonarSource SA"));
  }

  @Test
  public void footer_contains_version() {
    WsResponse status = ItUtils.newAdminWsClient(ORCHESTRATOR).wsConnector().call(new GetRequest("api/system/status"));
    Map<String, Object> statusMap = ItUtils.jsonToMap(status.content());

    nav.getFooter().should(hasText((String) statusMap.get("version")));
  }
}
