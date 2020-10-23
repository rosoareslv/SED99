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
package org.sonar.server.issue;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.common.base.Splitter;
import com.google.common.base.Strings;
import com.google.common.collect.Collections2;
import com.google.common.collect.Lists;
import java.time.Clock;
import java.time.OffsetDateTime;
import java.time.Period;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.annotation.CheckForNull;
import javax.annotation.Nullable;
import org.apache.commons.lang.BooleanUtils;
import org.sonar.api.resources.Qualifiers;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.server.ServerSide;
import org.sonar.api.web.UserRole;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.component.ComponentDto;
import org.sonar.db.component.SnapshotDto;
import org.sonar.db.organization.OrganizationDto;
import org.sonar.server.user.UserSession;

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.collect.Lists.newArrayList;
import static java.lang.String.format;
import static java.util.Collections.singleton;
import static java.util.Collections.singletonList;
import static org.sonar.api.utils.DateUtils.longToDate;
import static org.sonar.api.utils.DateUtils.parseDateOrDateTime;
import static org.sonar.api.utils.DateUtils.parseEndingDateOrDateTime;
import static org.sonar.api.utils.DateUtils.parseStartingDateOrDateTime;
import static org.sonar.core.util.stream.MoreCollectors.toHashSet;
import static org.sonar.core.util.stream.MoreCollectors.toList;
import static org.sonar.core.util.stream.MoreCollectors.toSet;
import static org.sonar.core.util.stream.MoreCollectors.uniqueIndex;
import static org.sonar.server.ws.WsUtils.checkRequest;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_COMPONENTS;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_COMPONENT_KEYS;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_COMPONENT_ROOTS;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_COMPONENT_UUIDS;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_CREATED_AFTER;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_CREATED_IN_LAST;
import static org.sonarqube.ws.client.issue.IssuesWsParameters.PARAM_SINCE_LEAK_PERIOD;

/**
 * This component is used to create an IssueQuery, in order to transform the component and component roots keys into uuid.
 */
@ServerSide
public class IssueQueryFactory {

  public static final String LOGIN_MYSELF = "__me__";

  private static final String UNKNOWN = "<UNKNOWN>";

  private static final ComponentDto UNKNOWN_COMPONENT = new ComponentDto().setUuid(UNKNOWN).setProjectUuid(UNKNOWN);

  private final DbClient dbClient;
  private final Clock clock;
  private final UserSession userSession;

  public IssueQueryFactory(DbClient dbClient, Clock clock, UserSession userSession) {
    this.dbClient = dbClient;
    this.clock = clock;
    this.userSession = userSession;
  }

  public IssueQuery create(SearchRequest request) {
    try (DbSession dbSession = dbClient.openSession(false)) {
      IssueQuery.Builder builder = IssueQuery.builder()
        .issueKeys(request.getIssues())
        .severities(request.getSeverities())
        .statuses(request.getStatuses())
        .resolutions(request.getResolutions())
        .resolved(request.getResolved())
        .rules(stringsToRules(request.getRules()))
        .assignees(buildAssignees(request.getAssignees()))
        .languages(request.getLanguages())
        .tags(request.getTags())
        .types(request.getTypes())
        .assigned(request.getAssigned())
        .createdAt(parseDateOrDateTime(request.getCreatedAt()))
        .createdBefore(parseEndingDateOrDateTime(request.getCreatedBefore()))
        .facetMode(request.getFacetMode())
        .organizationUuid(convertOrganizationKeyToUuid(dbSession, request.getOrganization()));

      List<ComponentDto> allComponents = new ArrayList<>();
      boolean effectiveOnComponentOnly = mergeDeprecatedComponentParameters(dbSession, request, allComponents);
      addComponentParameters(builder, dbSession, effectiveOnComponentOnly, allComponents, request);

      builder.createdAfter(buildCreatedAfterFromRequest(dbSession, request, allComponents));
      String sort = request.getSort();
      if (!Strings.isNullOrEmpty(sort)) {
        builder.sort(sort);
        builder.asc(request.getAsc());
      }
      return builder.build();
    }
  }

  @CheckForNull
  private Date buildCreatedAfterFromDates(@Nullable Date createdAfter, @Nullable String createdInLast) {
    checkArgument(createdAfter == null || createdInLast == null, format("Parameters %s and %s cannot be set simultaneously", PARAM_CREATED_AFTER, PARAM_CREATED_IN_LAST));

    Date actualCreatedAfter = createdAfter;
    if (createdInLast != null) {
      actualCreatedAfter = Date.from(
        OffsetDateTime.now(clock)
          .minus(Period.parse("P" + createdInLast.toUpperCase(Locale.ENGLISH)))
          .toInstant());
    }
    return actualCreatedAfter;
  }

  @CheckForNull
  private String convertOrganizationKeyToUuid(DbSession dbSession, @Nullable String organizationKey) {
    if (organizationKey == null) {
      return null;
    }
    Optional<OrganizationDto> organization = dbClient.organizationDao().selectByKey(dbSession, organizationKey);
    return organization.map(OrganizationDto::getUuid).orElse(UNKNOWN);
  }

  private Date buildCreatedAfterFromRequest(DbSession dbSession, SearchRequest request, List<ComponentDto> componentUuids) {
    Date createdAfter = parseStartingDateOrDateTime(request.getCreatedAfter());
    String createdInLast = request.getCreatedInLast();

    if (request.getSinceLeakPeriod() == null || !request.getSinceLeakPeriod()) {
      return buildCreatedAfterFromDates(createdAfter, createdInLast);
    }

    checkRequest(createdAfter == null, "Parameters '%s' and '%s' cannot be set simultaneously", PARAM_CREATED_AFTER, PARAM_SINCE_LEAK_PERIOD);
    checkArgument(componentUuids.size() == 1, "One and only one component must be provided when searching since leak period");
    ComponentDto component = componentUuids.iterator().next();
    Date createdAfterFromSnapshot = findCreatedAfterFromComponentUuid(dbSession, component);
    return buildCreatedAfterFromDates(createdAfterFromSnapshot, createdInLast);
  }

  @CheckForNull
  private Date findCreatedAfterFromComponentUuid(DbSession dbSession, ComponentDto component) {
    Optional<SnapshotDto> snapshot = dbClient.snapshotDao().selectLastAnalysisByComponentUuid(dbSession, component.uuid());
    return snapshot.map(s -> longToDate(s.getPeriodDate())).orElse(null);
  }

  private List<String> buildAssignees(@Nullable List<String> assigneesFromParams) {
    List<String> assignees = Lists.newArrayList();
    if (assigneesFromParams != null) {
      assignees.addAll(assigneesFromParams);
    }
    if (assignees.contains(LOGIN_MYSELF)) {
      String login = userSession.getLogin();
      if (login == null) {
        assignees.add(UNKNOWN);
      } else {
        assignees.add(login);
      }
    }
    return assignees;
  }

  private boolean mergeDeprecatedComponentParameters(DbSession session, SearchRequest request, List<ComponentDto> allComponents) {
    Boolean onComponentOnly = request.getOnComponentOnly();
    Collection<String> components = request.getComponents();
    Collection<String> componentUuids = request.getComponentUuids();
    Collection<String> componentKeys = request.getComponentKeys();
    Collection<String> componentRootUuids = request.getComponentRootUuids();
    Collection<String> componentRoots = request.getComponentRoots();
    String branch = request.getBranch();

    boolean effectiveOnComponentOnly = false;

    checkArgument(atMostOneNonNullElement(components, componentUuids, componentKeys, componentRootUuids, componentRoots),
      "At most one of the following parameters can be provided: %s, %s, %s, %s, %s",
      PARAM_COMPONENT_KEYS, PARAM_COMPONENT_UUIDS, PARAM_COMPONENTS, PARAM_COMPONENT_ROOTS, PARAM_COMPONENT_UUIDS);

    if (componentRootUuids != null) {
      allComponents.addAll(getComponentsFromUuids(session, componentRootUuids));
    } else if (componentRoots != null) {
      allComponents.addAll(getComponentsFromKeys(session, componentRoots, branch));
    } else if (components != null) {
      allComponents.addAll(getComponentsFromKeys(session, components, branch));
      effectiveOnComponentOnly = true;
    } else if (componentUuids != null) {
      allComponents.addAll(getComponentsFromUuids(session, componentUuids));
      effectiveOnComponentOnly = BooleanUtils.isTrue(onComponentOnly);
    } else if (componentKeys != null) {
      allComponents.addAll(getComponentsFromKeys(session, componentKeys, branch));
      effectiveOnComponentOnly = BooleanUtils.isTrue(onComponentOnly);
    }

    return effectiveOnComponentOnly;
  }

  private static boolean atMostOneNonNullElement(Object... objects) {
    return Arrays.stream(objects)
      .filter(Objects::nonNull)
      .count() <= 1;
  }

  private void addComponentParameters(IssueQuery.Builder builder, DbSession session, boolean onComponentOnly,
    List<ComponentDto> components, SearchRequest request) {

    builder.onComponentOnly(onComponentOnly);
    if (onComponentOnly) {
      builder.componentUuids(components.stream().map(ComponentDto::uuid).collect(toList()));
      setBranch(builder, components.get(0), request.getBranch());
      return;
    }

    builder.authors(request.getAuthors());
    List<String> projectUuids = request.getProjectUuids();
    List<String> projectKeys = request.getProjectKeys();
    checkArgument(projectUuids == null || projectKeys == null, "Parameters projects and projectUuids cannot be set simultaneously");
    if (projectUuids != null) {
      builder.projectUuids(projectUuids);
    } else if (projectKeys != null) {
      List<ComponentDto> projects = getComponentsFromKeys(session, projectKeys, request.getBranch());
      builder.projectUuids(projects.stream().map(IssueQueryFactory::toProjectUuid).collect(toList()));
      setBranch(builder, projects.get(0), request.getBranch());
    }
    builder.moduleUuids(request.getModuleUuids());
    builder.directories(request.getDirectories());
    builder.fileUuids(request.getFileUuids());

    addComponentsBasedOnQualifier(builder, session, components, request);
  }

  private void addComponentsBasedOnQualifier(IssueQuery.Builder builder, DbSession dbSession, List<ComponentDto> components, SearchRequest request) {
    if (components.isEmpty()) {
      return;
    }
    if (components.stream().map(ComponentDto::uuid).anyMatch(uuid -> uuid.equals(UNKNOWN))) {
      builder.componentUuids(singleton(UNKNOWN));
      return;
    }

    Set<String> qualifiers = components.stream().map(ComponentDto::qualifier).collect(toHashSet());
    checkArgument(qualifiers.size() == 1, "All components must have the same qualifier, found %s", String.join(",", qualifiers));

    setBranch(builder, components.get(0), request.getBranch());
    String qualifier = qualifiers.iterator().next();
    switch (qualifier) {
      case Qualifiers.VIEW:
      case Qualifiers.SUBVIEW:
        addViewsOrSubViews(builder, components);
        break;
      case Qualifiers.APP:
        addApplications(builder, dbSession, components, request);
        break;
      case Qualifiers.PROJECT:
        builder.projectUuids(components.stream().map(IssueQueryFactory::toProjectUuid).collect(toList()));
        break;
      case Qualifiers.MODULE:
        builder.moduleRootUuids(components.stream().map(ComponentDto::uuid).collect(toList()));
        break;
      case Qualifiers.DIRECTORY:
        addDirectories(builder, components);
        break;
      case Qualifiers.FILE:
      case Qualifiers.UNIT_TEST_FILE:
        builder.fileUuids(components.stream().map(ComponentDto::uuid).collect(toList()));
        break;
      default:
        throw new IllegalArgumentException("Unable to set search root context for components " + Joiner.on(',').join(components));
    }
  }

  private void addViewsOrSubViews(IssueQuery.Builder builder, Collection<ComponentDto> viewOrSubViewUuids) {
    List<String> filteredViewUuids = viewOrSubViewUuids.stream()
      .filter(uuid -> userSession.hasComponentPermission(UserRole.USER, uuid))
      .map(ComponentDto::uuid)
      .collect(Collectors.toList());
    if (filteredViewUuids.isEmpty()) {
      filteredViewUuids.add(UNKNOWN);
    }
    builder.viewUuids(filteredViewUuids);
  }

  private void addApplications(IssueQuery.Builder builder, DbSession dbSession, List<ComponentDto> applications, SearchRequest request) {
    Set<String> authorizedApplicationUuids = applications.stream()
      .filter(app -> userSession.hasComponentPermission(UserRole.USER, app))
      .map(ComponentDto::uuid)
      .collect(toSet());

    builder.viewUuids(authorizedApplicationUuids.isEmpty() ? singleton(UNKNOWN) : authorizedApplicationUuids);
    addCreatedAfterByProjects(builder, dbSession, request, authorizedApplicationUuids);
  }

  private void addCreatedAfterByProjects(IssueQuery.Builder builder, DbSession dbSession, SearchRequest request, Set<String> applicationUuids) {
    if (request.getSinceLeakPeriod() == null || !request.getSinceLeakPeriod()) {
      return;
    }

    Set<String> projectUuids = applicationUuids.stream()
      .flatMap(app -> dbClient.componentDao().selectProjectsFromView(dbSession, app, app).stream())
      .collect(toSet());

    Map<String, Date> leakByProjects = dbClient.snapshotDao().selectLastAnalysesByRootComponentUuids(dbSession, projectUuids)
      .stream()
      .filter(s -> s.getPeriodDate() != null)
      .collect(uniqueIndex(SnapshotDto::getComponentUuid, s -> longToDate(s.getPeriodDate())));
    builder.createdAfterByProjectUuids(leakByProjects);
  }

  private static void addDirectories(IssueQuery.Builder builder, List<ComponentDto> directories) {
    Collection<String> directoryModuleUuids = new HashSet<>();
    Collection<String> directoryPaths = new HashSet<>();
    for (ComponentDto directory : directories) {
      directoryModuleUuids.add(directory.moduleUuid());
      directoryPaths.add(directory.path());
    }
    builder.moduleUuids(directoryModuleUuids);
    builder.directories(directoryPaths);
  }

  private List<ComponentDto> getComponentsFromKeys(DbSession dbSession, Collection<String> componentKeys, @Nullable String branch) {
    List<ComponentDto> componentDtos = branch == null
      ? dbClient.componentDao().selectByKeys(dbSession, componentKeys)
      : dbClient.componentDao().selectByKeysAndBranch(dbSession, componentKeys, branch);
    if (!componentKeys.isEmpty() && componentDtos.isEmpty()) {
      return singletonList(UNKNOWN_COMPONENT);
    }
    return componentDtos;
  }

  private List<ComponentDto> getComponentsFromUuids(DbSession dbSession, Collection<String> componentUuids) {
    List<ComponentDto> componentDtos = dbClient.componentDao().selectByUuids(dbSession, componentUuids);
    if (!componentUuids.isEmpty() && componentDtos.isEmpty()) {
      return singletonList(UNKNOWN_COMPONENT);
    }
    return componentDtos;
  }

  @VisibleForTesting
  static Collection<RuleKey> toRules(@Nullable Object o) {
    Collection<RuleKey> result = null;
    if (o != null) {
      if (o instanceof List) {
        // assume that it contains only strings
        result = stringsToRules((List<String>) o);
      } else if (o instanceof String) {
        result = stringsToRules(newArrayList(Splitter.on(',').omitEmptyStrings().split((String) o)));
      }
    }
    return result;
  }

  @CheckForNull
  private static Collection<RuleKey> stringsToRules(@Nullable Collection<String> rules) {
    if (rules != null) {
      return Collections2.transform(rules, RuleKey::parse);
    }
    return null;
  }

  private static String toProjectUuid(ComponentDto componentDto) {
    String mainBranchProjectUuid = componentDto.getMainBranchProjectUuid();
    return mainBranchProjectUuid == null ? componentDto.projectUuid() : mainBranchProjectUuid;
  }

  private static void setBranch(IssueQuery.Builder builder, ComponentDto component, @Nullable String branch) {
    builder.branchUuid(branch == null ? null : component.projectUuid());
    builder.mainBranch(branch == null || component.equals(UNKNOWN_COMPONENT) || !branch.equals(component.getBranch()));
  }
}
