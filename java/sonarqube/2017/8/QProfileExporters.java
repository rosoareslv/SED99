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
package org.sonar.server.qualityprofile;

import com.google.common.collect.FluentIterable;
import com.google.common.collect.ListMultimap;
import com.google.common.collect.Lists;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.apache.commons.lang.ArrayUtils;
import org.apache.commons.lang.StringUtils;
import org.sonar.api.profiles.ProfileExporter;
import org.sonar.api.profiles.ProfileImporter;
import org.sonar.api.profiles.RulesProfile;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.rules.ActiveRuleParam;
import org.sonar.api.rules.Rule;
import org.sonar.api.rules.RuleFinder;
import org.sonar.api.rules.RulePriority;
import org.sonar.api.server.ServerSide;
import org.sonar.api.utils.ValidationMessages;
import org.sonar.core.util.stream.MoreCollectors;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.qualityprofile.ActiveRuleDto;
import org.sonar.db.qualityprofile.ActiveRuleParamDto;
import org.sonar.db.qualityprofile.OrgActiveRuleDto;
import org.sonar.db.qualityprofile.QProfileDto;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.exceptions.NotFoundException;

import static org.sonar.server.ws.WsUtils.checkRequest;

@ServerSide
public class QProfileExporters {

  private final DbClient dbClient;
  private final RuleFinder ruleFinder;
  private final RuleActivator ruleActivator;
  private final ProfileExporter[] exporters;
  private final ProfileImporter[] importers;

  public QProfileExporters(DbClient dbClient, RuleFinder ruleFinder, RuleActivator ruleActivator, ProfileExporter[] exporters, ProfileImporter[] importers) {
    this.dbClient = dbClient;
    this.ruleFinder = ruleFinder;
    this.ruleActivator = ruleActivator;
    this.exporters = exporters;
    this.importers = importers;
  }

  /**
   * Used by Pico if no {@link ProfileImporter} is found
   */
  public QProfileExporters(DbClient dbClient, RuleFinder ruleFinder, RuleActivator ruleActivator, ProfileExporter[] exporters) {
    this(dbClient, ruleFinder, ruleActivator, exporters, new ProfileImporter[0]);
  }

  /**
   * Used by Pico if no {@link ProfileExporter} is found
   */
  public QProfileExporters(DbClient dbClient, RuleFinder ruleFinder, RuleActivator ruleActivator, ProfileImporter[] importers) {
    this(dbClient, ruleFinder, ruleActivator, new ProfileExporter[0], importers);
  }

  /**
   * Used by Pico if no {@link ProfileImporter} nor {@link ProfileExporter} is found
   */
  public QProfileExporters(DbClient dbClient, RuleFinder ruleFinder, RuleActivator ruleActivator) {
    this(dbClient, ruleFinder, ruleActivator, new ProfileExporter[0], new ProfileImporter[0]);
  }

  public List<ProfileExporter> exportersForLanguage(String language) {
    List<ProfileExporter> result = new ArrayList<>();
    for (ProfileExporter exporter : exporters) {
      if (exporter.getSupportedLanguages() == null || exporter.getSupportedLanguages().length == 0 || ArrayUtils.contains(exporter.getSupportedLanguages(), language)) {
        result.add(exporter);
      }
    }
    return result;
  }

  public String mimeType(String exporterKey) {
    ProfileExporter exporter = findExporter(exporterKey);
    return exporter.getMimeType();
  }

  public void export(DbSession dbSession, QProfileDto profile, String exporterKey, Writer writer) {
    ProfileExporter exporter = findExporter(exporterKey);
    exporter.exportProfile(wrap(dbSession, profile), writer);
  }

  private RulesProfile wrap(DbSession dbSession, QProfileDto profile) {
    RulesProfile target = new RulesProfile(profile.getName(), profile.getLanguage());
    List<OrgActiveRuleDto> activeRuleDtos = dbClient.activeRuleDao().selectByProfile(dbSession, profile);
    List<ActiveRuleParamDto> activeRuleParamDtos = dbClient.activeRuleDao().selectParamsByActiveRuleIds(dbSession, Lists.transform(activeRuleDtos, ActiveRuleDto::getId));
    ListMultimap<Integer, ActiveRuleParamDto> activeRuleParamsByActiveRuleId = FluentIterable.from(activeRuleParamDtos).index(ActiveRuleParamDto::getActiveRuleId);

    for (ActiveRuleDto activeRule : activeRuleDtos) {
      // TODO all rules should be loaded by using one query with all active rule keys as parameter
      Rule rule = ruleFinder.findByKey(activeRule.getRuleKey());
      org.sonar.api.rules.ActiveRule wrappedActiveRule = target.activateRule(rule, RulePriority.valueOf(activeRule.getSeverityString()));
      List<ActiveRuleParamDto> paramDtos = activeRuleParamsByActiveRuleId.get(activeRule.getId());
      for (ActiveRuleParamDto activeRuleParamDto : paramDtos) {
        wrappedActiveRule.setParameter(activeRuleParamDto.getKey(), activeRuleParamDto.getValue());
      }
    }
    return target;
  }

  private ProfileExporter findExporter(String exporterKey) {
    for (ProfileExporter e : exporters) {
      if (exporterKey.equals(e.getKey())) {
        return e;
      }
    }
    throw new NotFoundException("Unknown quality profile exporter: " + exporterKey);
  }

  public QProfileResult importXml(QProfileDto profileDto, String importerKey, InputStream xml, DbSession dbSession) {
    return importXml(profileDto, importerKey, new InputStreamReader(xml, StandardCharsets.UTF_8), dbSession);
  }

  private QProfileResult importXml(QProfileDto profileDto, String importerKey, Reader xml, DbSession dbSession) {
    QProfileResult result = new QProfileResult();
    ValidationMessages messages = ValidationMessages.create();
    ProfileImporter importer = getProfileImporter(importerKey);
    RulesProfile rulesProfile = importer.importProfile(xml, messages);
    List<ActiveRuleChange> changes = importProfile(profileDto, rulesProfile, dbSession);
    result.addChanges(changes);
    processValidationMessages(messages, result);
    return result;
  }

  private List<ActiveRuleChange> importProfile(QProfileDto profileDto, RulesProfile rulesProfile, DbSession dbSession) {
    List<ActiveRuleChange> changes = new ArrayList<>();
    for (org.sonar.api.rules.ActiveRule activeRule : rulesProfile.getActiveRules()) {
      changes.addAll(ruleActivator.activate(dbSession, toRuleActivation(activeRule), profileDto));
    }
    return changes;
  }

  private ProfileImporter getProfileImporter(String importerKey) {
    for (ProfileImporter importer : importers) {
      if (StringUtils.equals(importerKey, importer.getKey())) {
        return importer;
      }
    }
    throw BadRequestException.create("No such importer : " + importerKey);
  }

  private static void processValidationMessages(ValidationMessages messages, QProfileResult result) {
    checkRequest(messages.getErrors().isEmpty(), messages.getErrors());
    result.addWarnings(messages.getWarnings());
    result.addInfos(messages.getInfos());
  }

  private static RuleActivation toRuleActivation(org.sonar.api.rules.ActiveRule activeRule) {
    RuleKey ruleKey = activeRule.getRule().ruleKey();
    String severity = activeRule.getSeverity().name();
    Map<String, String> params = activeRule.getActiveRuleParams().stream()
      .collect(MoreCollectors.uniqueIndex(ActiveRuleParam::getKey, ActiveRuleParam::getValue));
    return RuleActivation.create(ruleKey, severity, params);
  }

}
