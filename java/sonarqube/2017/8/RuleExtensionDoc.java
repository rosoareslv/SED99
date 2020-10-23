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
package org.sonar.server.rule.index;

import com.google.common.collect.Maps;
import java.util.Map;
import java.util.Set;
import org.apache.commons.lang.builder.ReflectionToStringBuilder;
import org.sonar.api.rule.RuleKey;
import org.sonar.db.rule.RuleExtensionForIndexingDto;
import org.sonar.db.rule.RuleForIndexingDto;
import org.sonar.server.es.BaseDoc;

public class RuleExtensionDoc extends BaseDoc {

  public RuleExtensionDoc(Map<String, Object> fields) {
    super(fields);
  }

  public RuleExtensionDoc() {
    super(Maps.newHashMapWithExpectedSize(4));
  }

  @Override
  public String getId() {
    return idOf(getRuleKey(), getScope());
  }

  @Override
  public String getRouting() {
    return getRuleKey().toString();
  }

  @Override
  public String getParent() {
    return getRuleKey().toString();
  }

  public RuleKey getRuleKey() {
    return RuleKey.parse(getField(RuleIndexDefinition.FIELD_RULE_EXTENSION_RULE_KEY));
  }

  public RuleExtensionDoc setRuleKey(RuleKey ruleKey) {
    setField(RuleIndexDefinition.FIELD_RULE_EXTENSION_RULE_KEY, String.valueOf(ruleKey));
    return this;
  }

  public RuleExtensionScope getScope() {
    return RuleExtensionScope.parse(getField(RuleIndexDefinition.FIELD_RULE_EXTENSION_SCOPE));
  }

  public RuleExtensionDoc setScope(RuleExtensionScope scope) {
    setField(RuleIndexDefinition.FIELD_RULE_EXTENSION_SCOPE, scope.getScope());
    return this;
  }

  public Set<String> getTags() {
    return getField(RuleIndexDefinition.FIELD_RULE_EXTENSION_TAGS);
  }

  public RuleExtensionDoc setTags(Set<String> tags) {
    setField(RuleIndexDefinition.FIELD_RULE_EXTENSION_TAGS, tags);
    return this;
  }

  public static RuleExtensionDoc of(RuleForIndexingDto rule) {
    return new RuleExtensionDoc()
      .setRuleKey(rule.getRuleKey())
      .setScope(RuleExtensionScope.system())
      .setTags(rule.getSystemTagsAsSet());
  }

  public static RuleExtensionDoc of(RuleExtensionForIndexingDto rule) {
    return new RuleExtensionDoc()
      .setRuleKey(rule.getRuleKey())
      .setScope(RuleExtensionScope.organization(rule.getOrganizationUuid()))
      .setTags(rule.getTagsAsSet());
  }

  public static String idOf(RuleKey ruleKey, RuleExtensionScope scope) {
    return ruleKey + "|" + scope.getScope();
  }

  @Override
  public String toString() {
    return ReflectionToStringBuilder.toString(this);
  }
}
