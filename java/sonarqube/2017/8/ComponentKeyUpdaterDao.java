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
package org.sonar.db.component;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.lang.StringUtils;
import org.sonar.api.resources.Qualifiers;
import org.sonar.db.Dao;
import org.sonar.db.DbSession;

import static com.google.common.base.Preconditions.checkArgument;
import static org.sonar.core.component.ComponentKeys.checkModuleKey;
import static org.sonar.core.component.ComponentKeys.isValidModuleKey;

/**
 * Class used to rename the key of a project and its resources.
 *
 * @since 3.2
 */
public class ComponentKeyUpdaterDao implements Dao {

  private static final Set<String> PROJECT_OR_MODULE_QUALIFIERS = ImmutableSet.of(Qualifiers.PROJECT, Qualifiers.MODULE);

  public void updateKey(DbSession dbSession, String projectOrModuleUuid, String newKey) {
    ComponentKeyUpdaterMapper mapper = dbSession.getMapper(ComponentKeyUpdaterMapper.class);
    if (mapper.countResourceByKey(newKey) > 0) {
      throw new IllegalArgumentException("Impossible to update key: a component with key \"" + newKey + "\" already exists.");
    }

    // must SELECT first everything
    ResourceDto project = mapper.selectProject(projectOrModuleUuid);
    String projectOldKey = project.getKey();
    List<ResourceDto> resources = mapper.selectProjectResources(projectOrModuleUuid);
    resources.add(project);

    // and then proceed with the batch UPDATE at once
    runBatchUpdateForAllResources(resources, projectOldKey, newKey, mapper);
  }

  public static void checkIsProjectOrModule(ComponentDto component) {
    checkArgument(PROJECT_OR_MODULE_QUALIFIERS.contains(component.qualifier()), "Component updated must be a module or a key");
  }

  /**
   *
   * @return a map with currentKey/newKey is a bulk update was executed
   */
  public Map<String, String> simulateBulkUpdateKey(DbSession dbSession, String projectUuid, String stringToReplace, String replacementString) {
    return collectAllModules(projectUuid, stringToReplace, mapper(dbSession))
      .stream()
      .collect(Collectors.toMap(
        ResourceDto::getKey,
        component -> {
          String newKey = computeNewKey(component.getKey(), stringToReplace, replacementString);
          checkModuleKey(newKey);
          return newKey;
        }));
  }

  /**
   * @return a map with the component key as key, and boolean as true if key already exists in db
   */
  public Map<String, Boolean> checkComponentKeys(DbSession dbSession, List<String> newComponentKeys) {
    return newComponentKeys.stream().collect(Collectors.toMap(Function.identity(), key -> mapper(dbSession).countResourceByKey(key) > 0));
  }

  @VisibleForTesting
  static String computeNewKey(String key, String stringToReplace, String replacementString) {
    return key.replace(stringToReplace, replacementString);
  }

  public void bulkUpdateKey(DbSession session, String projectUuid, String stringToReplace, String replacementString) {
    ComponentKeyUpdaterMapper mapper = session.getMapper(ComponentKeyUpdaterMapper.class);
    // must SELECT first everything
    Set<ResourceDto> modules = collectAllModules(projectUuid, stringToReplace, mapper);
    checkNewNameOfAllModules(modules, stringToReplace, replacementString, mapper);
    Map<ResourceDto, List<ResourceDto>> allResourcesByModuleMap = Maps.newHashMap();
    for (ResourceDto module : modules) {
      allResourcesByModuleMap.put(module, mapper.selectProjectResources(module.getUuid()));
    }

    // and then proceed with the batch UPDATE at once
    for (ResourceDto module : modules) {
      String oldModuleKey = module.getKey();
      String newModuleKey = computeNewKey(module.getKey(), stringToReplace, replacementString);
      Collection<ResourceDto> resources = Lists.newArrayList(module);
      resources.addAll(allResourcesByModuleMap.get(module));
      runBatchUpdateForAllResources(resources, oldModuleKey, newModuleKey, mapper);
    }
  }

  private static void runBatchUpdateForAllResources(Collection<ResourceDto> resources, String oldKey, String newKey, ComponentKeyUpdaterMapper mapper) {
    for (ResourceDto resource : resources) {
      String oldResourceKey = resource.getKey();
      String newResourceKey = newKey + oldResourceKey.substring(oldKey.length(), oldResourceKey.length());
      resource.setKey(newResourceKey);
      String oldResourceDeprecatedKey = resource.getDeprecatedKey();
      if (StringUtils.isNotBlank(oldResourceDeprecatedKey)) {
        String newResourceDeprecatedKey = newKey + oldResourceDeprecatedKey.substring(oldKey.length(), oldResourceDeprecatedKey.length());
        resource.setDeprecatedKey(newResourceDeprecatedKey);
      }
      mapper.update(resource);
    }
  }

  private static Set<ResourceDto> collectAllModules(String projectUuid, String stringToReplace, ComponentKeyUpdaterMapper mapper) {
    ResourceDto project = mapper.selectProject(projectUuid);
    Set<ResourceDto> modules = new HashSet<>();
    if (project.getKey().contains(stringToReplace)) {
      modules.add(project);
    }
    for (ResourceDto submodule : mapper.selectDescendantProjects(projectUuid)) {
      modules.addAll(collectAllModules(submodule.getUuid(), stringToReplace, mapper));
    }
    return modules;
  }

  private static void checkNewNameOfAllModules(Set<ResourceDto> modules, String stringToReplace, String replacementString, ComponentKeyUpdaterMapper mapper) {
    for (ResourceDto module : modules) {
      String newKey = computeNewKey(module.getKey(), stringToReplace, replacementString);
      checkArgument(isValidModuleKey(newKey), "Malformed key for '%s'. Allowed characters are alphanumeric, '-', '_', '.' and ':', with at least one non-digit.", newKey);
      if (mapper.countResourceByKey(newKey) > 0) {
        throw new IllegalArgumentException("Impossible to update key: a component with key \"" + newKey + "\" already exists.");
      }
    }
  }

  private static ComponentKeyUpdaterMapper mapper(DbSession dbSession) {
    return dbSession.getMapper(ComponentKeyUpdaterMapper.class);
  }
}
