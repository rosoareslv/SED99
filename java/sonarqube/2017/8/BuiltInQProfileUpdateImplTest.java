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

import java.util.List;
import java.util.Optional;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.sonar.api.profiles.RulesProfile;
import org.sonar.api.rules.RulePriority;
import org.sonar.api.utils.System2;
import org.sonar.api.utils.internal.TestSystem2;
import org.sonar.db.DbTester;
import org.sonar.db.qualityprofile.ActiveRuleDto;
import org.sonar.db.qualityprofile.RulesProfileDto;
import org.sonar.db.rule.RuleDefinitionDto;
import org.sonar.server.qualityprofile.index.ActiveRuleIndexer;
import org.sonar.server.tester.UserSessionRule;
import org.sonar.server.util.IntegerTypeValidation;
import org.sonar.server.util.StringTypeValidation;
import org.sonar.server.util.TypeValidations;

import static java.util.Arrays.asList;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.sonar.api.rules.RulePriority.BLOCKER;
import static org.sonar.api.rules.RulePriority.CRITICAL;
import static org.sonar.api.rules.RulePriority.MAJOR;
import static org.sonar.db.qualityprofile.QualityProfileTesting.newRuleProfileDto;

public class BuiltInQProfileUpdateImplTest {

  private static final long NOW = 1_000;
  private static final long PAST = NOW - 100;

  @Rule
  public BuiltInQProfileRepositoryRule builtInProfileRepository = new BuiltInQProfileRepositoryRule();
  @Rule
  public DbTester db = DbTester.create().setDisableDefaultOrganization(true);
  @Rule
  public UserSessionRule userSession = UserSessionRule.standalone();
  private System2 system2 = new TestSystem2().setNow(NOW);
  private ActiveRuleIndexer activeRuleIndexer = mock(ActiveRuleIndexer.class);
  private RuleActivatorContextFactory contextFactory = new RuleActivatorContextFactory(db.getDbClient());
  private TypeValidations typeValidations = new TypeValidations(asList(new StringTypeValidation(), new IntegerTypeValidation()));
  private RuleActivator ruleActivator = new RuleActivator(system2, db.getDbClient(), null, contextFactory, typeValidations, activeRuleIndexer, userSession);

  private BuiltInQProfileUpdateImpl underTest = new BuiltInQProfileUpdateImpl(db.getDbClient(), ruleActivator, activeRuleIndexer);

  private RulesProfileDto persistedProfile;

  @Before
  public void setUp() {
    persistedProfile = newRuleProfileDto(rp -> rp
      .setIsBuiltIn(true)
      .setLanguage("xoo")
      .setRulesUpdatedAt(null));
    db.getDbClient().qualityProfileDao().insert(db.getSession(), persistedProfile);
    db.commit();
  }

  @Test
  public void activate_new_rules() {
    RuleDefinitionDto rule1 = db.rules().insert(r -> r.setLanguage("xoo"));
    RuleDefinitionDto rule2 = db.rules().insert(r -> r.setLanguage("xoo"));
    RulesProfile apiProfile = RulesProfile.create("Sonar way", "xoo");
    activateRuleInDef(apiProfile, rule1, CRITICAL);
    activateRuleInDef(apiProfile, rule2, MAJOR);
    BuiltInQProfile builtIn = builtInProfileRepository.create(apiProfile);

    underTest.update(db.getSession(), builtIn, persistedProfile);

    List<ActiveRuleDto> activeRules = db.getDbClient().activeRuleDao().selectByRuleProfile(db.getSession(), persistedProfile);
    assertThat(activeRules).hasSize(2);
    assertThatRuleIsNewlyActivated(activeRules, rule1, CRITICAL);
    assertThatRuleIsNewlyActivated(activeRules, rule2, MAJOR);
    assertThatProfileIsMarkedAsUpdated(persistedProfile);
  }

  @Test
  public void already_activated_rule_is_updated_in_case_of_differences() {
    RuleDefinitionDto rule = db.rules().insert(r -> r.setLanguage("xoo"));
    RulesProfile apiProfile = RulesProfile.create("Sonar way", "xoo");
    activateRuleInDef(apiProfile, rule, CRITICAL);
    BuiltInQProfile builtIn = builtInProfileRepository.create(apiProfile);

    activateRuleInDb(persistedProfile, rule, BLOCKER);

    underTest.update(db.getSession(), builtIn, persistedProfile);

    List<ActiveRuleDto> activeRules = db.getDbClient().activeRuleDao().selectByRuleProfile(db.getSession(), persistedProfile);
    assertThat(activeRules).hasSize(1);
    assertThatRuleIsUpdated(activeRules, rule, CRITICAL);
    assertThatProfileIsMarkedAsUpdated(persistedProfile);
  }

  @Test
  public void already_activated_rule_is_not_touched_if_no_differences() {
    RuleDefinitionDto rule = db.rules().insert(r -> r.setLanguage("xoo"));
    RulesProfile apiProfile = RulesProfile.create("Sonar way", "xoo");
    activateRuleInDef(apiProfile, rule, CRITICAL);
    BuiltInQProfile builtIn = builtInProfileRepository.create(apiProfile);

    activateRuleInDb(persistedProfile, rule, CRITICAL);

    underTest.update(db.getSession(), builtIn, persistedProfile);

    List<ActiveRuleDto> activeRules = db.getDbClient().activeRuleDao().selectByRuleProfile(db.getSession(), persistedProfile);
    assertThat(activeRules).hasSize(1);
    assertThatRuleIsUntouched(activeRules, rule, CRITICAL);
    assertThatProfileIsNotMarkedAsUpdated(persistedProfile);
  }

  @Test
  public void deactivate_rule_that_is_not_in_built_in_definition_anymore() {
    RuleDefinitionDto rule1 = db.rules().insert(r -> r.setLanguage("xoo"));
    RuleDefinitionDto rule2 = db.rules().insert(r -> r.setLanguage("xoo"));
    RulesProfile apiProfile = RulesProfile.create("Sonar way", "xoo");
    activateRuleInDef(apiProfile, rule2, CRITICAL);
    BuiltInQProfile builtIn = builtInProfileRepository.create(apiProfile);

    // built-in definition contains only rule2
    // so rule1 must be deactivated
    activateRuleInDb(persistedProfile, rule1, CRITICAL);

    underTest.update(db.getSession(), builtIn, persistedProfile);

    List<ActiveRuleDto> activeRules = db.getDbClient().activeRuleDao().selectByRuleProfile(db.getSession(), persistedProfile);
    assertThat(activeRules).hasSize(1);
    assertThatRuleIsDeactivated(activeRules, rule1);
    assertThatProfileIsMarkedAsUpdated(persistedProfile);
  }

  @Test
  public void activate_deactivate_and_update_three_rules_at_the_same_time() {
    RuleDefinitionDto rule1 = db.rules().insert(r -> r.setLanguage("xoo"));
    RuleDefinitionDto rule2 = db.rules().insert(r -> r.setLanguage("xoo"));
    RuleDefinitionDto rule3 = db.rules().insert(r -> r.setLanguage("xoo"));
    RulesProfile apiProfile = RulesProfile.create("Sonar way", "xoo");
    activateRuleInDef(apiProfile, rule1, CRITICAL);
    activateRuleInDef(apiProfile, rule2, MAJOR);
    BuiltInQProfile builtIn = builtInProfileRepository.create(apiProfile);

    // rule1 must be updated (blocker to critical)
    // rule2 must be activated
    // rule3 must be deactivated
    activateRuleInDb(persistedProfile, rule1, BLOCKER);
    activateRuleInDb(persistedProfile, rule3, BLOCKER);

    underTest.update(db.getSession(), builtIn, persistedProfile);

    List<ActiveRuleDto> activeRules = db.getDbClient().activeRuleDao().selectByRuleProfile(db.getSession(), persistedProfile);
    assertThat(activeRules).hasSize(2);
    assertThatRuleIsUpdated(activeRules, rule1, CRITICAL);
    assertThatRuleIsNewlyActivated(activeRules, rule2, MAJOR);
    assertThatRuleIsDeactivated(activeRules, rule3);
    assertThatProfileIsMarkedAsUpdated(persistedProfile);
  }


  private static void assertThatRuleIsNewlyActivated(List<ActiveRuleDto> activeRules, RuleDefinitionDto rule, RulePriority severity) {
    ActiveRuleDto activeRule = findRule(activeRules, rule).get();

    assertThat(activeRule.getInheritance()).isNull();
    assertThat(activeRule.getSeverityString()).isEqualTo(severity.name());
    assertThat(activeRule.getCreatedAt()).isEqualTo(NOW);
    assertThat(activeRule.getUpdatedAt()).isEqualTo(NOW);
  }

  private static void assertThatRuleIsUpdated(List<ActiveRuleDto> activeRules, RuleDefinitionDto rule, RulePriority severity) {
    ActiveRuleDto activeRule = findRule(activeRules, rule).get();

    assertThat(activeRule.getInheritance()).isNull();
    assertThat(activeRule.getSeverityString()).isEqualTo(severity.name());
    assertThat(activeRule.getCreatedAt()).isEqualTo(PAST);
    assertThat(activeRule.getUpdatedAt()).isEqualTo(NOW);
  }

  private static void assertThatRuleIsUntouched(List<ActiveRuleDto> activeRules, RuleDefinitionDto rule, RulePriority severity) {
    ActiveRuleDto activeRule = findRule(activeRules, rule).get();

    assertThat(activeRule.getInheritance()).isNull();
    assertThat(activeRule.getSeverityString()).isEqualTo(severity.name());
    assertThat(activeRule.getCreatedAt()).isEqualTo(PAST);
    assertThat(activeRule.getUpdatedAt()).isEqualTo(PAST);
  }

  private static void assertThatRuleIsDeactivated(List<ActiveRuleDto> activeRules, RuleDefinitionDto rule) {
    assertThat(findRule(activeRules, rule)).isEmpty();
  }

  private void assertThatProfileIsMarkedAsUpdated(RulesProfileDto dto) {
    RulesProfileDto reloaded = db.getDbClient().qualityProfileDao().selectBuiltInRulesProfiles(db.getSession())
      .stream()
      .filter(p -> p.getKee().equals(dto.getKee()))
      .findFirst()
      .get();
    assertThat(reloaded.getRulesUpdatedAt()).isNotEmpty();
  }

  private void assertThatProfileIsNotMarkedAsUpdated(RulesProfileDto dto) {
    RulesProfileDto reloaded = db.getDbClient().qualityProfileDao().selectBuiltInRulesProfiles(db.getSession())
      .stream()
      .filter(p -> p.getKee().equals(dto.getKee()))
      .findFirst()
      .get();
    assertThat(reloaded.getRulesUpdatedAt()).isNull();
  }

  private static Optional<ActiveRuleDto> findRule(List<ActiveRuleDto> activeRules, RuleDefinitionDto rule) {
    return activeRules.stream()
      .filter(ar -> ar.getRuleKey().equals(rule.getKey()))
      .findFirst();
  }

  private static void activateRuleInDef(RulesProfile apiProfile, RuleDefinitionDto rule, RulePriority severity) {
    apiProfile.activateRule(org.sonar.api.rules.Rule.create(rule.getRepositoryKey(), rule.getRuleKey()), severity);
  }

  private void activateRuleInDb(RulesProfileDto profile, RuleDefinitionDto rule, RulePriority severity) {
    ActiveRuleDto dto = new ActiveRuleDto()
      .setProfileId(profile.getId())
      .setSeverity(severity.name())
      .setRuleId(rule.getId())
      .setCreatedAt(PAST)
      .setUpdatedAt(PAST);
    db.getDbClient().activeRuleDao().insert(db.getSession(), dto);
    db.commit();
  }

}
