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
package org.sonar.db.rule;

import java.util.Set;
import javax.annotation.CheckForNull;
import javax.annotation.Nullable;
import org.apache.commons.lang.builder.EqualsBuilder;
import org.apache.commons.lang.builder.HashCodeBuilder;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.rule.RuleStatus;
import org.sonar.api.rules.RuleType;

public class RuleDto {

  public enum Format {
    HTML, MARKDOWN
  }

  private final RuleDefinitionDto definition;
  private final RuleMetadataDto metadata;

  public RuleDto() {
    this(new RuleDefinitionDto(), new RuleMetadataDto());
  }

  public RuleDto(RuleDefinitionDto definition, RuleMetadataDto metadata) {
    this.definition = definition;
    this.metadata = metadata;
  }

  public RuleDefinitionDto getDefinition() {
    return definition;
  }

  public RuleMetadataDto getMetadata() {
    return metadata;
  }

  public RuleKey getKey() {
    if (definition.getKey() == null) {
      definition.setKey(RuleKey.of(getRepositoryKey(), getRuleKey()));
    }
    return definition.getKey();
  }

  public Integer getId() {
    return definition.getId();
  }

  public RuleDto setId(Integer id) {
    definition.setId(id);
    metadata.setRuleId(id);
    return this;
  }

  public String getRepositoryKey() {
    return definition.getRepositoryKey();
  }

  public RuleDto setRepositoryKey(String s) {
    definition.setRepositoryKey(s);
    return this;
  }

  public String getRuleKey() {
    return definition.getRuleKey();
  }

  public RuleDto setRuleKey(String s) {
    definition.setRuleKey(s);
    return this;
  }

  @CheckForNull
  public String getPluginKey() {
    return definition.getPluginKey();
  }

  public RuleDto setPluginKey(@Nullable String s) {
    definition.setPluginKey(s);
    return this;
  }

  public String getDescription() {
    return definition.getDescription();
  }

  public RuleDto setDescription(String description) {
    definition.setDescription(description);
    return this;
  }

  public Format getDescriptionFormat() {
    return definition.getDescriptionFormat();
  }

  public RuleDto setDescriptionFormat(Format descriptionFormat) {
    definition.setDescriptionFormat(descriptionFormat);
    return this;
  }

  public RuleStatus getStatus() {
    return definition.getStatus();
  }

  public RuleDto setStatus(@Nullable RuleStatus s) {
    definition.setStatus(s);
    return this;
  }

  public String getName() {
    return definition.getName();
  }

  public RuleDto setName(@Nullable String s) {
    definition.setName(s);
    return this;
  }

  public String getConfigKey() {
    return definition.getConfigKey();
  }

  public RuleDto setConfigKey(@Nullable String configKey) {
    definition.setConfigKey(configKey);
    return this;
  }

  @CheckForNull
  public Integer getSeverity() {
    return definition.getSeverity();
  }

  @CheckForNull
  public String getSeverityString() {
    return definition.getSeverityString();
  }

  public RuleDto setSeverity(@Nullable String severity) {
    definition.setSeverity(severity);
    return this;
  }

  public RuleDto setSeverity(@Nullable Integer severity) {
    definition.setSeverity(severity);
    return this;
  }

  public boolean isTemplate() {
    return definition.isTemplate();
  }

  public RuleDto setIsTemplate(boolean isTemplate) {
    definition.setIsTemplate(isTemplate);
    return this;
  }

  @CheckForNull
  public String getLanguage() {
    return definition.getLanguage();
  }

  public RuleDto setLanguage(String language) {
    definition.setLanguage(language);
    return this;
  }

  @CheckForNull
  public Integer getTemplateId() {
    return definition.getTemplateId();
  }

  public RuleDto setTemplateId(@Nullable Integer templateId) {
    definition.setTemplateId(templateId);
    return this;
  }

  @CheckForNull
  public String getDefRemediationFunction() {
    return definition.getDefRemediationFunction();
  }

  public RuleDto setDefRemediationFunction(@Nullable String defaultRemediationFunction) {
    definition.setDefRemediationFunction(defaultRemediationFunction);
    return this;
  }

  @CheckForNull
  public String getDefRemediationGapMultiplier() {
    return definition.getDefRemediationGapMultiplier();
  }

  public RuleDto setDefRemediationGapMultiplier(@Nullable String defaultRemediationGapMultiplier) {
    definition.setDefRemediationGapMultiplier(defaultRemediationGapMultiplier);
    return this;
  }

  @CheckForNull
  public String getDefRemediationBaseEffort() {
    return definition.getDefRemediationBaseEffort();
  }

  public RuleDto setDefRemediationBaseEffort(@Nullable String defaultRemediationBaseEffort) {
    definition.setDefRemediationBaseEffort(defaultRemediationBaseEffort);
    return this;
  }

  @CheckForNull
  public String getGapDescription() {
    return definition.getGapDescription();
  }

  public RuleDto setGapDescription(@Nullable String s) {
    definition.setGapDescription(s);
    return this;
  }

  public RuleDto setSystemTags(Set<String> tags) {
    this.definition.setSystemTags(tags);
    return this;
  }

  public int getType() {
    return definition.getType();
  }

  public RuleDto setType(int type) {
    definition.setType(type);
    return this;
  }

  public RuleDto setType(RuleType type) {
    definition.setType(type);
    return this;
  }

  public Set<String> getSystemTags() {
    return definition.getSystemTags();
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setSystemTagsField(String s) {
    definition.setSystemTagsField(s);
  }

  public long getCreatedAt() {
    return definition.getCreatedAt();
  }

  public RuleDto setCreatedAt(long createdAt) {
    definition.setCreatedAt(createdAt);
    metadata.setCreatedAt(createdAt);
    return this;
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setCreatedAtFromDefinition(@Nullable Long createdAt) {
    if (createdAt != null && createdAt > definition.getCreatedAt()) {
      setCreatedAt(createdAt);
    }
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setCreatedAtFromMetadata(@Nullable Long createdAt) {
    if (createdAt != null && createdAt > definition.getCreatedAt()) {
      setCreatedAt(createdAt);
    }
  }

  public long getUpdatedAt() {
    return definition.getUpdatedAt();
  }

  public RuleDto setUpdatedAt(long updatedAt) {
    definition.setUpdatedAt(updatedAt);
    metadata.setUpdatedAt(updatedAt);
    return this;
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setUpdatedAtFromDefinition(@Nullable Long updatedAt) {
    if (updatedAt != null && updatedAt > definition.getUpdatedAt()) {
      setUpdatedAt(updatedAt);
    }
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setUpdatedAtFromMetadata(@Nullable Long updatedAt) {
    if (updatedAt != null && updatedAt > definition.getUpdatedAt()) {
      setUpdatedAt(updatedAt);
    }
  }

  public String getOrganizationUuid() {
    return metadata.getOrganizationUuid();
  }

  public RuleDto setOrganizationUuid(String organizationUuid) {
    metadata.setOrganizationUuid(organizationUuid);
    return this;
  }

  @CheckForNull
  public String getNoteData() {
    return metadata.getNoteData();
  }

  public RuleDto setNoteData(@Nullable String s) {
    metadata.setNoteData(s);
    return this;
  }

  @CheckForNull
  public String getNoteUserLogin() {
    return metadata.getNoteUserLogin();
  }

  public RuleDto setNoteUserLogin(@Nullable String noteUserLogin) {
    metadata.setNoteUserLogin(noteUserLogin);
    return this;
  }

  @CheckForNull
  public Long getNoteCreatedAt() {
    return metadata.getNoteCreatedAt();
  }

  public RuleDto setNoteCreatedAt(@Nullable Long noteCreatedAt) {
    metadata.setNoteCreatedAt(noteCreatedAt);
    return this;
  }

  @CheckForNull
  public Long getNoteUpdatedAt() {
    return metadata.getNoteUpdatedAt();
  }

  public RuleDto setNoteUpdatedAt(@Nullable Long noteUpdatedAt) {
    metadata.setNoteUpdatedAt(noteUpdatedAt);
    return this;
  }

  @CheckForNull
  public String getRemediationFunction() {
    return metadata.getRemediationFunction();
  }

  public RuleDto setRemediationFunction(@Nullable String remediationFunction) {
    metadata.setRemediationFunction(remediationFunction);
    return this;
  }

  @CheckForNull
  public String getRemediationGapMultiplier() {
    return metadata.getRemediationGapMultiplier();
  }

  public RuleDto setRemediationGapMultiplier(@Nullable String remediationGapMultiplier) {
    metadata.setRemediationGapMultiplier(remediationGapMultiplier);
    return this;
  }

  @CheckForNull
  public String getRemediationBaseEffort() {
    return metadata.getRemediationBaseEffort();
  }

  public RuleDto setRemediationBaseEffort(@Nullable String remediationBaseEffort) {
    metadata.setRemediationBaseEffort(remediationBaseEffort);
    return this;
  }

  public Set<String> getTags() {
    return metadata.getTags();
  }

  /**
   * Used in MyBatis mapping.
   */
  private void setTagsField(String s) {
    metadata.setTagsField(s);
  }

  public RuleDto setTags(Set<String> tags) {
    this.metadata.setTags(tags);
    return this;
  }

  @Override
  public boolean equals(Object obj) {
    if (!(obj instanceof RuleDto)) {
      return false;
    }
    if (this == obj) {
      return true;
    }
    RuleDto other = (RuleDto) obj;
    return new EqualsBuilder()
      .append(getRepositoryKey(), other.getRepositoryKey())
      .append(getRuleKey(), other.getRuleKey())
      .isEquals();
  }

  @Override
  public int hashCode() {
    return new HashCodeBuilder(17, 37)
      .append(getRepositoryKey())
      .append(getRuleKey())
      .toHashCode();
  }

  @Override
  public String toString() {
    return "RuleDto{" +
      "definition=" + definition +
      ", metadata=" + metadata +
      '}';
  }

  public static RuleDto createFor(RuleKey key) {
    return new RuleDto()
      .setRepositoryKey(key.repository())
      .setRuleKey(key.rule());
  }

}
