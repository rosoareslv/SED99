/*
 * SonarQube, open source software quality management tool.
 * Copyright (C) 2008-2014 SonarSource
 * mailto:contact AT sonarsource DOT com
 *
 * SonarQube is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * SonarQube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.server.computation.issue;

import com.google.common.base.Optional;
import java.util.HashMap;
import java.util.Map;
import org.junit.rules.ExternalResource;
import org.sonar.api.rule.RuleKey;

import static com.google.common.base.Preconditions.checkArgument;
import static java.util.Objects.requireNonNull;

public class RuleRepositoryRule extends ExternalResource implements RuleRepository {

  private final Map<RuleKey, Rule> rulesByKey = new HashMap<>();
  private final Map<Integer, Rule> rulesById = new HashMap<>();

  @Override
  protected void after() {
    rulesByKey.clear();
    rulesById.clear();
  }

  @Override
  public Rule getByKey(RuleKey key) {
    Rule rule = rulesByKey.get(requireNonNull(key));
    checkArgument(rule != null);
    return rule;
  }

  @Override
  public Rule getById(int id) {
    Rule rule = rulesById.get(id);
    checkArgument(rule != null);
    return rule;
  }

  @Override
  public Optional<Rule> findByKey(RuleKey key) {
    return Optional.fromNullable(rulesByKey.get(requireNonNull(key)));
  }

  @Override
  public Optional<Rule> findById(int id) {
    return Optional.fromNullable(rulesById.get(id));
  }

  public DumbRule add(RuleKey key) {
    DumbRule rule = new DumbRule(key);
    rule.setId(key.hashCode());
    rulesByKey.put(key, rule);
    rulesById.put(rule.getId(), rule);
    return rule;
  }

  public RuleRepositoryRule add(DumbRule rule) {
    rulesByKey.put(requireNonNull(rule.getKey()), rule);
    rulesById.put(requireNonNull(rule.getId()), rule);
    return this;
  }

}
