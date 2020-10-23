/*
 * Copyright 2012-2015 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.autoconfigure.jackson;

import java.io.IOException;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonCreator.Mode;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.ObjectCodec;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonSerializer;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.Module;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.PropertyNamingStrategy.LowerCaseWithUnderscoresStrategy;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.introspect.Annotated;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.databind.util.StdDateFormat;
import com.fasterxml.jackson.module.paramnames.ParameterNamesModule;
import org.joda.time.DateTime;
import org.joda.time.DateTimeZone;
import org.joda.time.LocalDateTime;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import org.springframework.boot.autoconfigure.web.HttpMessageConvertersAutoConfiguration;
import org.springframework.boot.test.EnvironmentTestUtils;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Primary;
import org.springframework.http.converter.json.Jackson2ObjectMapperBuilder;

import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasItem;
import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;

/**
 * Tests for {@link JacksonAutoConfiguration}.
 *
 * @author Dave Syer
 * @author Oliver Gierke
 * @author Andy Wilkinson
 * @author Marcel Overdijk
 * @author Sebastien Deleuze
 * @author Johannes Edmeier
 */
public class JacksonAutoConfigurationTests {

	private AnnotationConfigApplicationContext context;

	@Before
	public void setUp() {
		this.context = new AnnotationConfigApplicationContext();
	}

	@After
	public void tearDown() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	public void registersJodaModuleAutomatically() {
		this.context.register(JacksonAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper objectMapper = this.context.getBean(ObjectMapper.class);
		assertThat(objectMapper.canSerialize(LocalDateTime.class), is(true));
	}

	@Test
	public void doubleModuleRegistration() throws Exception {
		this.context.register(DoubleModulesConfig.class,
				HttpMessageConvertersAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertEquals("{\"foo\":\"bar\"}", mapper.writeValueAsString(new Foo()));
	}

	/*
	 * ObjectMapper does not contain method to get the date format of the mapper. See
	 * https://github.com/FasterXML/jackson-databind/issues/559 If such a method will be
	 * provided below tests can be simplified.
	 */

	@Test
	public void noCustomDateFormat() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertThat(mapper.getDateFormat(), is(instanceOf(StdDateFormat.class)));
	}

	@Test
	public void customDateFormat() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.date-format:yyyyMMddHHmmss");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		DateFormat dateFormat = mapper.getDateFormat();
		assertThat(dateFormat, is(instanceOf(SimpleDateFormat.class)));
		assertThat(((SimpleDateFormat) dateFormat).toPattern(),
				is(equalTo("yyyyMMddHHmmss")));
	}

	@Test
	public void customJodaDateTimeFormat() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.date-format:yyyyMMddHHmmss",
				"spring.jackson.joda-date-time-format:yyyy-MM-dd HH:mm:ss");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		DateTime dateTime = new DateTime(1988, 6, 25, 20, 30, DateTimeZone.UTC);
		assertEquals("\"1988-06-25 20:30:00\"", mapper.writeValueAsString(dateTime));
		Date date = dateTime.toDate();
		assertEquals("\"19880625203000\"", mapper.writeValueAsString(date));
	}

	@Test
	public void customDateFormatClass() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.date-format:org.springframework.boot.autoconfigure.jackson.JacksonAutoConfigurationTests.MyDateFormat");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertThat(mapper.getDateFormat(), is(instanceOf(MyDateFormat.class)));
	}

	@Test
	public void noCustomPropertyNamingStrategy() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertThat(mapper.getPropertyNamingStrategy(), is(nullValue()));
	}

	@Test
	public void customPropertyNamingStrategyField() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.property-naming-strategy:CAMEL_CASE_TO_LOWER_CASE_WITH_UNDERSCORES");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertThat(mapper.getPropertyNamingStrategy(),
				is(instanceOf(LowerCaseWithUnderscoresStrategy.class)));
	}

	@Test
	public void customPropertyNamingStrategyClass() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.property-naming-strategy:com.fasterxml.jackson.databind.PropertyNamingStrategy.LowerCaseWithUnderscoresStrategy");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertThat(mapper.getPropertyNamingStrategy(),
				is(instanceOf(LowerCaseWithUnderscoresStrategy.class)));
	}

	@Test
	public void enableSerializationFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.serialization.indent_output:true");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertFalse(SerializationFeature.INDENT_OUTPUT.enabledByDefault());
		assertTrue(mapper.getSerializationConfig()
				.hasSerializationFeatures(SerializationFeature.INDENT_OUTPUT.getMask()));
	}

	@Test
	public void disableSerializationFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.serialization.write_dates_as_timestamps:false");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertTrue(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS.enabledByDefault());
		assertFalse(mapper.getSerializationConfig().hasSerializationFeatures(
				SerializationFeature.WRITE_DATES_AS_TIMESTAMPS.getMask()));
	}

	@Test
	public void enableDeserializationFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.deserialization.use_big_decimal_for_floats:true");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertFalse(DeserializationFeature.USE_BIG_DECIMAL_FOR_FLOATS.enabledByDefault());
		assertTrue(mapper.getDeserializationConfig().hasDeserializationFeatures(
				DeserializationFeature.USE_BIG_DECIMAL_FOR_FLOATS.getMask()));
	}

	@Test
	public void disableDeserializationFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.deserialization.fail-on-unknown-properties:false");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertTrue(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES.enabledByDefault());
		assertFalse(mapper.getDeserializationConfig().hasDeserializationFeatures(
				DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES.getMask()));
	}

	@Test
	public void enableMapperFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.mapper.require_setters_for_getters:true");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertFalse(MapperFeature.REQUIRE_SETTERS_FOR_GETTERS.enabledByDefault());
		assertTrue(mapper.getSerializationConfig()
				.hasMapperFeatures(MapperFeature.REQUIRE_SETTERS_FOR_GETTERS.getMask()));
		assertTrue(mapper.getDeserializationConfig()
				.hasMapperFeatures(MapperFeature.REQUIRE_SETTERS_FOR_GETTERS.getMask()));
	}

	@Test
	public void disableMapperFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.mapper.use_annotations:false");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertTrue(MapperFeature.USE_ANNOTATIONS.enabledByDefault());
		assertFalse(mapper.getDeserializationConfig()
				.hasMapperFeatures(MapperFeature.USE_ANNOTATIONS.getMask()));
		assertFalse(mapper.getSerializationConfig()
				.hasMapperFeatures(MapperFeature.USE_ANNOTATIONS.getMask()));
	}

	@Test
	public void enableParserFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.parser.allow_single_quotes:true");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertFalse(JsonParser.Feature.ALLOW_SINGLE_QUOTES.enabledByDefault());
		assertTrue(mapper.getFactory().isEnabled(JsonParser.Feature.ALLOW_SINGLE_QUOTES));
	}

	@Test
	public void disableParserFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.parser.auto_close_source:false");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertTrue(JsonParser.Feature.AUTO_CLOSE_SOURCE.enabledByDefault());
		assertFalse(mapper.getFactory().isEnabled(JsonParser.Feature.AUTO_CLOSE_SOURCE));
	}

	@Test
	public void enableGeneratorFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.generator.write_numbers_as_strings:true");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertFalse(JsonGenerator.Feature.WRITE_NUMBERS_AS_STRINGS.enabledByDefault());
		assertTrue(mapper.getFactory()
				.isEnabled(JsonGenerator.Feature.WRITE_NUMBERS_AS_STRINGS));
	}

	@Test
	public void disableGeneratorFeature() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.generator.auto_close_target:false");
		this.context.refresh();
		ObjectMapper mapper = this.context.getBean(ObjectMapper.class);
		assertTrue(JsonGenerator.Feature.AUTO_CLOSE_TARGET.enabledByDefault());
		assertFalse(
				mapper.getFactory().isEnabled(JsonGenerator.Feature.AUTO_CLOSE_TARGET));
	}

	@Test
	public void defaultObjectMapperBuilder() throws Exception {
		this.context.register(JacksonAutoConfiguration.class);
		this.context.refresh();
		Jackson2ObjectMapperBuilder builder = this.context
				.getBean(Jackson2ObjectMapperBuilder.class);
		ObjectMapper mapper = builder.build();
		assertTrue(MapperFeature.DEFAULT_VIEW_INCLUSION.enabledByDefault());
		assertFalse(mapper.getDeserializationConfig()
				.isEnabled(MapperFeature.DEFAULT_VIEW_INCLUSION));
		assertTrue(MapperFeature.DEFAULT_VIEW_INCLUSION.enabledByDefault());
		assertFalse(mapper.getDeserializationConfig()
				.isEnabled(MapperFeature.DEFAULT_VIEW_INCLUSION));
		assertFalse(mapper.getSerializationConfig()
				.isEnabled(MapperFeature.DEFAULT_VIEW_INCLUSION));
		assertTrue(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES.enabledByDefault());
		assertFalse(mapper.getDeserializationConfig()
				.isEnabled(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES));
	}

	@Test
	public void moduleBeansAndWellKnownModulesAreRegisteredWithTheObjectMapperBuilder() {
		this.context.register(ModuleConfig.class, JacksonAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();
		assertThat(this.context.getBean(CustomModule.class).getOwners(),
				hasItem((ObjectCodec) objectMapper));
		assertThat(objectMapper.canSerialize(LocalDateTime.class), is(true));
	}

	@Test
	public void defaultSerializationInclusion() {
		this.context.register(JacksonAutoConfiguration.class);
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();
		assertThat(objectMapper.getSerializationConfig().getSerializationInclusion(),
				is(JsonInclude.Include.ALWAYS));
	}

	@Test
	public void customSerializationInclusion() {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.serialization-inclusion:non_null");
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();
		assertThat(objectMapper.getSerializationConfig().getSerializationInclusion(),
				is(JsonInclude.Include.NON_NULL));
	}

	@Test
	public void customTimeZoneFormattingADateTime() throws JsonProcessingException {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.time-zone:America/Los_Angeles");
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.date-format:zzzz");
		EnvironmentTestUtils.addEnvironment(this.context, "spring.jackson.locale:en");
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();
		DateTime dateTime = new DateTime(1436966242231L, DateTimeZone.UTC);
		assertEquals("\"Pacific Daylight Time\"",
				objectMapper.writeValueAsString(dateTime));
	}

	@Test
	public void customTimeZoneFormattingADate() throws JsonProcessingException {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.time-zone:GMT+10");
		EnvironmentTestUtils.addEnvironment(this.context, "spring.jackson.date-format:z");
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();
		Date date = new Date(1436966242231L);
		assertEquals("\"GMT+10:00\"", objectMapper.writeValueAsString(date));
	}

	@Test
	public void customLocale() throws JsonProcessingException {
		this.context.register(JacksonAutoConfiguration.class);
		EnvironmentTestUtils.addEnvironment(this.context, "spring.jackson.locale:de");
		EnvironmentTestUtils.addEnvironment(this.context,
				"spring.jackson.date-format:zzzz");
		this.context.refresh();
		ObjectMapper objectMapper = this.context
				.getBean(Jackson2ObjectMapperBuilder.class).build();

		DateTime dateTime = new DateTime(1436966242231L, DateTimeZone.UTC);
		assertEquals("\"Koordinierte Universalzeit\"",
				objectMapper.writeValueAsString(dateTime));
	}

	@Test
	public void parameterNamesModuleIsAutoConfigured() {
		assertParameterNamesModuleCreatorBinding(Mode.DEFAULT,
				JacksonAutoConfiguration.class);
	}

	@Test
	public void customParameterNamesModuleCanBeConfigured() {
		assertParameterNamesModuleCreatorBinding(Mode.DELEGATING,
				ParameterNamesModuleConfig.class, JacksonAutoConfiguration.class);
	}

	private void assertParameterNamesModuleCreatorBinding(Mode expectedMode,
			Class<?>... configClasses) {
		this.context.register(configClasses);
		this.context.refresh();
		Annotated annotated = mock(Annotated.class);
		Mode mode = this.context.getBean(ObjectMapper.class).getDeserializationConfig()
				.getAnnotationIntrospector().findCreatorBinding(annotated);
		assertThat(mode, is(equalTo(expectedMode)));
	}

	public static class MyDateFormat extends SimpleDateFormat {

		public MyDateFormat() {
			super("yyyy-MM-dd HH:mm:ss");
		}
	}

	@Configuration
	protected static class MockObjectMapperConfig {

		@Bean
		@Primary
		public ObjectMapper objectMapper() {
			return mock(ObjectMapper.class);
		}

	}

	@Configuration
	protected static class ModuleConfig {

		@Bean
		public CustomModule jacksonModule() {
			return new CustomModule();
		}
	}

	@Configuration
	protected static class DoubleModulesConfig {

		@Bean
		public Module jacksonModule() {
			SimpleModule module = new SimpleModule();
			module.addSerializer(Foo.class, new JsonSerializer<Foo>() {

				@Override
				public void serialize(Foo value, JsonGenerator jgen,
						SerializerProvider provider)
								throws IOException, JsonProcessingException {
					jgen.writeStartObject();
					jgen.writeStringField("foo", "bar");
					jgen.writeEndObject();
				}
			});
			return module;
		}

		@Bean
		@Primary
		public ObjectMapper objectMapper() {
			ObjectMapper mapper = new ObjectMapper();
			mapper.registerModule(jacksonModule());
			return mapper;
		}

	}

	@Configuration
	protected static class ParameterNamesModuleConfig {

		@Bean
		public ParameterNamesModule parameterNamesModule() {
			return new ParameterNamesModule(JsonCreator.Mode.DELEGATING);
		}

	}

	protected static final class Foo {

		private String name;

		private Foo() {
		}

		static Foo create() {
			return new Foo();
		}

		public String getName() {
			return this.name;
		}

		public void setName(String name) {
			this.name = name;
		}

	}

	protected static class Bar {

		private String propertyName;

		public String getPropertyName() {
			return this.propertyName;
		}

		public void setPropertyName(String propertyName) {
			this.propertyName = propertyName;
		}
	}

	private static class CustomModule extends SimpleModule {

		private Set<ObjectCodec> owners = new HashSet<ObjectCodec>();

		@Override
		public void setupModule(SetupContext context) {
			this.owners.add(context.getOwner());
		}

		Set<ObjectCodec> getOwners() {
			return this.owners;
		}

	}
}
