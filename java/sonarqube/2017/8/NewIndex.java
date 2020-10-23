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
import org.sonar.api.config.Configuration;
import org.sonar.process.ProcessProperties;
import org.sonar.server.permission.index.AuthorizationTypeSupport;

import static com.google.common.base.Preconditions.checkArgument;
import static java.lang.String.format;
import static java.lang.String.valueOf;
import static java.util.Objects.requireNonNull;
import static org.sonar.server.es.DefaultIndexSettings.ANALYZER;
import static org.sonar.server.es.DefaultIndexSettings.FIELDDATA_ENABLED;
import static org.sonar.server.es.DefaultIndexSettings.FIELD_FIELDDATA;
import static org.sonar.server.es.DefaultIndexSettings.FIELD_TERM_VECTOR;
import static org.sonar.server.es.DefaultIndexSettings.FIELD_TYPE_KEYWORD;
import static org.sonar.server.es.DefaultIndexSettings.FIELD_TYPE_TEXT;
import static org.sonar.server.es.DefaultIndexSettings.INDEX;
import static org.sonar.server.es.DefaultIndexSettings.INDEX_NOT_SEARCHABLE;
import static org.sonar.server.es.DefaultIndexSettings.INDEX_SEARCHABLE;
import static org.sonar.server.es.DefaultIndexSettings.TYPE;
import static org.sonar.server.es.DefaultIndexSettingsElement.UUID_MODULE_ANALYZER;

public class NewIndex {

  private final String indexName;
  private final Settings.Builder settings = DefaultIndexSettings.defaults();
  private final Map<String, NewIndexType> types = new LinkedHashMap<>();

  NewIndex(String indexName, SettingsConfiguration settingsConfiguration) {
    checkArgument(StringUtils.isAllLowerCase(indexName), "Index name must be lower-case: " + indexName);
    this.indexName = indexName;
    applySettingsConfiguration(settingsConfiguration);
  }

  private void applySettingsConfiguration(SettingsConfiguration settingsConfiguration) {
    settings.put("index.mapper.dynamic", valueOf(false));
    settings.put("index.refresh_interval", refreshInterval(settingsConfiguration));

    Configuration config = settingsConfiguration.getConfiguration();
    boolean clusterMode = config.getBoolean(ProcessProperties.CLUSTER_ENABLED).orElse(false);
    int shards = config.getInt(format("sonar.search.%s.shards", indexName))
      .orElse(settingsConfiguration.getDefaultNbOfShards());
    int replicas = clusterMode ? config.getInt(ProcessProperties.SEARCH_REPLICAS).orElse(1) : 0;

    settings.put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, shards);
    settings.put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, replicas);
  }

  private static String refreshInterval(SettingsConfiguration settingsConfiguration) {
    int refreshInterval = settingsConfiguration.getRefreshInterval();
    if (refreshInterval == -1) {
      return "-1";
    }
    return refreshInterval + "s";
  }

  public static class SettingsConfiguration {
    public static final int MANUAL_REFRESH_INTERVAL = -1;

    private final Configuration configuration;
    private final int defaultNbOfShards;
    private final int refreshInterval;

    private SettingsConfiguration(Builder builder) {
      this.configuration = builder.configuration;
      this.defaultNbOfShards = builder.defaultNbOfShards;
      this.refreshInterval = builder.refreshInterval;
    }

    public static Builder newBuilder(Configuration configuration) {
      return new Builder(configuration);
    }

    public Configuration getConfiguration() {
      return configuration;
    }

    public int getDefaultNbOfShards() {
      return defaultNbOfShards;
    }

    public int getRefreshInterval() {
      return refreshInterval;
    }

    public static class Builder {
      private final Configuration configuration;
      private int defaultNbOfShards = 1;
      private int refreshInterval = 30;

      public Builder(Configuration configuration) {
        this.configuration = requireNonNull(configuration, "configuration can't be null");
      }

      public Builder setDefaultNbOfShards(int defaultNbOfShards) {
        checkArgument(defaultNbOfShards >= 1, "defaultNbOfShards must be >= 1");
        this.defaultNbOfShards = defaultNbOfShards;
        return this;
      }

      public Builder setRefreshInterval(int refreshInterval) {
        checkArgument(refreshInterval == -1 || refreshInterval > 0,
          "refreshInterval must be either -1 or strictly positive");
        this.refreshInterval = refreshInterval;
        return this;
      }

      public SettingsConfiguration build() {
        return new SettingsConfiguration(this);
      }
    }

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

    public KeywordFieldBuilder keywordFieldBuilder(String fieldName) {
      return new KeywordFieldBuilder(this, fieldName);
    }

    public TextFieldBuilder textFieldBuilder(String fieldName) {
      return new TextFieldBuilder(this, fieldName);
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
      return setProperty(fieldName, ImmutableMap.of("type", "date", "format", "date_time||epoch_second"));
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
        TYPE, FIELD_TYPE_TEXT,
        INDEX, DefaultIndexSettings.INDEX_SEARCHABLE,
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
  public abstract static class StringFieldBuilder {
    private final NewIndexType indexType;
    private final String fieldName;
    private boolean disableSearch = false;
    private boolean disableNorms = false;
    private boolean termVectorWithPositionOffsets = false;
    private SortedMap<String, Object> subFields = Maps.newTreeMap();
    private boolean store = false;

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
     * "index: false" -> Make this field not searchable.
     * By default field is "true": it is searchable, but index the value exactly
     * as specified.
     */
    public StringFieldBuilder disableSearch() {
      this.disableSearch = true;
      return this;
    }

    public StringFieldBuilder store() {
      this.store = true;
      return this;
    }

    public NewIndexType build() {
      if (subFields.isEmpty()) {
        return buildWithoutSubfields();
      }
      return buildWithSubfields();
    }

    private NewIndexType buildWithoutSubfields() {
      Map<String, Object> hash = new TreeMap<>();
      hash.putAll(ImmutableMap.of(
        "type", getFieldType(),
        INDEX, disableSearch ? INDEX_NOT_SEARCHABLE : INDEX_SEARCHABLE,
        "norms", valueOf(!disableNorms),
        "store", valueOf(store)));
      if (getFieldData()) {
        hash.put(FIELD_FIELDDATA, FIELDDATA_ENABLED);
      }
      return indexType.setProperty(fieldName, hash);
    }

    private NewIndexType buildWithSubfields() {
      Map<String, Object> hash = new TreeMap<>();
      hash.put("type", getFieldType());

      Map<String, Object> multiFields = new TreeMap<>(subFields);

      if (termVectorWithPositionOffsets) {
        multiFields.entrySet().forEach(entry -> {
          Object subFieldMapping = entry.getValue();
          if (subFieldMapping instanceof Map) {
            entry.setValue(
              addFieldToMapping(
                (Map<String, String>) subFieldMapping,
                FIELD_TERM_VECTOR, "with_positions_offsets"));
          }
        });
        hash.put(FIELD_TERM_VECTOR, "with_positions_offsets");
      }
      if (getFieldData()) {
        multiFields.entrySet().forEach(entry -> {
          Object subFieldMapping = entry.getValue();
          if (subFieldMapping instanceof Map) {
            entry.setValue(
              addFieldToMapping(
                (Map<String, String>) subFieldMapping,
                FIELD_FIELDDATA, FIELDDATA_ENABLED));
          }
        });
        hash.put(FIELD_FIELDDATA, FIELDDATA_ENABLED);
      }

      multiFields.put(fieldName, ImmutableMap.of(
        "type", getFieldType(),
        INDEX, INDEX_SEARCHABLE,
        "norms", "false",
        "store", valueOf(store)));
      hash.put("fields", multiFields);

      return indexType.setProperty(fieldName, hash);
    }

    protected abstract boolean getFieldData();

    protected abstract String getFieldType();

    private static SortedMap<String, String> addFieldToMapping(Map<String, String> source, String key, String value) {
      SortedMap<String, String> mutable = new TreeMap<>(source);
      mutable.put(key, value);
      return ImmutableSortedMap.copyOf(mutable);
    }
  }

  public static class KeywordFieldBuilder extends StringFieldBuilder {

    private KeywordFieldBuilder(NewIndexType indexType, String fieldName) {
      super(indexType, fieldName);
    }

    @Override
    protected boolean getFieldData() {
      return false;
    }

    protected String getFieldType() {
      return FIELD_TYPE_KEYWORD;
    }
  }

  public static class TextFieldBuilder extends StringFieldBuilder {

    private boolean fieldData = false;

    private TextFieldBuilder(NewIndexType indexType, String fieldName) {
      super(indexType, fieldName);
    }

    protected String getFieldType() {
      return FIELD_TYPE_TEXT;
    }

    /**
     * Required to enable sorting, aggregation and access to field data on fields of type "text".
     * <p>Disabled by default as this can have significant memory cost</p>
     */
    public StringFieldBuilder withFieldData() {
      this.fieldData = true;
      return this;
    }

    @Override
    protected boolean getFieldData() {
      return fieldData;
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

    public NestedFieldBuilder addKeywordField(String fieldName) {
      return setProperty(fieldName, ImmutableMap.of(
        "type", FIELD_TYPE_KEYWORD,
        INDEX, INDEX_SEARCHABLE));
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
