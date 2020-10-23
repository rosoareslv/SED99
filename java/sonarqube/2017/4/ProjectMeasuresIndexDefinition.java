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
package org.sonar.server.measure.index;

import org.sonar.api.config.Settings;
import org.sonar.server.es.IndexDefinition;
import org.sonar.server.es.IndexType;
import org.sonar.server.es.NewIndex;

import static org.sonar.server.es.DefaultIndexSettingsElement.SEARCH_GRAMS_ANALYZER;
import static org.sonar.server.es.DefaultIndexSettingsElement.SORTABLE_ANALYZER;

public class ProjectMeasuresIndexDefinition implements IndexDefinition {

  public static final IndexType INDEX_TYPE_PROJECT_MEASURES = new IndexType("projectmeasures", "projectmeasure");
  public static final String FIELD_ORGANIZATION_UUID = "organizationUuid";
  public static final String FIELD_KEY = "key";
  public static final String FIELD_NAME = "name";
  public static final String FIELD_ANALYSED_AT = "analysedAt";
  public static final String FIELD_QUALITY_GATE_STATUS = "qualityGateStatus";
  public static final String FIELD_TAGS = "tags";
  public static final String FIELD_MEASURES = "measures";
  public static final String FIELD_MEASURES_KEY = "key";
  public static final String FIELD_MEASURES_VALUE = "value";
  public static final String FIELD_LANGUAGES = "languages";

  private final Settings settings;

  public ProjectMeasuresIndexDefinition(Settings settings) {
    this.settings = settings;
  }

  @Override
  public void define(IndexDefinitionContext context) {
    NewIndex index = context.create(INDEX_TYPE_PROJECT_MEASURES.getIndex());
    index.refreshHandledByIndexer();
    index.configureShards(settings, 5);

    NewIndex.NewIndexType mapping = index.createType(INDEX_TYPE_PROJECT_MEASURES.getType())
      .requireProjectAuthorization();

    mapping.stringFieldBuilder(FIELD_ORGANIZATION_UUID).build();
    mapping.stringFieldBuilder(FIELD_KEY).disableNorms().addSubFields(SORTABLE_ANALYZER).build();
    mapping.stringFieldBuilder(FIELD_NAME).addSubFields(SORTABLE_ANALYZER, SEARCH_GRAMS_ANALYZER).build();
    mapping.stringFieldBuilder(FIELD_QUALITY_GATE_STATUS).build();
    mapping.stringFieldBuilder(FIELD_TAGS).build();
    mapping.stringFieldBuilder(FIELD_LANGUAGES).build();
    mapping.nestedFieldBuilder(FIELD_MEASURES)
      .addStringField(FIELD_MEASURES_KEY)
      .addDoubleField(FIELD_MEASURES_VALUE)
      .build();
    mapping.createDateTimeField(FIELD_ANALYSED_AT);
    mapping.setEnableSource(false);
  }
}
