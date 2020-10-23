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

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Optional;
import java.util.List;
import java.util.Map;
import javax.annotation.Nullable;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDao;
import org.sonar.db.component.ComponentDto;
import org.sonarqube.ws.WsDuplications;
import org.sonarqube.ws.WsDuplications.Block;
import org.sonarqube.ws.WsDuplications.ShowResponse;

import static com.google.common.collect.Maps.newHashMap;
import static org.sonar.core.util.Protobuf.setNullable;

public class ShowResponseBuilder {

  private final ComponentDao componentDao;

  public ShowResponseBuilder(DbClient dbClient) {
    this.componentDao = dbClient.componentDao();
  }

  @VisibleForTesting
  ShowResponseBuilder(ComponentDao componentDao) {
    this.componentDao = componentDao;
  }

  ShowResponse build(List<DuplicationsParser.Block> blocks, DbSession session) {
    ShowResponse.Builder response = ShowResponse.newBuilder();
    Map<String, String> refByComponentKey = newHashMap();
    blocks.stream()
      .map(block -> toWsDuplication(block, refByComponentKey))
      .forEach(response::addDuplications);

    writeFiles(response, refByComponentKey, session);

    return response.build();
  }

  private static WsDuplications.Duplication.Builder toWsDuplication(DuplicationsParser.Block block, Map<String, String> refByComponentKey) {
    WsDuplications.Duplication.Builder wsDuplication = WsDuplications.Duplication.newBuilder();
    block.getDuplications().stream()
      .map(d -> toWsBlock(refByComponentKey, d))
      .forEach(wsDuplication::addBlocks);

    return wsDuplication;
  }

  private static Block.Builder toWsBlock(Map<String, String> refByComponentKey, DuplicationsParser.Duplication duplication) {
    String ref = null;
    ComponentDto componentDto = duplication.file();
    if (componentDto != null) {
      String componentKey = componentDto.getDbKey();
      ref = refByComponentKey.get(componentKey);
      if (ref == null) {
        ref = Integer.toString(refByComponentKey.size() + 1);
        refByComponentKey.put(componentKey, ref);
      }
    }

    Block.Builder block = Block.newBuilder();
    block.setFrom(duplication.from());
    block.setSize(duplication.size());
    setNullable(ref, block::setRef);

    return block;
  }

  private static WsDuplications.File toWsFile(ComponentDto file, @Nullable ComponentDto project, @Nullable ComponentDto subProject) {
    WsDuplications.File.Builder wsFile = WsDuplications.File.newBuilder();
    wsFile.setKey(file.getDbKey());
    wsFile.setUuid(file.uuid());
    wsFile.setName(file.longName());

    if (project != null) {
      wsFile.setProject(project.getDbKey());
      wsFile.setProjectUuid(project.uuid());
      wsFile.setProjectName(project.longName());

      // Do not return sub project if sub project and project are the same
      boolean displaySubProject = subProject != null && !subProject.uuid().equals(project.uuid());
      if (displaySubProject) {
        wsFile.setSubProject(subProject.getDbKey());
        wsFile.setSubProjectUuid(subProject.uuid());
        wsFile.setSubProjectName(subProject.longName());
      }
    }

    return wsFile.build();
  }

  private void writeFiles(ShowResponse.Builder response, Map<String, String> refByComponentKey, DbSession session) {
    Map<String, ComponentDto> projectsByUuid = newHashMap();
    Map<String, ComponentDto> parentModulesByUuid = newHashMap();
    Map<String, WsDuplications.File> filesByRef = response.getMutableFiles();

    for (Map.Entry<String, String> entry : refByComponentKey.entrySet()) {
      String componentKey = entry.getKey();
      String ref = entry.getValue();
      Optional<ComponentDto> fileOptional = componentDao.selectByKey(session, componentKey);
      if (fileOptional.isPresent()) {
        ComponentDto file = fileOptional.get();

        ComponentDto project = getProject(file.projectUuid(), projectsByUuid, session);
        ComponentDto parentModule = getParentProject(file.getRootUuid(), parentModulesByUuid, session);
        filesByRef.put(ref, toWsFile(file, project, parentModule));
      }
    }
  }

  private ComponentDto getProject(String projectUuid, Map<String, ComponentDto> projectsByUuid, DbSession session) {
    ComponentDto project = projectsByUuid.get(projectUuid);
    if (project == null) {
      Optional<ComponentDto> projectOptional = componentDao.selectByUuid(session, projectUuid);
      if (projectOptional.isPresent()) {
        project = projectOptional.get();
        projectsByUuid.put(project.uuid(), project);
      }
    }
    return project;
  }

  private ComponentDto getParentProject(String rootUuid, Map<String, ComponentDto> subProjectsByUuid, DbSession session) {
    ComponentDto project = subProjectsByUuid.get(rootUuid);
    if (project == null) {
      Optional<ComponentDto> projectOptional = componentDao.selectByUuid(session, rootUuid);
      if (projectOptional.isPresent()) {
        project = projectOptional.get();
        subProjectsByUuid.put(project.uuid(), project);
      }
    }
    return project;
  }

}
