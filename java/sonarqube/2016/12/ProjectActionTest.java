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
package org.sonar.server.batch;

import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.sonar.scanner.protocol.input.FileData;
import org.sonar.scanner.protocol.input.ProjectRepositories;
import org.sonar.server.ws.TestResponse;
import org.sonar.server.ws.WsActionTester;
import org.sonarqube.ws.MediaTypes;
import org.sonarqube.ws.WsBatch.WsProjectResponse;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.test.JsonAssert.assertJson;

public class ProjectActionTest {

  ProjectDataLoader projectDataLoader = mock(ProjectDataLoader.class);

  WsActionTester ws;

  @Before
  public void setUp() {
    ws = new WsActionTester(new ProjectAction(projectDataLoader));
  }

  @Test
  public void project_referentials() throws Exception {
    String projectKey = "org.codehaus.sonar:sonar";

    ProjectRepositories projectReferentials = mock(ProjectRepositories.class);
    when(projectReferentials.toJson()).thenReturn("{\"settingsByModule\": {}}");

    ArgumentCaptor<ProjectDataQuery> queryArgumentCaptor = ArgumentCaptor.forClass(ProjectDataQuery.class);
    when(projectDataLoader.load(queryArgumentCaptor.capture())).thenReturn(projectReferentials);

    TestResponse response = ws.newRequest()
      .setParam("key", projectKey)
      .setParam("profile", "Default")
      .setParam("preview", "false")
      .execute();
    assertJson(response.getInput()).isSimilarTo("{\"settingsByModule\": {}}");

    assertThat(queryArgumentCaptor.getValue().getModuleKey()).isEqualTo(projectKey);
    assertThat(queryArgumentCaptor.getValue().getProfileName()).isEqualTo("Default");
    assertThat(queryArgumentCaptor.getValue().isIssuesMode()).isFalse();
  }

  /**
   * SONAR-7084
   */
  @Test
  public void do_not_fail_when_a_path_is_null() throws Exception {
    String projectKey = "org.codehaus.sonar:sonar";

    ProjectRepositories projectRepositories = new ProjectRepositories().addFileData("module-1", null, new FileData(null, null));
    when(projectDataLoader.load(any(ProjectDataQuery.class))).thenReturn(projectRepositories);

    TestResponse result = ws.newRequest()
      .setMediaType(MediaTypes.PROTOBUF)
      .setParam("key", projectKey)
      .setParam("profile", "Default")
      .execute();

    WsProjectResponse wsProjectResponse = WsProjectResponse.parseFrom(result.getInputStream());
    assertThat(wsProjectResponse.getFileDataByModuleAndPath()).isEmpty();
  }
}
