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
package org.sonar.db.permission;

import java.util.Collection;
import java.util.List;
import javax.annotation.Nullable;
import org.apache.ibatis.annotations.Param;
import org.apache.ibatis.session.RowBounds;

public interface UserPermissionMapper {

  List<UserPermissionDto> selectByQuery(@Param("organizationUuid") String organizationUuid,
    @Param("query") PermissionQuery query, @Nullable @Param("userLogins") Collection<String> userLogins, RowBounds rowBounds);

  /**
   * Count the number of distinct users returned by {@link #selectByQuery(String, PermissionQuery, Collection, RowBounds)}
   * {@link PermissionQuery#getPageOffset()} and {@link PermissionQuery#getPageSize()} are ignored.
   *
   * @param useNull must always be null. It is needed for using the sql of 
   * {@link #selectByQuery(String, PermissionQuery, Collection, RowBounds)}
   */
  int countUsersByQuery(@Param("organizationUuid") String organizationUuid, @Param("query") PermissionQuery query,
    @Nullable @Param("userLogins") Collection<String> useNull);

  /**
   * Count the number of users per permission for a given list of projects.
   * @param projectIds a non-null and non-empty list of project ids
   */
  List<CountPerProjectPermission> countUsersByProjectPermission(@Param("projectIds") List<Long> projectIds);

  void insert(UserPermissionDto dto);

  int countRowsByRootComponentId(@Param("rootComponentId") long projectId);

  void deleteGlobalPermission(@Param("userId") long userId, @Param("permission") String permission,
    @Param("organizationUuid") String organizationUuid);

  void deleteProjectPermission(@Param("userId") long userId, @Param("permission") String permission,
    @Param("projectId") long projectId);

  void deleteProjectPermissions(@Param("projectId") long projectId);

  List<String> selectGlobalPermissionsOfUser(@Param("userId") long userId, @Param("organizationUuid") String organizationUuid);

  List<String> selectProjectPermissionsOfUser(@Param("userId") long userId, @Param("projectId") long projectId);

  void deleteByOrganization(@Param("organizationUuid") String organizationUuid);
}
