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
package org.sonar.server.rule;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Sets;
import java.util.Date;
import java.util.List;
import org.assertj.core.api.Fail;
import org.junit.After;
import org.junit.Before;
import org.junit.ClassRule;
import org.junit.Test;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.rule.RuleStatus;
import org.sonar.api.rule.Severity;
import org.sonar.api.server.debt.DebtRemediationFunction;
import org.sonar.db.DbClient;
import org.sonar.db.DbSession;
import org.sonar.db.rule.RuleDao;
import org.sonar.db.rule.RuleDto;
import org.sonar.db.rule.RuleDto.Format;
import org.sonar.db.rule.RuleParamDto;
import org.sonar.db.rule.RuleTesting;
import org.sonar.server.es.SearchOptions;
import org.sonar.server.exceptions.BadRequestException;
import org.sonar.server.rule.index.RuleIndex;
import org.sonar.server.rule.index.RuleIndexer;
import org.sonar.server.rule.index.RuleQuery;
import org.sonar.server.tester.ServerTester;
import org.sonar.server.tester.UserSessionRule;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.Assert.fail;

// TODO replace ServerTester by EsTester / DbTester
public class RuleCreatorMediumTest {

  @ClassRule
  public static ServerTester tester = new ServerTester().withEsIndexes();

  @org.junit.Rule
  public UserSessionRule userSessionRule = UserSessionRule.forServerTester(tester);

  DbSession dbSession;
  DbClient db = tester.get(DbClient.class);
  RuleDao dao = tester.get(RuleDao.class);
  RuleCreator creator = tester.get(RuleCreator.class);
  RuleIndex ruleIndex = tester.get(RuleIndex.class);
  RuleIndexer ruleIndexer;

  @Before
  public void before() {
    tester.clearDbAndIndexes();
    dbSession = tester.get(DbClient.class).openSession(false);
    ruleIndexer = tester.get(RuleIndexer.class);
    ruleIndexer.setEnabled(true);
  }

  @After
  public void after() {
    dbSession.close();
  }

  @Test
  public void create_custom_rule() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    // Create custom rule
    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setMarkdownDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));
    RuleKey customRuleKey = creator.create(newRule);

    dbSession.clearCache();

    RuleDto rule = db.ruleDao().selectOrFailByKey(dbSession, customRuleKey);
    assertThat(rule).isNotNull();
    assertThat(rule.getKey()).isEqualTo(RuleKey.of("java", "CUSTOM_RULE"));
    assertThat(rule.getTemplateId()).isEqualTo(templateRule.getId());
    assertThat(rule.getName()).isEqualTo("My custom");
    assertThat(rule.getDescription()).isEqualTo("Some description");
    assertThat(rule.getSeverityString()).isEqualTo("MAJOR");
    assertThat(rule.getStatus()).isEqualTo(RuleStatus.READY);
    assertThat(rule.getLanguage()).isEqualTo("java");
    assertThat(rule.getConfigKey()).isEqualTo("S001");
    assertThat(rule.getDefaultRemediationFunction()).isEqualTo("LINEAR_OFFSET");
    assertThat(rule.getDefaultRemediationGapMultiplier()).isEqualTo("1h");
    assertThat(rule.getDefaultRemediationBaseEffort()).isEqualTo("5min");
    assertThat(rule.getGapDescription()).isEqualTo("desc");
    assertThat(rule.getTags()).containsOnly("usertag1", "usertag2");
    assertThat(rule.getSystemTags()).containsOnly("tag1", "tag4");

    List<RuleParamDto> params = db.ruleDao().selectRuleParamsByRuleKey(dbSession, customRuleKey);
    assertThat(params).hasSize(1);

    RuleParamDto param = params.get(0);
    // From template rule
    assertThat(param.getName()).isEqualTo("regex");
    assertThat(param.getDescription()).isEqualTo("Reg ex");
    assertThat(param.getType()).isEqualTo("STRING");
    // From user
    assertThat(param.getDefaultValue()).isEqualTo("a.*");

    assertThat(ruleIndex.search(new RuleQuery(), new SearchOptions()).getIds()).containsOnly(customRuleKey, templateRule.getKey());
  }

  @Test
  public void create_custom_rule_with_empty_parameter_value() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", ""));

    RuleKey customRuleKey = creator.create(newRule);
    dbSession.clearCache();

    List<RuleParamDto> params = db.ruleDao().selectRuleParamsByRuleKey(dbSession, customRuleKey);
    assertThat(params).hasSize(1);

    RuleParamDto param = params.get(0);
    assertThat(param.getName()).isEqualTo("regex");
    assertThat(param.getDescription()).isEqualTo("Reg ex");
    assertThat(param.getType()).isEqualTo("STRING");
    assertThat(param.getDefaultValue()).isNull();
  }

  @Test
  public void create_custom_rule_with_no_parameter_value() {
    // insert template rule
    RuleDto templateRule = createTemplateRuleWithIntArrayParam();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY);

    RuleKey customRuleKey = creator.create(newRule);
    dbSession.clearCache();

    List<RuleParamDto> params = db.ruleDao().selectRuleParamsByRuleKey(dbSession, customRuleKey);
    assertThat(params).hasSize(1);

    RuleParamDto param = params.get(0);
    assertThat(param.getName()).isEqualTo("myIntegers");
    assertThat(param.getDescription()).isEqualTo("My Integers");
    assertThat(param.getType()).isEqualTo("INTEGER,multiple=true,values=1;2;3");
    assertThat(param.getDefaultValue()).isNull();
  }

  @Test
  public void create_custom_rule_with_multiple_parameter_values() {
    // insert template rule
    RuleDto templateRule = createTemplateRuleWithIntArrayParam();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("myIntegers", "1,3"));

    RuleKey customRuleKey = creator.create(newRule);
    dbSession.clearCache();

    List<RuleParamDto> params = db.ruleDao().selectRuleParamsByRuleKey(dbSession, customRuleKey);
    assertThat(params).hasSize(1);

    RuleParamDto param = params.get(0);
    assertThat(param.getName()).isEqualTo("myIntegers");
    assertThat(param.getDescription()).isEqualTo("My Integers");
    assertThat(param.getType()).isEqualTo("INTEGER,multiple=true,values=1;2;3");
    assertThat(param.getDefaultValue()).isEqualTo("1,3");
  }

  @Test
  public void create_custom_rule_with_invalid_parameter() {
    // insert template rule
    RuleDto templateRule = createTemplateRuleWithIntArrayParam();

    // Create custom rule
    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setMarkdownDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("myIntegers", "1,polop,2"));
    try {
      creator.create(newRule);
      Fail.failBecauseExceptionWasNotThrown(BadRequestException.class);
    } catch (BadRequestException iae) {
      assertThat(iae).hasMessage("errors.type.notInteger");
    }

    dbSession.clearCache();
  }

  @Test
  public void create_custom_rule_with_invalid_parameters() {
    // insert template rule
    RuleDto templateRule = createTemplateRuleWithTwoIntParams();

    // Create custom rule
    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setMarkdownDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("first", "polop", "second", "palap"));
    try {
      creator.create(newRule);
      Fail.failBecauseExceptionWasNotThrown(BadRequestException.class);
    } catch (BadRequestException badRequest) {
      assertThat(badRequest.errors().toString()).contains("palap").contains("polop");
    }

    dbSession.clearCache();
  }

  @Test
  public void reactivate_custom_rule_if_already_exists_in_removed_status() {
    String key = "CUSTOM_RULE";

    // insert template rule
    RuleDto templateRule = createTemplateRule();

    // insert a removed rule
    RuleDto rule = RuleTesting.newCustomRule(templateRule)
      .setRuleKey(key)
      .setStatus(RuleStatus.REMOVED)
      .setName("Old name")
      .setDescription("Old description")
      .setDescriptionFormat(Format.MARKDOWN)
      .setSeverity(Severity.INFO);
    dao.insert(dbSession, rule);
    dao.insertRuleParam(dbSession, rule, dao.selectRuleParamsByRuleKey(dbSession, templateRule.getKey()).get(0).setDefaultValue("a.*"));
    dbSession.commit();
    dbSession.clearCache();

    // Create custom rule with same key, but with different values
    NewCustomRule newRule = NewCustomRule.createForCustomRule(key, templateRule.getKey())
      .setName("New name")
      .setMarkdownDescription("New description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "c.*"));
    RuleKey customRuleKey = creator.create(newRule);

    dbSession.clearCache();

    RuleDto result = db.ruleDao().selectOrFailByKey(dbSession, customRuleKey);
    assertThat(result.getKey()).isEqualTo(RuleKey.of("java", key));
    assertThat(result.getStatus()).isEqualTo(RuleStatus.READY);

    // These values should be the same than before
    assertThat(result.getName()).isEqualTo("Old name");
    assertThat(result.getDescription()).isEqualTo("Old description");
    assertThat(result.getSeverityString()).isEqualTo(Severity.INFO);

    List<RuleParamDto> params = db.ruleDao().selectRuleParamsByRuleKey(dbSession, customRuleKey);
    assertThat(params).hasSize(1);
    assertThat(params.get(0).getDefaultValue()).isEqualTo("a.*");
  }

  @Test
  public void generate_reactivation_exception_when_rule_exists_in_removed_status_and_prevent_reactivation_parameter_is_true() {
    String key = "CUSTOM_RULE";

    // insert template rule
    RuleDto templateRule = createTemplateRule();

    // insert a removed rule
    RuleDto rule = RuleTesting.newCustomRule(templateRule)
      .setRuleKey(key)
      .setStatus(RuleStatus.REMOVED)
      .setName("Old name")
      .setDescription("Old description")
      .setSeverity(Severity.INFO);
    dao.insert(dbSession, rule);
    dao.insertRuleParam(dbSession, rule, dao.selectRuleParamsByRuleKey(dbSession, templateRule.getKey()).get(0).setDefaultValue("a.*"));
    dbSession.commit();
    dbSession.clearCache();

    // Create custom rule with same key, but with different values
    NewCustomRule newRule = NewCustomRule.createForCustomRule(key, templateRule.getKey())
      .setName("New name")
      .setHtmlDescription("New description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "c.*"))
      .setPreventReactivation(true);

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(ReactivationException.class);
      ReactivationException reactivationException = (ReactivationException) e;
      assertThat(reactivationException.ruleKey()).isEqualTo(rule.getKey());
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_invalid_key() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("*INVALID*", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.invalid_rule_key");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_rule_key_already_exists() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    // Create a custom rule
    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));
    creator.create(newRule);

    try {
      // Create another custom rule having same key
      newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
        .setName("My another custom")
        .setHtmlDescription("Some description")
        .setSeverity(Severity.MAJOR)
        .setStatus(RuleStatus.READY)
        .setParameters(ImmutableMap.of("regex", "a.*"));
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(IllegalArgumentException.class).hasMessage("A rule with the key 'CUSTOM_RULE' already exists");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_missing_name() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.missing_name");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_missing_description() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.missing_description");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_missing_severity() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.missing_severity");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_invalid_severity() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity("INVALID")
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.invalid_severity");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_missing_status() {
    // insert template rule
    RuleDto templateRule = createTemplateRule();

    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", templateRule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(BadRequestException.class).hasMessage("coding_rules.validation.missing_status");
    }
  }

  @Test
  public void fail_to_create_custom_rule_when_wrong_rule_template() {
    // insert rule
    RuleDto rule = RuleTesting.newDto(RuleKey.of("java", "S001")).setIsTemplate(false);
    dao.insert(dbSession, rule);
    dbSession.commit();

    // Create custom rule with unknown template rule
    NewCustomRule newRule = NewCustomRule.createForCustomRule("CUSTOM_RULE", rule.getKey())
      .setName("My custom")
      .setHtmlDescription("Some description")
      .setSeverity(Severity.MAJOR)
      .setStatus(RuleStatus.READY)
      .setParameters(ImmutableMap.of("regex", "a.*"));

    try {
      creator.create(newRule);
      fail();
    } catch (Exception e) {
      assertThat(e).isInstanceOf(IllegalArgumentException.class).hasMessage("This rule is not a template rule: java:S001");
    }
  }

  private RuleDto createTemplateRule() {
    RuleDto templateRule = RuleTesting.newDto(RuleKey.of("java", "S001"))
      .setIsTemplate(true)
      .setLanguage("java")
      .setConfigKey("S001")
      .setDefaultRemediationFunction(DebtRemediationFunction.Type.LINEAR_OFFSET.name())
      .setDefaultRemediationGapMultiplier("1h")
      .setDefaultRemediationBaseEffort("5min")
      .setGapDescription("desc")
      .setTags(Sets.newHashSet("usertag1", "usertag2"))
      .setSystemTags(Sets.newHashSet("tag1", "tag4"))
      .setCreatedAt(new Date().getTime())
      .setUpdatedAt(new Date().getTime());
    dao.insert(dbSession, templateRule);
    RuleParamDto ruleParamDto = RuleParamDto.createFor(templateRule).setName("regex").setType("STRING").setDescription("Reg ex").setDefaultValue(".*");
    dao.insertRuleParam(dbSession, templateRule, ruleParamDto);
    dbSession.commit();
    ruleIndexer.index();
    return templateRule;
  }

  private RuleDto createTemplateRuleWithIntArrayParam() {
    RuleDto templateRule = RuleTesting.newDto(RuleKey.of("java", "S002"))
      .setIsTemplate(true)
      .setLanguage("java")
      .setConfigKey("S002")
      .setDefaultRemediationFunction(DebtRemediationFunction.Type.LINEAR_OFFSET.name())
      .setDefaultRemediationGapMultiplier("1h")
      .setDefaultRemediationBaseEffort("5min")
      .setGapDescription("desc")
      .setCreatedAt(new Date().getTime())
      .setUpdatedAt(new Date().getTime());
    dao.insert(dbSession, templateRule);
    RuleParamDto ruleParamDto = RuleParamDto.createFor(templateRule)
      .setName("myIntegers").setType("INTEGER,multiple=true,values=1;2;3").setDescription("My Integers").setDefaultValue("1");
    dao.insertRuleParam(dbSession, templateRule, ruleParamDto);
    dbSession.commit();
    ruleIndexer.index();
    return templateRule;
  }

  private RuleDto createTemplateRuleWithTwoIntParams() {
    RuleDto templateRule = RuleTesting.newDto(RuleKey.of("java", "S003"))
      .setIsTemplate(true)
      .setLanguage("java")
      .setConfigKey("S003")
      .setDefaultRemediationFunction(DebtRemediationFunction.Type.LINEAR_OFFSET.name())
      .setDefaultRemediationGapMultiplier("1h")
      .setDefaultRemediationBaseEffort("5min")
      .setGapDescription("desc")
      .setCreatedAt(new Date().getTime())
      .setUpdatedAt(new Date().getTime());
    dao.insert(dbSession, templateRule);
    RuleParamDto ruleParam1Dto = RuleParamDto.createFor(templateRule)
      .setName("first").setType("INTEGER").setDescription("First integer").setDefaultValue("0");
    dao.insertRuleParam(dbSession, templateRule, ruleParam1Dto);
    RuleParamDto ruleParam2Dto = RuleParamDto.createFor(templateRule)
      .setName("second").setType("INTEGER").setDescription("Second integer").setDefaultValue("0");
    dao.insertRuleParam(dbSession, templateRule, ruleParam2Dto);
    dbSession.commit();
    return templateRule;
  }

}
