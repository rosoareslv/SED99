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
package org.sonar.db;

import com.google.common.collect.ImmutableList;
import java.util.List;
import org.sonar.core.platform.Module;
import org.sonar.db.ce.CeActivityDao;
import org.sonar.db.ce.CeQueueDao;
import org.sonar.db.ce.CeScannerContextDao;
import org.sonar.db.ce.CeTaskCharacteristicDao;
import org.sonar.db.ce.CeTaskInputDao;
import org.sonar.db.component.ComponentDao;
import org.sonar.db.component.ComponentKeyUpdaterDao;
import org.sonar.db.component.ComponentLinkDao;
import org.sonar.db.component.SnapshotDao;
import org.sonar.db.duplication.DuplicationDao;
import org.sonar.db.es.EsQueueDao;
import org.sonar.db.event.EventDao;
import org.sonar.db.issue.IssueChangeDao;
import org.sonar.db.issue.IssueDao;
import org.sonar.db.loadedtemplate.LoadedTemplateDao;
import org.sonar.db.measure.MeasureDao;
import org.sonar.db.measure.custom.CustomMeasureDao;
import org.sonar.db.metric.MetricDao;
import org.sonar.db.notification.NotificationQueueDao;
import org.sonar.db.organization.OrganizationDao;
import org.sonar.db.organization.OrganizationMemberDao;
import org.sonar.db.permission.AuthorizationDao;
import org.sonar.db.permission.GroupPermissionDao;
import org.sonar.db.permission.UserPermissionDao;
import org.sonar.db.permission.template.PermissionTemplateCharacteristicDao;
import org.sonar.db.permission.template.PermissionTemplateDao;
import org.sonar.db.property.InternalPropertiesDao;
import org.sonar.db.property.PropertiesDao;
import org.sonar.db.purge.PurgeDao;
import org.sonar.db.qualitygate.ProjectQgateAssociationDao;
import org.sonar.db.qualitygate.QualityGateConditionDao;
import org.sonar.db.qualitygate.QualityGateDao;
import org.sonar.db.qualityprofile.ActiveRuleDao;
import org.sonar.db.qualityprofile.DefaultQProfileDao;
import org.sonar.db.qualityprofile.QProfileChangeDao;
import org.sonar.db.qualityprofile.QualityProfileDao;
import org.sonar.db.rule.RuleDao;
import org.sonar.db.rule.RuleRepositoryDao;
import org.sonar.db.schemamigration.SchemaMigrationDao;
import org.sonar.db.source.FileSourceDao;
import org.sonar.db.user.GroupDao;
import org.sonar.db.user.GroupMembershipDao;
import org.sonar.db.user.RoleDao;
import org.sonar.db.user.UserDao;
import org.sonar.db.user.UserGroupDao;
import org.sonar.db.user.UserTokenDao;
import org.sonar.db.webhook.WebhookDeliveryDao;

public class DaoModule extends Module {
  private static final List<Class<? extends Dao>> classes = ImmutableList.<Class<? extends Dao>>builder().add(
    // =====================================================================
    // for readability and easier merge, keep list ordered alphabetically
    // =====================================================================
    ActiveRuleDao.class,
    AuthorizationDao.class,
    CeActivityDao.class,
    CeQueueDao.class,
    CeScannerContextDao.class,
    CeTaskCharacteristicDao.class,
    CeTaskInputDao.class,
    ComponentDao.class,
    ComponentKeyUpdaterDao.class,
    ComponentLinkDao.class,
    CustomMeasureDao.class,
    DefaultQProfileDao.class,
    DuplicationDao.class,
    EsQueueDao.class,
    EventDao.class,
    FileSourceDao.class,
    GroupDao.class,
    GroupMembershipDao.class,
    GroupPermissionDao.class,
    InternalPropertiesDao.class,
    IssueChangeDao.class,
    IssueDao.class,
    LoadedTemplateDao.class,
    MeasureDao.class,
    MetricDao.class,
    NotificationQueueDao.class,
    OrganizationDao.class,
    OrganizationMemberDao.class,
    PermissionTemplateCharacteristicDao.class,
    PermissionTemplateDao.class,
    ProjectQgateAssociationDao.class,
    PropertiesDao.class,
    PurgeDao.class,
    QProfileChangeDao.class,
    QualityGateConditionDao.class,
    QualityGateDao.class,
    QualityProfileDao.class,
    RoleDao.class,
    RuleDao.class,
    RuleRepositoryDao.class,
    SnapshotDao.class,
    SchemaMigrationDao.class,
    UserDao.class,
    UserGroupDao.class,
    UserPermissionDao.class,
    UserTokenDao.class,
    WebhookDeliveryDao.class)
    .build();

  @Override
  protected void configureModule() {
    add(classes.toArray());
  }

  public static List<Class<? extends Dao>> classes() {
    return classes;
  }
}
