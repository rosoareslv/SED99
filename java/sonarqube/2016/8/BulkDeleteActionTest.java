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
package org.sonar.server.project.ws;

import java.util.Arrays;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.sonar.api.config.Settings;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.resources.ResourceType;
import org.sonar.api.resources.ResourceTypes;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.utils.System2;
import org.sonar.api.web.UserRole;
import org.sonar.core.permission.GlobalPermissions;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTesting;
import org.sonar.db.component.SnapshotDto;
import org.sonar.db.component.SnapshotTesting;
import org.sonar.db.issue.IssueDto;
import org.sonar.db.rule.RuleDto;
import org.sonar.db.rule.RuleTesting;
import org.sonar.server.component.ComponentCleanerService;
import org.sonar.server.component.ComponentFinder;
import org.sonar.server.es.EsTester;
import org.sonar.server.exceptions.ForbiddenException;
import org.sonar.server.issue.IssueTesting;
import org.sonar.server.issue.index.IssueAuthorizationDoc;
import org.sonar.server.issue.index.IssueAuthorizationIndexer;
import org.sonar.server.issue.index.IssueIndexDefinition;
import org.sonar.server.issue.index.IssueIndexer;
import org.sonar.server.test.index.TestDoc;
import org.sonar.server.test.index.TestIndexDefinition;
import org.sonar.server.test.index.TestIndexer;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.ws.WsTester;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.sonar.server.issue.index.IssueIndexDefinition.FIELD_AUTHORIZATION_PROJECT_UUID;
import static org.sonar.server.issue.index.IssueIndexDefinition.TYPE_AUTHORIZATION;
import static org.sonar.server.issue.index.IssueIndexDefinition.TYPE_ISSUE;
import static org.sonar.server.project.ws.BulkDeleteAction.PARAM_IDS;
import static org.sonar.server.project.ws.BulkDeleteAction.PARAM_KEYS;

public class BulkDeleteActionTest {

  private static final String ACTION = "bulk_delete";

  @Rule
  public DbTester db = DbTester.create(System2.INSTANCE);

  @Rule
  public EsTester es = new EsTester(new IssueIndexDefinition(new Settings()),
    new TestIndexDefinition(new Settings()));

  @Rule
  public UserSessionRule userSessionRule = UserSessionRule.standalone();

  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  WsTester ws;
  DbClient dbClient = db.getDbClient();
  final DbSession dbSession = db.getSession();
  ResourceType resourceType;

  @Before
  public void setUp() {
    resourceType = mock(ResourceType.class);
    when(resourceType.getBooleanProperty(anyString())).thenReturn(true);
    ResourceTypes mockResourceTypes = mock(ResourceTypes.class);
    when(mockResourceTypes.get(anyString())).thenReturn(resourceType);
    ws = new WsTester(new ProjectsWs(
      new BulkDeleteAction(
        new ComponentCleanerService(dbClient,
          new IssueAuthorizationIndexer(dbClient, es.client()),
          new IssueIndexer(dbClient, es.client()),
          new TestIndexer(dbClient, es.client()), mockResourceTypes, new ComponentFinder(dbClient)),
        dbClient,
        userSessionRule)));
    userSessionRule.setGlobalPermissions(GlobalPermissions.SYSTEM_ADMIN);
  }

  @Test
  public void delete_projects_and_data_in_db_by_uuids() throws Exception {
    long snapshotId1 = insertNewProjectInDbAndReturnSnapshotId(1);
    long snapshotId2 = insertNewProjectInDbAndReturnSnapshotId(2);
    long snapshotId3 = insertNewProjectInDbAndReturnSnapshotId(3);
    long snapshotId4 = insertNewProjectInDbAndReturnSnapshotId(4);

    ws.newPostRequest("api/projects", ACTION)
      .setParam(PARAM_IDS, "project-uuid-1, project-uuid-3, project-uuid-4").execute();
    dbSession.commit();

    assertThat(dbClient.componentDao().selectByUuids(dbSession, Arrays.asList("project-uuid-1", "project-uuid-3", "project-uuid-4"))).isEmpty();
    assertThat(dbClient.componentDao().selectOrFailByUuid(dbSession, "project-uuid-2")).isNotNull();
    assertThat(dbClient.snapshotDao().selectById(dbSession, snapshotId1)).isNull();
    assertThat(dbClient.snapshotDao().selectById(dbSession, snapshotId3)).isNull();
    assertThat(dbClient.snapshotDao().selectById(dbSession, snapshotId4)).isNull();
    assertThat(dbClient.snapshotDao().selectById(dbSession, snapshotId2)).isNotNull();
    assertThat(dbClient.issueDao().selectByKeys(dbSession, Arrays.asList("issue-key-1", "issue-key-3", "issue-key-4"))).isEmpty();
    assertThat(dbClient.issueDao().selectOrFailByKey(dbSession, "issue-key-2")).isNotNull();
  }

  @Test
  public void delete_projects_and_data_in_db_by_keys() throws Exception {
    insertNewProjectInDbAndReturnSnapshotId(1);
    insertNewProjectInDbAndReturnSnapshotId(2);
    insertNewProjectInDbAndReturnSnapshotId(3);
    insertNewProjectInDbAndReturnSnapshotId(4);

    ws.newPostRequest("api/projects", ACTION)
      .setParam(PARAM_KEYS, "project-key-1, project-key-3, project-key-4").execute();
    dbSession.commit();

    assertThat(dbClient.componentDao().selectByUuids(dbSession, Arrays.asList("project-uuid-1", "project-uuid-3", "project-uuid-4"))).isEmpty();
    assertThat(dbClient.componentDao().selectOrFailByUuid(dbSession, "project-uuid-2")).isNotNull();
  }

  @Test
  public void delete_documents_indexes() throws Exception {
    insertNewProjectInIndexes(1);
    insertNewProjectInIndexes(2);
    insertNewProjectInIndexes(3);
    insertNewProjectInIndexes(4);

    ws.newPostRequest("api/projects", ACTION)
      .setParam(PARAM_KEYS, "project-key-1, project-key-3, project-key-4").execute();

    String remainingProjectUuid = "project-uuid-2";
    assertThat(es.getDocumentFieldValues(IssueIndexDefinition.INDEX, TYPE_ISSUE, IssueIndexDefinition.FIELD_ISSUE_PROJECT_UUID))
      .containsOnly(remainingProjectUuid);
    assertThat(es.getDocumentFieldValues(IssueIndexDefinition.INDEX, TYPE_AUTHORIZATION, FIELD_AUTHORIZATION_PROJECT_UUID))
      .containsOnly(remainingProjectUuid);
    assertThat(es.getDocumentFieldValues(TestIndexDefinition.INDEX, TestIndexDefinition.TYPE, TestIndexDefinition.FIELD_PROJECT_UUID))
      .containsOnly(remainingProjectUuid);
  }

  @Test
  public void web_service_returns_204() throws Exception {
    insertNewProjectInDbAndReturnSnapshotId(1);

    WsTester.Result result = ws.newPostRequest("api/projects", ACTION).setParam(PARAM_IDS, "project-uuid-1").execute();

    result.assertNoContent();
  }

  @Test
  public void fail_if_insufficient_privileges() throws Exception {
    userSessionRule.setGlobalPermissions(UserRole.CODEVIEWER, UserRole.ISSUE_ADMIN, UserRole.USER);
    expectedException.expect(ForbiddenException.class);

    ws.newPostRequest("api/projects", ACTION).setParam(PARAM_IDS, "whatever-the-uuid").execute();
  }

  @Test
  public void fail_if_scope_is_not_project() throws Exception {
    expectedException.expect(IllegalArgumentException.class);
    dbClient.componentDao().insert(dbSession, ComponentTesting.newFileDto(ComponentTesting.newProjectDto(), null, "file-uuid"));
    dbSession.commit();

    ws.newPostRequest("api/projects", ACTION).setParam(PARAM_IDS, "file-uuid").execute();
  }

  @Test
  public void fail_if_qualifier_is_not_deletable() throws Exception {
    expectedException.expect(IllegalArgumentException.class);
    dbClient.componentDao().insert(dbSession, ComponentTesting.newProjectDto("project-uuid").setQualifier(Qualifiers.FILE));
    dbSession.commit();
    when(resourceType.getBooleanProperty(anyString())).thenReturn(false);

    ws.newPostRequest("api/projects", ACTION).setParam(PARAM_IDS, "project-uuid").execute();
  }

  private long insertNewProjectInDbAndReturnSnapshotId(int id) {
    String suffix = String.valueOf(id);
    ComponentDto project = ComponentTesting
      .newProjectDto("project-uuid-" + suffix)
      .setKey("project-key-" + suffix);
    RuleDto rule = RuleTesting.newDto(RuleKey.of("sonarqube", "rule-" + suffix));
    dbClient.ruleDao().insert(dbSession, rule);
    IssueDto issue = IssueTesting.newDto(rule, project, project).setKee("issue-key-" + suffix);
    dbClient.componentDao().insert(dbSession, project);
    SnapshotDto snapshot = dbClient.snapshotDao().insert(dbSession, SnapshotTesting.newAnalysis(project));
    dbClient.issueDao().insert(dbSession, issue);
    dbSession.commit();

    return snapshot.getId();
  }

  private void insertNewProjectInIndexes(int id) throws Exception {
    String suffix = String.valueOf(id);
    ComponentDto project = ComponentTesting
      .newProjectDto("project-uuid-" + suffix)
      .setKey("project-key-" + suffix);
    dbClient.componentDao().insert(dbSession, project);
    dbSession.commit();

    es.putDocuments(IssueIndexDefinition.INDEX, TYPE_ISSUE, IssueTesting.newDoc("issue-key-" + suffix, project));
    es.putDocuments(IssueIndexDefinition.INDEX, TYPE_AUTHORIZATION, new IssueAuthorizationDoc().setProjectUuid(project.uuid()));

    TestDoc testDoc = new TestDoc().setUuid("test-uuid-" + suffix).setProjectUuid(project.uuid()).setFileUuid(project.uuid());
    es.putDocuments(TestIndexDefinition.INDEX, TestIndexDefinition.TYPE, testDoc);
  }
}
