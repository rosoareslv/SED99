/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.index.analysis;

import com.carrotsearch.randomizedtesting.generators.RandomPicks;

import org.apache.lucene.analysis.MockTokenFilter;
import org.apache.lucene.analysis.TokenStream;
import org.apache.lucene.analysis.en.EnglishAnalyzer;
import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.analysis.tokenattributes.CharTermAttribute;
import org.elasticsearch.Version;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.env.Environment;
import org.elasticsearch.index.IndexSettings;
import org.elasticsearch.indices.analysis.AnalysisModule;
import org.elasticsearch.indices.analysis.AnalysisModule.AnalysisProvider;
import org.elasticsearch.indices.analysis.PreBuiltAnalyzers;
import org.elasticsearch.plugins.AnalysisPlugin;
import org.elasticsearch.test.ESTestCase;
import org.elasticsearch.test.IndexSettingsModule;
import org.elasticsearch.test.VersionUtils;

import java.io.IOException;
import java.util.Map;

import static java.util.Collections.emptyMap;
import static java.util.Collections.singletonList;
import static java.util.Collections.singletonMap;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.instanceOf;

public class AnalysisRegistryTests extends ESTestCase {

    private AnalysisRegistry registry;

    private static AnalyzerProvider<?> analyzerProvider(final String name) {
        return new PreBuiltAnalyzerProvider(name, AnalyzerScope.INDEX, new EnglishAnalyzer());
    }

    @Override
    public void setUp() throws Exception {
        super.setUp();
        Settings settings = Settings
            .builder()
            .put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString())
            .build();
        registry = new AnalysisRegistry(new Environment(settings),
            emptyMap(), emptyMap(), emptyMap(), emptyMap(), emptyMap());
    }

    public void testDefaultAnalyzers() throws IOException {
        Version version = VersionUtils.randomVersion(random());
        Settings settings = Settings
            .builder()
            .put(IndexMetaData.SETTING_VERSION_CREATED, version)
            .put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString())
            .build();
        IndexSettings idxSettings = IndexSettingsModule.newIndexSettings("index", settings);
        IndexAnalyzers indexAnalyzers = new AnalysisRegistry(new Environment(settings),
                emptyMap(), emptyMap(), emptyMap(), emptyMap(), emptyMap())
            .build(idxSettings);
        assertThat(indexAnalyzers.getDefaultIndexAnalyzer().analyzer(), instanceOf(StandardAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchAnalyzer().analyzer(), instanceOf(StandardAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchQuoteAnalyzer().analyzer(), instanceOf(StandardAnalyzer.class));
    }

    public void testOverrideDefaultAnalyzer() throws IOException {
        Version version = VersionUtils.randomVersion(random());
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        IndexAnalyzers indexAnalyzers = registry.build(IndexSettingsModule.newIndexSettings("index", settings),
            singletonMap("default", analyzerProvider("default"))
                , emptyMap(), emptyMap(), emptyMap(), emptyMap());
        assertThat(indexAnalyzers.getDefaultIndexAnalyzer().analyzer(), instanceOf(EnglishAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchAnalyzer().analyzer(), instanceOf(EnglishAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchQuoteAnalyzer().analyzer(), instanceOf(EnglishAnalyzer.class));
    }

    public void testOverrideDefaultIndexAnalyzerIsUnsupported() {
        Version version = VersionUtils.randomVersionBetween(random(), Version.V_5_0_0_alpha1, Version.CURRENT);
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        AnalyzerProvider<?> defaultIndex = new PreBuiltAnalyzerProvider("default_index", AnalyzerScope.INDEX, new EnglishAnalyzer());
        IllegalArgumentException e = expectThrows(IllegalArgumentException.class,
                () -> registry.build(IndexSettingsModule.newIndexSettings("index", settings),
                        singletonMap("default_index", defaultIndex), emptyMap(), emptyMap(), emptyMap(), emptyMap()));
        assertTrue(e.getMessage().contains("[index.analysis.analyzer.default_index] is not supported"));
    }

    public void testOverrideDefaultSearchAnalyzer() {
        Version version = VersionUtils.randomVersion(random());
        Settings settings = Settings.builder().put(IndexMetaData.SETTING_VERSION_CREATED, version).build();
        IndexAnalyzers indexAnalyzers = registry.build(IndexSettingsModule.newIndexSettings("index", settings),
                singletonMap("default_search", analyzerProvider("default_search")), emptyMap(), emptyMap(), emptyMap(), emptyMap());
        assertThat(indexAnalyzers.getDefaultIndexAnalyzer().analyzer(), instanceOf(StandardAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchAnalyzer().analyzer(), instanceOf(EnglishAnalyzer.class));
        assertThat(indexAnalyzers.getDefaultSearchQuoteAnalyzer().analyzer(), instanceOf(EnglishAnalyzer.class));
    }

    /**
     * Tests that {@code camelCase} filter names and {@code snake_case} filter names don't collide.
     */
    public void testConfigureCamelCaseTokenFilter() throws IOException {
        Settings settings = Settings.builder().put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString()).build();
        Settings indexSettings = Settings.builder()
                .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT)
                .put("index.analysis.filter.testFilter.type", "mock")
                .put("index.analysis.filter.test_filter.type", "mock")
                .put("index.analysis.analyzer.custom_analyzer_with_camel_case.tokenizer", "standard")
                .putArray("index.analysis.analyzer.custom_analyzer_with_camel_case.filter", "lowercase", "testFilter")
                .put("index.analysis.analyzer.custom_analyzer_with_snake_case.tokenizer", "standard")
                .putArray("index.analysis.analyzer.custom_analyzer_with_snake_case.filter", "lowercase", "test_filter").build();

        IndexSettings idxSettings = IndexSettingsModule.newIndexSettings("index", indexSettings);

        /* The snake_case version of the name should not filter out any stopwords while the
         * camelCase version will filter out English stopwords. */
        AnalysisPlugin plugin = new AnalysisPlugin() {
            class MockFactory extends AbstractTokenFilterFactory {
                MockFactory(IndexSettings indexSettings, Environment env, String name, Settings settings) {
                    super(indexSettings, name, settings);
                }

                @Override
                public TokenStream create(TokenStream tokenStream) {
                    if (name().equals("test_filter")) {
                        return new MockTokenFilter(tokenStream, MockTokenFilter.EMPTY_STOPSET);
                    }
                    return new MockTokenFilter(tokenStream, MockTokenFilter.ENGLISH_STOPSET);
                }
            }

            @Override
            public Map<String, AnalysisProvider<TokenFilterFactory>> getTokenFilters() {
                return singletonMap("mock", MockFactory::new);
            }
        };
        IndexAnalyzers indexAnalyzers = new AnalysisModule(new Environment(settings), singletonList(plugin)).getAnalysisRegistry()
                .build(idxSettings);

        // This shouldn't contain English stopwords
        try (NamedAnalyzer custom_analyser = indexAnalyzers.get("custom_analyzer_with_camel_case")) {
            assertNotNull(custom_analyser);
            TokenStream tokenStream = custom_analyser.tokenStream("foo", "has a foo");
            tokenStream.reset();
            CharTermAttribute charTermAttribute = tokenStream.addAttribute(CharTermAttribute.class);
            assertTrue(tokenStream.incrementToken());
            assertEquals("has", charTermAttribute.toString());
            assertTrue(tokenStream.incrementToken());
            assertEquals("foo", charTermAttribute.toString());
            assertFalse(tokenStream.incrementToken());
        }

        // This *should* contain English stopwords
        try (NamedAnalyzer custom_analyser = indexAnalyzers.get("custom_analyzer_with_snake_case")) {
            assertNotNull(custom_analyser);
            TokenStream tokenStream = custom_analyser.tokenStream("foo", "has a foo");
            tokenStream.reset();
            CharTermAttribute charTermAttribute = tokenStream.addAttribute(CharTermAttribute.class);
            assertTrue(tokenStream.incrementToken());
            assertEquals("has", charTermAttribute.toString());
            assertTrue(tokenStream.incrementToken());
            assertEquals("a", charTermAttribute.toString());
            assertTrue(tokenStream.incrementToken());
            assertEquals("foo", charTermAttribute.toString());
            assertFalse(tokenStream.incrementToken());
        }
    }

    public void testBuiltInAnalyzersAreCached() throws IOException {
        Settings settings = Settings.builder().put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString()).build();
        Settings indexSettings = Settings.builder()
                .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT).build();
        IndexSettings idxSettings = IndexSettingsModule.newIndexSettings("index", indexSettings);
        IndexAnalyzers indexAnalyzers = new AnalysisRegistry(new Environment(settings),
                emptyMap(), emptyMap(), emptyMap(), emptyMap(), emptyMap())
                .build(idxSettings);
        IndexAnalyzers otherIndexAnalyzers = new AnalysisRegistry(new Environment(settings), emptyMap(), emptyMap(), emptyMap(),
                emptyMap(), emptyMap()).build(idxSettings);
        final int numIters = randomIntBetween(5, 20);
        for (int i = 0; i < numIters; i++) {
            PreBuiltAnalyzers preBuiltAnalyzers = RandomPicks.randomFrom(random(), PreBuiltAnalyzers.values());
            assertSame(indexAnalyzers.get(preBuiltAnalyzers.name()), otherIndexAnalyzers.get(preBuiltAnalyzers.name()));
        }
    }

    public void testNoTypeOrTokenizerErrorMessage() throws IOException {
        Version version = VersionUtils.randomVersion(random());
        Settings settings = Settings
            .builder()
            .put(IndexMetaData.SETTING_VERSION_CREATED, version)
            .put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString())
            .putArray("index.analysis.analyzer.test_analyzer.filter", new String[] {"lowercase", "stop", "shingle"})
            .putArray("index.analysis.analyzer.test_analyzer.char_filter", new String[] {"html_strip"})
            .build();
        IndexSettings idxSettings = IndexSettingsModule.newIndexSettings("index", settings);

        IllegalArgumentException e = expectThrows(IllegalArgumentException.class,
                () -> new AnalysisRegistry(new Environment(settings),
                        emptyMap(), emptyMap(), emptyMap(), emptyMap(), emptyMap()).build(idxSettings));
        assertThat(e.getMessage(), equalTo("analyzer [test_analyzer] must specify either an analyzer type, or a tokenizer"));
    }

    public void testCloseIndexAnalyzersMultipleTimes() throws IOException {
        Settings settings = Settings.builder().put(Environment.PATH_HOME_SETTING.getKey(), createTempDir().toString()).build();
        Settings indexSettings = Settings.builder()
            .put(IndexMetaData.SETTING_VERSION_CREATED, Version.CURRENT).build();
        IndexSettings idxSettings = IndexSettingsModule.newIndexSettings("index", indexSettings);
        IndexAnalyzers indexAnalyzers = new AnalysisRegistry(new Environment(settings),
                emptyMap(), emptyMap(), emptyMap(), emptyMap(), emptyMap())
            .build(idxSettings);
        indexAnalyzers.close();
        indexAnalyzers.close();
    }
}
