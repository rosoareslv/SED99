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
package org.sonarqube.ws.client.project;

import java.util.Arrays;
import org.junit.Rule;
import org.junit.Test;
import org.sonarqube.ws.WsProjects;
import org.sonarqube.ws.client.ServiceTester;
import org.sonarqube.ws.client.WsConnector;

import static java.util.Arrays.asList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.data.MapEntry.entry;
import static org.mockito.Mockito.mock;
import static org.sonar.api.server.ws.WebService.Param.PAGE;
import static org.sonar.api.server.ws.WebService.Param.PAGE_SIZE;

public class ProjectsServiceTest {

  @Rule
  public ServiceTester<ProjectsService> serviceTester = new ServiceTester<>(new ProjectsService(mock(WsConnector.class)));

  private ProjectsService underTest = serviceTester.getInstanceUnderTest();

  @Test
  public void creates_project() {
    underTest.create(CreateRequest.builder()
      .setKey("project_key")
      .setName("Project Name")
      .build());

    assertThat(serviceTester.getPostParser()).isSameAs(WsProjects.CreateWsResponse.parser());
    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/create");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("project", "project_key"),
      entry("name", "Project Name"));
  }

  @Test
  public void creates_project_on_organization() {
    underTest.create(CreateRequest.builder()
      .setOrganization("org_key")
      .setKey("project_key")
      .setName("Project Name")
      .build());

    assertThat(serviceTester.getPostParser()).isSameAs(WsProjects.CreateWsResponse.parser());
    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/create");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("organization", "org_key"),
      entry("project", "project_key"),
      entry("name", "Project Name"));
  }

  @Test
  public void creates_project_on_branch() {
    underTest.create(CreateRequest.builder()
      .setKey("project_key")
      .setName("Project Name")
      .setBranch("the_branch")
      .build());

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/create");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("project", "project_key"),
      entry("name", "Project Name"),
      entry("branch", "the_branch"));
  }

  @Test
  public void creates_public_project() {
    underTest.create(CreateRequest.builder()
      .setKey("project_key")
      .setName("Project Name")
      .setVisibility("public")
      .build());

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/create");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("project", "project_key"),
      entry("name", "Project Name"),
      entry("visibility", "public"));
  }

  @Test
  public void creates_private_project() {
    underTest.create(CreateRequest.builder()
      .setKey("project_key")
      .setName("Project Name")
      .setVisibility("private")
      .build());

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/create");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("project", "project_key"),
      entry("name", "Project Name"),
      entry("visibility", "private"));
  }

  @Test
  public void delete() {
    underTest.delete(DeleteRequest.builder().setKey("P1").build());

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/delete");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(entry("project", "P1"));
  }

  @Test
  public void bulk_delete() {
    BulkDeleteRequest request = BulkDeleteRequest.builder().setProjectKeys(Arrays.asList("p1", "p2")).setOrganization("my-org").build();
    underTest.bulkDelete(request);

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/bulk_delete");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(entry("organization", "my-org"), entry("projects", "p1,p2"));
  }

  @Test
  public void search() {
    underTest.search(SearchWsRequest.builder()
      .setOrganization("default")
      .setQuery("project")
      .setQualifiers(asList("TRK", "VW"))
      .setPage(3)
      .setPageSize(10)
      .build());

    serviceTester.assertThat(serviceTester.getGetRequest())
      .hasPath("search")
      .hasParam("organization", "default")
      .hasParam("q", "project")
      .hasParam("qualifiers", "TRK,VW")
      .hasParam(PAGE, 3)
      .hasParam(PAGE_SIZE, 10)
      .andNoOtherParam();
  }

  @Test
  public void update_visibility() {
    underTest.updateVisibility(UpdateVisibilityRequest.builder()
      .setProject("project_key")
      .setVisibility("public")
      .build());

    assertThat(serviceTester.getPostRequest().getPath()).isEqualTo("api/projects/update_visibility");
    assertThat(serviceTester.getPostRequest().getParams()).containsOnly(
      entry("project", "project_key"),
      entry("visibility", "public"));
  }
}
