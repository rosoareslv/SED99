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

import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ListMultimap;
import com.google.common.collect.Multimaps;
import java.security.MessageDigest;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import javax.annotation.Nullable;
import org.apache.commons.codec.digest.DigestUtils;
import org.sonar.api.profiles.ProfileDefinition;
import org.sonar.api.profiles.RulesProfile;
import org.sonar.api.resources.Languages;
import org.sonar.api.utils.ValidationMessages;
import org.sonar.api.utils.log.Logger;
import org.sonar.api.utils.log.Loggers;
import org.sonar.api.utils.log.Profiler;
import org.sonar.core.util.stream.MoreCollectors;

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkState;
import static org.apache.commons.lang.StringUtils.isNotEmpty;
import static org.apache.commons.lang.StringUtils.lowerCase;

public class DefinedQProfileRepositoryImpl implements DefinedQProfileRepository {
  private static final Logger LOGGER = Loggers.get(DefinedQProfileRepositoryImpl.class);
  private static final String DEFAULT_PROFILE_NAME = "Sonar way";

  private final Languages languages;
  private final List<ProfileDefinition> definitions;
  private Map<String, List<DefinedQProfile>> qProfilesByLanguage;

  /**
   * Requires for pico container when no {@link ProfileDefinition} is defined at all
   */
  public DefinedQProfileRepositoryImpl(Languages languages) {
    this(languages, new ProfileDefinition[0]);
  }

  public DefinedQProfileRepositoryImpl(Languages languages, ProfileDefinition... definitions) {
    this.languages = languages;
    this.definitions = ImmutableList.copyOf(definitions);
  }

  @Override
  public void initialize() {
    checkState(qProfilesByLanguage == null, "initialize must be called only once");

    Profiler profiler = Profiler.create(Loggers.get(getClass())).startInfo("Load quality profiles");
    ListMultimap<String, RulesProfile> rulesProfilesByLanguage = buildRulesProfilesByLanguage();
    validateAndClean(rulesProfilesByLanguage);
    this.qProfilesByLanguage = toQualityProfilesByLanguage(rulesProfilesByLanguage);
    profiler.stopDebug();
  }

  @Override
  public Map<String, List<DefinedQProfile>> getQProfilesByLanguage() {
    checkState(qProfilesByLanguage != null, "initialize must be called first");

    return qProfilesByLanguage;
  }

  /**
   * @return profiles by language
   */
  private ListMultimap<String, RulesProfile> buildRulesProfilesByLanguage() {
    ListMultimap<String, RulesProfile> byLang = ArrayListMultimap.create();
    for (ProfileDefinition definition : definitions) {
      ValidationMessages validation = ValidationMessages.create();
      RulesProfile profile = definition.createProfile(validation);
      validation.log(LOGGER);
      if (profile != null && !validation.hasErrors()) {
        checkArgument(isNotEmpty(profile.getName()), "Profile created by Definition %s can't have a blank name", definition);
        byLang.put(lowerCase(profile.getLanguage(), Locale.ENGLISH), profile);
      }
    }
    return byLang;
  }

  private void validateAndClean(ListMultimap<String, RulesProfile> byLang) {
    byLang.asMap().entrySet()
      .removeIf(entry -> {
        String language = entry.getKey();
        if (languages.get(language) == null) {
          LOGGER.info("Language {} is not installed, related Quality profiles are ignored", language);
          return true;
        }
        Collection<RulesProfile> profiles = entry.getValue();
        if (profiles.isEmpty()) {
          LOGGER.warn("No Quality profiles defined for language: {}", language);
          return true;
        }
        return false;
      });
  }

  private Map<String, List<DefinedQProfile>> toQualityProfilesByLanguage(ListMultimap<String, RulesProfile> rulesProfilesByLanguage) {
    Map<String, List<DefinedQProfile.Builder>> buildersByLanguage = Multimaps.asMap(rulesProfilesByLanguage)
      .entrySet()
      .stream()
      .collect(MoreCollectors.uniqueIndex(Map.Entry::getKey, DefinedQProfileRepositoryImpl::toQualityProfileBuilders));
    return buildersByLanguage
      .entrySet()
      .stream()
      .filter(DefinedQProfileRepositoryImpl::ensureAtMostOneDeclaredDefault)
      .collect(MoreCollectors.uniqueIndex(Map.Entry::getKey, entry -> toQualityProfiles(entry.getValue()), buildersByLanguage.size()));
  }

  /**
   * Creates {@link DefinedQProfile.Builder} for each unique quality profile name for a given language.
   * Builders will have the following properties populated:
   * <ul>
   *   <li>{@link DefinedQProfile.Builder#language language}: key of the method's parameter</li>
   *   <li>{@link DefinedQProfile.Builder#name name}: {@link RulesProfile#getName()}</li>
   *   <li>{@link DefinedQProfile.Builder#declaredDefault declaredDefault}: {@code true} if at least one RulesProfile
   *       with a given name has {@link RulesProfile#getDefaultProfile()} is {@code true}</li>
   *   <li>{@link DefinedQProfile.Builder#activeRules activeRules}: the concatenate of the active rules of all
   *       RulesProfile with a given name</li>
   * </ul>
   */
  private static List<DefinedQProfile.Builder> toQualityProfileBuilders(Map.Entry<String, List<RulesProfile>> rulesProfilesByLanguage) {
    String language = rulesProfilesByLanguage.getKey();
    // use a LinkedHashMap to keep order of insertion of RulesProfiles
    Map<String, DefinedQProfile.Builder> qualityProfileBuildersByName = new LinkedHashMap<>();
    for (RulesProfile rulesProfile : rulesProfilesByLanguage.getValue()) {
      qualityProfileBuildersByName.compute(
        rulesProfile.getName(),
        (name, existingBuilder) -> updateOrCreateBuilder(language, existingBuilder, rulesProfile, name));
    }
    return ImmutableList.copyOf(qualityProfileBuildersByName.values());
  }

  /**
   * Fails if more than one {@link DefinedQProfile.Builder#declaredDefault} is {@code true}, otherwise returns {@code true}.
   */
  private static boolean ensureAtMostOneDeclaredDefault(Map.Entry<String, List<DefinedQProfile.Builder>> entry) {
    Set<String> declaredDefaultProfileNames = entry.getValue().stream()
      .filter(DefinedQProfile.Builder::isDeclaredDefault)
      .map(DefinedQProfile.Builder::getName)
      .collect(MoreCollectors.toSet());
    checkState(declaredDefaultProfileNames.size() <= 1, "Several Quality profiles are flagged as default for the language %s: %s", entry.getKey(), declaredDefaultProfileNames);
    return true;
  }

  private static DefinedQProfile.Builder updateOrCreateBuilder(String language, @Nullable DefinedQProfile.Builder existingBuilder, RulesProfile rulesProfile, String name) {
    DefinedQProfile.Builder builder = existingBuilder;
    if (builder == null) {
      builder = new DefinedQProfile.Builder()
        .setLanguage(language)
        .setName(name);
    }
    Boolean defaultProfile = rulesProfile.getDefaultProfile();
    boolean declaredDefault = defaultProfile != null && defaultProfile;
    return builder
      // if there is multiple RulesProfiles with the same name, if at least one is declared default,
      // then QualityProfile is flagged as declared default
      .setDeclaredDefault(builder.isDeclaredDefault() || declaredDefault)
      .addRules(rulesProfile.getActiveRules());
  }

  private static List<DefinedQProfile> toQualityProfiles(List<DefinedQProfile.Builder> builders) {
    if (builders.stream().noneMatch(DefinedQProfile.Builder::isDeclaredDefault)) {
      Optional<DefinedQProfile.Builder> sonarWayProfile = builders.stream().filter(builder -> builder.getName().equals(DEFAULT_PROFILE_NAME)).findFirst();
      if (sonarWayProfile.isPresent()) {
        sonarWayProfile.get().setComputedDefault(true);
      } else {
        builders.iterator().next().setComputedDefault(true);
      }
    }
    MessageDigest md5Digest = DigestUtils.getMd5Digest();
    return builders.stream()
      .map(builder -> builder.build(md5Digest))
      .collect(MoreCollectors.toList(builders.size()));
  }
}
