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
package org.sonar.server.duplication.ws;

import java.io.StringWriter;
import java.util.Collections;
import java.util.List;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.utils.text.JsonWriter;
import org.sonar.core.util.ProtobufJsonFormat;
import org.sonar.db.DbSession;
import org.sonar.db.DbTester;
import org.sonar.db.component.ComponentDao;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.ComponentTesting;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.db.organization.OrganizationTesting;
import org.sonar.test.JsonAssert;

import static com.google.common.collect.Lists.newArrayList;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.sonar.db.component.ComponentTesting.newPrivateProjectDto;

public class ShowResponseBuilderTest {

  @Rule
  public DbTester db = DbTester.create();
  private DbSession dbSession = db.getSession();
  private ComponentDao componentDao = spy(db.getDbClient().componentDao());

  private ComponentDto project;
  private OrganizationDto organization = OrganizationTesting.newOrganizationDto();

  private ShowResponseBuilder underTest = new ShowResponseBuilder(componentDao);

  @Before
  public void setUp() {
    project = newPrivateProjectDto(organization)
      .setName("SonarQube")
      .setLongName("SonarQube")
      .setDbKey("org.codehaus.sonar:sonar");
    db.components().insertComponent(project);
  }

  @Test
  public void write_duplications() {
    String key1 = "org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java";
    ComponentDto file1 = ComponentTesting.newFileDto(project, null).setDbKey(key1).setLongName("PropertyDeleteQuery").setRootUuid("uuid_5");
    String key2 = "org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyUpdateQuery.java";
    ComponentDto file2 = ComponentTesting.newFileDto(project, null).setQualifier("FIL").setDbKey(key2).setLongName("PropertyUpdateQuery").setRootUuid("uuid_5");
    ComponentDto project2 = db.components().insertPrivateProject(organization, p -> p.setUuid("uuid_5").setDbKey("org.codehaus.sonar:sonar-ws-client")
      .setLongName("SonarQube :: Web Service Client"));

    db.components().insertComponent(file1);
    db.components().insertComponent(file2);

    List<DuplicationsParser.Block> blocks = newArrayList();
    blocks.add(new DuplicationsParser.Block(newArrayList(
      new DuplicationsParser.Duplication(file1, 57, 12),
      new DuplicationsParser.Duplication(file2, 73, 12)
    )));

    test(blocks,
      "{\n" +
        "  \"duplications\": [\n" +
        "    {\n" +
        "      \"blocks\": [\n" +
        "        {\n" +
        "          \"from\": 57, \"size\": 12, \"_ref\": \"1\"\n" +
        "        },\n" +
        "        {\n" +
        "          \"from\": 73, \"size\": 12, \"_ref\": \"2\"\n" +
        "        }\n" +
        "      ]\n" +
        "    }," +
        "  ],\n" +
        "  \"files\": {\n" +
        "    \"1\": {\n" +
        "      \"key\": \"org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java\",\n" +
        "      \"name\": \"PropertyDeleteQuery\",\n" +
        "      \"project\": \"org.codehaus.sonar:sonar\",\n" +
        "      \"projectName\": \"SonarQube\",\n" +
        "      \"subProject\": \"org.codehaus.sonar:sonar-ws-client\",\n" +
        "      \"subProjectName\": \"SonarQube :: Web Service Client\"\n" +
        "    },\n" +
        "    \"2\": {\n" +
        "      \"key\": \"org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyUpdateQuery.java\",\n" +
        "      \"name\": \"PropertyUpdateQuery\",\n" +
        "      \"project\": \"org.codehaus.sonar:sonar\",\n" +
        "      \"projectName\": \"SonarQube\",\n" +
        "      \"subProject\": \"org.codehaus.sonar:sonar-ws-client\",\n" +
        "      \"subProjectName\": \"SonarQube :: Web Service Client\"\n" +
        "    }\n" +
        "  }" +
        "}");

    verify(componentDao, times(2)).selectByKey(eq(dbSession), anyString());
    // Verify call to dao is cached when searching for project / sub project
    verify(componentDao, times(1)).selectByUuid(eq(dbSession), eq(project.uuid()));
    verify(componentDao, times(1)).selectByUuid(eq(dbSession), eq("uuid_5"));
  }

  @Test
  public void write_duplications_without_sub_project() {
    String key1 = "org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java";
    ComponentDto file1 = ComponentTesting.newFileDto(project, null).setId(10L).setDbKey(key1).setLongName("PropertyDeleteQuery");
    String key2 = "org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyUpdateQuery.java";
    ComponentDto file2 = ComponentTesting.newFileDto(project, null).setId(11L).setDbKey(key2).setLongName("PropertyUpdateQuery");

    db.components().insertComponent(file1);
    db.components().insertComponent(file2);

    List<DuplicationsParser.Block> blocks = newArrayList();
    blocks.add(new DuplicationsParser.Block(newArrayList(
      new DuplicationsParser.Duplication(file1, 57, 12),
      new DuplicationsParser.Duplication(file2, 73, 12)
    )));

    test(blocks,
      "{\n" +
        "  \"duplications\": [\n" +
        "    {\n" +
        "      \"blocks\": [\n" +
        "        {\n" +
        "          \"from\": 57, \"size\": 12, \"_ref\": \"1\"\n" +
        "        },\n" +
        "        {\n" +
        "          \"from\": 73, \"size\": 12, \"_ref\": \"2\"\n" +
        "        }\n" +
        "      ]\n" +
        "    }," +
        "  ],\n" +
        "  \"files\": {\n" +
        "    \"1\": {\n" +
        "      \"key\": \"org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java\",\n" +
        "      \"name\": \"PropertyDeleteQuery\",\n" +
        "      \"project\": \"org.codehaus.sonar:sonar\",\n" +
        "      \"projectName\": \"SonarQube\"\n" +
        "    },\n" +
        "    \"2\": {\n" +
        "      \"key\": \"org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyUpdateQuery.java\",\n" +
        "      \"name\": \"PropertyUpdateQuery\",\n" +
        "      \"project\": \"org.codehaus.sonar:sonar\",\n" +
        "      \"projectName\": \"SonarQube\"\n" +
        "    }\n" +
        "  }" +
        "}");
  }

  @Test
  public void write_duplications_with_a_removed_component() {
    String key1 = "org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java";
    ComponentDto file1 = ComponentTesting.newFileDto(project, null).setId(10L).setDbKey(key1).setLongName("PropertyDeleteQuery");
    db.components().insertComponent(file1);

    List<DuplicationsParser.Block> blocks = newArrayList();

    blocks.add(new DuplicationsParser.Block(newArrayList(
      new DuplicationsParser.Duplication(file1, 57, 12),
      // Duplication on a removed file
      new DuplicationsParser.Duplication(null, 73, 12)
    )));

    test(blocks,
      "{\n" +
        "  \"duplications\": [\n" +
        "    {\n" +
        "      \"blocks\": [\n" +
        "        {\n" +
        "          \"from\": 57, \"size\": 12, \"_ref\": \"1\"\n" +
        "        },\n" +
        "        {\n" +
        "          \"from\": 73, \"size\": 12\n" +
        "        }\n" +
        "      ]\n" +
        "    }," +
        "  ],\n" +
        "  \"files\": {\n" +
        "    \"1\": {\n" +
        "      \"key\": \"org.codehaus.sonar:sonar-ws-client:src/main/java/org/sonar/wsclient/services/PropertyDeleteQuery.java\",\n" +
        "      \"name\": \"PropertyDeleteQuery\",\n" +
        "      \"project\": \"org.codehaus.sonar:sonar\",\n" +
        "      \"projectName\": \"SonarQube\"\n" +
        "    }\n" +
        "  }" +
        "}");
  }

  @Test
  public void write_nothing_when_no_data() {
    test(Collections.emptyList(), "{\"duplications\": [], \"files\": {}}");
  }

  private void test(List<DuplicationsParser.Block> blocks, String expected) {
    StringWriter output = new StringWriter();
    JsonWriter jsonWriter = JsonWriter.of(output);
    ProtobufJsonFormat.write(underTest.build(blocks, dbSession), jsonWriter);
    JsonAssert.assertJson(output.toString()).isSimilarTo(expected);
  }

}
