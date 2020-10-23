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
package org.sonar.server.es;

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSortedMap;
import com.google.common.collect.Maps;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.SortedMap;
import java.util.TreeMap;
import javax.annotation.CheckForNull;
import org.apache.commons.lang.StringUtils;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.settings.Settings;
import org.sonar.process.ProcessProperties;
import org.sonar.server.permission.index.AuthorizationTypeSupport;

import static com.google.common.base.Preconditions.checkArgument;
import static java.lang.String.format;
import static org.sonar.server.es.DefaultIndexSettings.ANALYZED;
import static org.sonar.server.es.DefaultIndexSettings.ANALYZER;
import static org.sonar.server.es.DefaultIndexSettings.INDEX;
import static org.sonar.server.es.DefaultIndexSettings.STRING;
import static org.sonar.server.es.DefaultIndexSettings.TYPE;
import static org.sonar.server.es.DefaultIndexSettingsElement.UUID_MODULE_ANALYZER;

public class NewIndex {

  private final String indexName;
  private final Settings.Builder settings = DefaultIndexSettings.defaults();
  private final Map<String, NewIndexType> types = new LinkedHashMap<>();

  NewIndex(String indexName) {
    Preconditions.checkArgument(StringUtils.isAllLowerCase(indexName), "Index name must be lower-case: " + indexName);
    this.indexName = indexName;
  }

  public void refreshHandledByIndexer() {
    getSettings().put("index.refresh_interval", "-1");
  }

  public String getName() {
    return indexName;
  }

  public Settings.Builder getSettings() {
    return settings;
  }

  public NewIndexType createType(String typeName) {
    NewIndexType type = new NewIndexType(this, typeName);
    types.put(typeName, type);
    return type;
  }

  public Map<String, NewIndexType> getTypes() {
    return types;
  }

  public void configureShards(org.sonar.api.config.Settings settings, int defaultNbOfShards) {
    boolean clusterMode = settings.getBoolean(ProcessProperties.CLUSTER_ENABLED);
    int shards = settings.getInt(format("sonar.search.%s.shards", indexName));
    if (shards == 0) {
      shards = defaultNbOfShards;
    }
    int replicas = settings.getInt(format("sonar.search.%s.replicas", indexName));
    if (replicas == 0) {
      replicas = clusterMode ? 1 : 0;
    }
    getSettings().put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, shards);
    getSettings().put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, replicas);
  }

  public static class NewIndexType {
    private final NewIndex index;
    private final String name;
    private final Map<String, Object> attributes = new TreeMap<>();
    private final Map<String, Object> properties = new TreeMap<>();

    private NewIndexType(NewIndex index, String typeName) {
      this.index = index;
      this.name = typeName;
      // defaults
      attributes.put("dynamic", false);
      attributes.put("_all", ImmutableSortedMap.of("enabled", false));
      attributes.put("_source", ImmutableSortedMap.of("enabled", true));
      attributes.put("properties", properties);
    }

    public NewIndexType requireProjectAuthorization() {
      AuthorizationTypeSupport.enableProjectAuthorization(this);
      return this;
    }

    public NewIndex getIndex() {
      return index;
    }

    public String getName() {
      return name;
    }

    /**
     * Complete the root json hash of mapping type, for example to set the attribute "_id"
     */
    public NewIndexType setAttribute(String key, Object value) {
      attributes.put(key, value);
      return this;
    }

    /**
     * Complete the json hash named "properties" in mapping type, usually to declare fields
     */
    public NewIndexType setProperty(String key, Object value) {
      properties.put(key, value);
      return this;
    }

    public NewIndexType setEnableSource(boolean enableSource) {
      attributes.put("_source", ImmutableSortedMap.of("enabled", enableSource));
      return this;
    }

    public StringFieldBuilder stringFieldBuilder(String fieldName) {
      return new StringFieldBuilder(this, fieldName);
    }

    public NestedFieldBuilder nestedFieldBuilder(String fieldName) {
      return new NestedFieldBuilder(this, fieldName);
    }

    public NewIndexType createBooleanField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "boolean"));
    }

    public NewIndexType createByteField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "byte"));
    }

    public NewIndexType createDateTimeField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "date", "format", "date_time"));
    }

    public NewIndexType createDoubleField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "double"));
    }

    public NewIndexType createIntegerField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "integer"));
    }

    public NewIndexType createLongField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "long"));
    }

    public NewIndexType createShortField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "short"));
    }

    public NewIndexType createUuidPathField(String fieldName) {
      return setProperty(fieldName, ImmutableSortedMap.of(
        TYPE, STRING,
        INDEX, ANALYZED,
        ANALYZER, UUID_MODULE_ANALYZER.getName()));
    }

    public Map<String, Object> getAttributes() {
      return attributes;
    }

    @CheckForNull
    public Object getProperty(String key) {
      return properties.get(key);
    }
  }

  /**
   * Helper to define a string field in mapping of index type
   */
  public static class StringFieldBuilder {
    private final NewIndexType indexType;
    private final String fieldName;
    private boolean disableSearch = false;
    private boolean disableNorms = false;
    private boolean termVectorWithPositionOffsets = false;
    private SortedMap<String, Object> subFields = Maps.newTreeMap();

    private StringFieldBuilder(NewIndexType indexType, String fieldName) {
      this.indexType = indexType;
      this.fieldName = fieldName;
    }

    /**
     * Add a sub-field. A {@code SortedMap} is required for consistency of the index settings hash.
     * @see IndexDefinitionHash
     */
    private StringFieldBuilder addSubField(String fieldName, SortedMap<String, String> fieldDefinition) {
      subFields.put(fieldName, fieldDefinition);
      return this;
    }

    /**
     * Add subfields, one for each analyzer.
     */
    public StringFieldBuilder addSubFields(DefaultIndexSettingsElement... analyzers) {
      Arrays.stream(analyzers)
        .forEach(analyzer -> addSubField(analyzer.getSubFieldSuffix(), analyzer.fieldMapping()));
      return this;
    }

    /**
     * Norms consume useless memory if string field is used for filtering or aggregations.
     *
     * https://www.elastic.co/guide/en/elasticsearch/reference/2.3/norms.html
     * https://www.elastic.co/guide/en/elasticsearch/guide/current/scoring-theory.html#field-norm
     */
    public StringFieldBuilder disableNorms() {
      this.disableNorms = true;
      return this;
    }

    /**
     * Position offset term vectors are required for the fast_vector_highlighter (fvh).
     */
    public StringFieldBuilder termVectorWithPositionOffsets() {
      this.termVectorWithPositionOffsets = true;
      return this;
    }

    /**
     * "index: no" -> Don’t index this field at all. This field will not be searchable.
     * By default field is "not_analyzed": it is searchable, but index the value exactly
     * as specified.
     */
    public StringFieldBuilder disableSearch() {
      this.disableSearch = true;
      return this;
    }

    public NewIndexType build() {
      Map<String, Object> hash = new TreeMap<>();
      if (subFields.isEmpty()) {
        hash.putAll(ImmutableMap.of(
          "type", "string",
          "index", disableSearch ? "no" : "not_analyzed",
          "norms", ImmutableMap.of("enabled", String.valueOf(!disableNorms))));
      } else {
        hash.put("type", "multi_field");

        Map<String, Object> multiFields = new TreeMap<>(subFields);

        if (termVectorWithPositionOffsets) {
          multiFields.entrySet().forEach(entry -> {
            Object subFieldMapping = entry.getValue();
            if (subFieldMapping instanceof Map) {
              entry.setValue(
                addFieldToMapping(
                  (Map<String, String>) subFieldMapping,
                  "term_vector", "with_positions_offsets"));
            }
          });
        }

        multiFields.put(fieldName, ImmutableMap.of(
          "type", "string",
          "index", "not_analyzed",
          "term_vector", termVectorWithPositionOffsets ? "with_positions_offsets" : "no",
          "norms", ImmutableMap.of("enabled", "false")));

        hash.put("fields", multiFields);
      }

      return indexType.setProperty(fieldName, hash);
    }

    private static SortedMap<String, String> addFieldToMapping(Map<String, String> source, String key, String value) {
      SortedMap<String, String> mutable = new TreeMap<>(source);
      mutable.put(key, value);
      return ImmutableSortedMap.copyOf(mutable);
    }
  }

  public static class NestedFieldBuilder {
    private final NewIndexType indexType;
    private final String fieldName;
    private final Map<String, Object> properties = new TreeMap<>();

    private NestedFieldBuilder(NewIndexType indexType, String fieldName) {
      this.indexType = indexType;
      this.fieldName = fieldName;
    }

    private NestedFieldBuilder setProperty(String fieldName, Object value) {
      properties.put(fieldName, value);

      return this;
    }

    public NestedFieldBuilder addStringField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of(
        "type", "string",
        "index", "not_analyzed"));
    }

    public NestedFieldBuilder addDoubleField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "double"));
    }

    public NestedFieldBuilder addIntegerField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of("type", "integer"));
    }

    public NewIndexType build() {
      checkArgument(!properties.isEmpty(), "At least one sub-field must be declared in nested property '%s'", fieldName);
      Map<String, Object> hash = new TreeMap<>();
      hash.put("type", "nested");
      hash.put("properties", properties);

      return indexType.setProperty(fieldName, hash);
    }
  }

}
