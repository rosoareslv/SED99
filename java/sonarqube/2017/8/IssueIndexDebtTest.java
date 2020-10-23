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
package org.sonar.server.issue.index;

import java.util.Map;
import java.util.TimeZone;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.config.internal.MapSettings;
import org.sonar.api.issue.Issue;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.rule.Severity;
import org.sonar.api.utils.DateUtils;
import org.sonar.api.utils.System2;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTesting;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.server.es.EsTester;
import org.sonar.server.es.Facets;
import org.sonar.server.es.SearchOptions;
import org.sonar.server.issue.IssueDocTesting;
import org.sonar.server.issue.IssueQuery;
import org.sonar.server.issue.IssueQuery.Builder;
import org.sonar.server.permission.index.AuthorizationTypeSupport;
import org.sonar.server.permission.index.PermissionIndexerDao;
import org.sonar.server.permission.index.PermissionIndexerTester;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.view.index.ViewIndexDefinition;

import static java.util.Arrays.asList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.entry;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.db.organization.OrganizationTesting.newOrganizationDto;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.DEPRECATED_FACET_MODE_DEBT;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.FACET_MODE_EFFORT;

public class IssueIndexDebtTest {

  private System2 system2 = System2.INSTANCE;

  @Rule
  public EsTester es = new EsTester(new IssueIndexDefinition(new MapSettings().asConfig()), new ViewIndexDefinition(new MapSettings().asConfig()));
  @Rule
  public UserSessionRule userSessionRule = UserSessionRule.standalone();
  @Rule
  public DbTester db = DbTester.create(system2);

  private IssueIndexer issueIndexer = new IssueIndexer(es.client(), db.getDbClient(), new IssueIteratorFactory(db.getDbClient()));
  private PermissionIndexerTester authorizationIndexerTester = new PermissionIndexerTester(es, issueIndexer);
  private IssueIndex underTest;

  @Before
  public void setUp() {
    System2 system = mock(System2.class);
    when(system.getDefaultTimeZone()).thenReturn(TimeZone.getTimeZone("+01:00"));
    when(system.now()).thenReturn(System.currentTimeMillis());
    underTest = new IssueIndex(es.client(), system, userSessionRule, new AuthorizationTypeSupport(userSessionRule));
  }

  @Test
  public void facets_on_projects() {
    OrganizationDto organizationDto = newOrganizationDto();
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto, "ABCD");
    ComponentDto project2 = ComponentTesting.newPrivateProjectDto(organizationDto, "EFGH");

    indexIssues(
      IssueDocTesting.newDoc("I1", ComponentTesting.newFileDto(project, null)).setEffort(10L),
      IssueDocTesting.newDoc("I2", ComponentTesting.newFileDto(project, null)).setEffort(10L),
      IssueDocTesting.newDoc("I3", ComponentTesting.newFileDto(project2, null)).setEffort(10L));

    Facets facets = search("projectUuids");
    assertThat(facets.getNames()).containsOnly("projectUuids", FACET_MODE_EFFORT);
    assertThat(facets.get("projectUuids")).containsOnly(entry("ABCD", 20L), entry("EFGH", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 30L));
  }

  @Test
  public void facets_on_components() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto(), "A");
    ComponentDto file1 = ComponentTesting.newFileDto(project, null, "ABCD");
    ComponentDto file2 = ComponentTesting.newFileDto(project, null, "BCDE");
    ComponentDto file3 = ComponentTesting.newFileDto(project, null, "CDEF");

    indexIssues(
      IssueDocTesting.newDoc("I1", project).setEffort(10L),
      IssueDocTesting.newDoc("I2", file1).setEffort(10L),
      IssueDocTesting.newDoc("I3", file2).setEffort(10L),
      IssueDocTesting.newDoc("I4", file2).setEffort(10L),
      IssueDocTesting.newDoc("I5", file3).setEffort(10L));

    Facets facets = search("fileUuids");
    assertThat(facets.getNames()).containsOnly("fileUuids", FACET_MODE_EFFORT);
    assertThat(facets.get("fileUuids"))
      .containsOnly(entry("A", 10L), entry("ABCD", 10L), entry("BCDE", 20L), entry("CDEF", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 50L));
  }

  @Test
  public void facets_on_directories() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file1 = ComponentTesting.newFileDto(project, null).setPath("src/main/xoo/F1.xoo");
    ComponentDto file2 = ComponentTesting.newFileDto(project, null).setPath("F2.xoo");

    indexIssues(
      IssueDocTesting.newDoc("I1", file1).setDirectoryPath("/src/main/xoo").setEffort(10L),
      IssueDocTesting.newDoc("I2", file2).setDirectoryPath("/").setEffort(10L));

    Facets facets = search("directories");
    assertThat(facets.getNames()).containsOnly("directories", FACET_MODE_EFFORT);
    assertThat(facets.get("directories")).containsOnly(entry("/src/main/xoo", 10L), entry("/", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 20L));
  }

  @Test
  public void facets_on_severities() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    indexIssues(
      IssueDocTesting.newDoc("I1", file).setSeverity(Severity.INFO).setEffort(10L),
      IssueDocTesting.newDoc("I2", file).setSeverity(Severity.INFO).setEffort(10L),
      IssueDocTesting.newDoc("I3", file).setSeverity(Severity.MAJOR).setEffort(10L));

    Facets facets = search("severities");
    assertThat(facets.getNames()).containsOnly("severities", FACET_MODE_EFFORT);
    assertThat(facets.get("severities")).containsOnly(entry("INFO", 20L), entry("MAJOR", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 30L));
  }

  @Test
  public void facets_on_statuses() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    indexIssues(
      IssueDocTesting.newDoc("I1", file).setStatus(Issue.STATUS_CLOSED).setEffort(10L),
      IssueDocTesting.newDoc("I2", file).setStatus(Issue.STATUS_CLOSED).setEffort(10L),
      IssueDocTesting.newDoc("I3", file).setStatus(Issue.STATUS_OPEN).setEffort(10L));

    Facets facets = search("statuses");
    assertThat(facets.getNames()).containsOnly("statuses", FACET_MODE_EFFORT);
    assertThat(facets.get("statuses")).containsOnly(entry("CLOSED", 20L), entry("OPEN", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 30L));
  }

  @Test
  public void facets_on_resolutions() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    indexIssues(
      IssueDocTesting.newDoc("I1", file).setResolution(Issue.RESOLUTION_FALSE_POSITIVE).setEffort(10L),
      IssueDocTesting.newDoc("I2", file).setResolution(Issue.RESOLUTION_FALSE_POSITIVE).setEffort(10L),
      IssueDocTesting.newDoc("I3", file).setResolution(Issue.RESOLUTION_FIXED).setEffort(10L));

    Facets facets = search("resolutions");
    assertThat(facets.getNames()).containsOnly("resolutions", FACET_MODE_EFFORT);
    assertThat(facets.get("resolutions")).containsOnly(entry("FALSE-POSITIVE", 20L), entry("FIXED", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 30L));
  }

  @Test
  public void facets_on_languages() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);
    RuleKey ruleKey = RuleKey.of("repo", "X1");

    indexIssues(IssueDocTesting.newDoc("I1", file).setRuleKey(ruleKey.toString()).setLanguage("xoo").setEffort(10L));

    Facets facets = search("languages");
    assertThat(facets.getNames()).containsOnly("languages", FACET_MODE_EFFORT);
    assertThat(facets.get("languages")).containsOnly(entry("xoo", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 10L));
  }

  private Facets search(String additionalFacet) {
    return new Facets(underTest.search(newQueryBuilder().build(), new SearchOptions().addFacets(asList(additionalFacet))));
  }

  @Test
  public void facets_on_assignees() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    indexIssues(
      IssueDocTesting.newDoc("I1", file).setAssignee("steph").setEffort(10L),
      IssueDocTesting.newDoc("I2", file).setAssignee("simon").setEffort(10L),
      IssueDocTesting.newDoc("I3", file).setAssignee("simon").setEffort(10L),
      IssueDocTesting.newDoc("I4", file).setAssignee(null).setEffort(10L));

    Facets facets = new Facets(underTest.search(newQueryBuilder().build(), new SearchOptions().addFacets(asList("assignees"))));
    assertThat(facets.getNames()).containsOnly("assignees", FACET_MODE_EFFORT);
    assertThat(facets.get("assignees")).containsOnly(entry("steph", 10L), entry("simon", 20L), entry("", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 40L));
  }

  @Test
  public void facets_on_authors() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    indexIssues(
      IssueDocTesting.newDoc("I1", file).setAuthorLogin("steph").setEffort(10L),
      IssueDocTesting.newDoc("I2", file).setAuthorLogin("simon").setEffort(10L),
      IssueDocTesting.newDoc("I3", file).setAuthorLogin("simon").setEffort(10L),
      IssueDocTesting.newDoc("I4", file).setAuthorLogin(null).setEffort(10L));

    Facets facets = new Facets(underTest.search(newQueryBuilder().build(), new SearchOptions().addFacets(asList("authors"))));
    assertThat(facets.getNames()).containsOnly("authors", FACET_MODE_EFFORT);
    assertThat(facets.get("authors")).containsOnly(entry("steph", 10L), entry("simon", 20L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 40L));
  }

  @Test
  public void facet_on_created_at() {
    SearchOptions searchOptions = fixtureForCreatedAtFacet();

    Builder query = newQueryBuilder().createdBefore(DateUtils.parseDateTime("2016-01-01T00:00:00+0100"));
    Map<String, Long> createdAt = new Facets(underTest.search(query.build(), searchOptions)).get("createdAt");
    assertThat(createdAt).containsOnly(
      entry("2011-01-01T00:00:00+0000", 10L),
      entry("2012-01-01T00:00:00+0000", 0L),
      entry("2013-01-01T00:00:00+0000", 0L),
      entry("2014-01-01T00:00:00+0000", 50L),
      entry("2015-01-01T00:00:00+0000", 10L));
  }

  @Test
  public void deprecated_debt_facets() {
    OrganizationDto organizationDto = newOrganizationDto();
    ComponentDto project = ComponentTesting.newPrivateProjectDto(organizationDto, "ABCD");
    ComponentDto project2 = ComponentTesting.newPrivateProjectDto(organizationDto, "EFGH");

    indexIssues(
      IssueDocTesting.newDoc("I1", ComponentTesting.newFileDto(project, null)).setEffort(10L),
      IssueDocTesting.newDoc("I2", ComponentTesting.newFileDto(project, null)).setEffort(10L),
      IssueDocTesting.newDoc("I3", ComponentTesting.newFileDto(project2, null)).setEffort(10L));

    Facets facets = new Facets(underTest.search(IssueQuery.builder().facetMode(DEPRECATED_FACET_MODE_DEBT).build(),
      new SearchOptions().addFacets(asList("projectUuids"))));
    assertThat(facets.getNames()).containsOnly("projectUuids", FACET_MODE_EFFORT);
    assertThat(facets.get("projectUuids")).containsOnly(entry("ABCD", 20L), entry("EFGH", 10L));
    assertThat(facets.get(FACET_MODE_EFFORT)).containsOnly(entry("total", 30L));
  }

  private SearchOptions fixtureForCreatedAtFacet() {
    ComponentDto project = ComponentTesting.newPrivateProjectDto(newOrganizationDto());
    ComponentDto file = ComponentTesting.newFileDto(project, null);

    IssueDoc issue0 = IssueDocTesting.newDoc("ISSUE0", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2011-04-25T01:05:13+0100"));
    IssueDoc issue1 = IssueDocTesting.newDoc("I1", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2014-09-01T12:34:56+0100"));
    IssueDoc issue2 = IssueDocTesting.newDoc("I2", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2014-09-01T23:46:00+0100"));
    IssueDoc issue3 = IssueDocTesting.newDoc("I3", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2014-09-02T12:34:56+0100"));
    IssueDoc issue4 = IssueDocTesting.newDoc("I4", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2014-09-05T12:34:56+0100"));
    IssueDoc issue5 = IssueDocTesting.newDoc("I5", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2014-09-20T12:34:56+0100"));
    IssueDoc issue6 = IssueDocTesting.newDoc("I6", file).setEffort(10L).setFuncCreationDate(DateUtils.parseDateTime("2015-01-18T12:34:56+0100"));

    indexIssues(issue0, issue1, issue2, issue3, issue4, issue5, issue6);

    return new SearchOptions().addFacets("createdAt");
  }

  private void indexIssues(IssueDoc... issues) {
    issueIndexer.index(asList(issues).iterator());
    for (IssueDoc issue : issues) {
      addIssueAuthorization(issue.projectUuid());
    }
  }

  private void addIssueAuthorization(String projectUuid) {
    PermissionIndexerDao.Dto access = new PermissionIndexerDao.Dto(projectUuid, Qualifiers.PROJECT);
    access.allowAnyone();
    authorizationIndexerTester.allow(access);
  }

  private Builder newQueryBuilder() {
    return IssueQuery.builder().facetMode(FACET_MODE_EFFORT);
  }
}
