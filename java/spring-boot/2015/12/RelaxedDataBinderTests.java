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

package org.springframework.boot.bind;

import java.lang.annotation.Documented;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Properties;
import java.util.Set;

import javax.validation.Constraint;
import javax.validation.ConstraintValidator;
import javax.validation.ConstraintValidatorContext;
import javax.validation.Payload;
import javax.validation.constraints.NotNull;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;

import org.springframework.beans.MutablePropertyValues;
import org.springframework.beans.NotWritablePropertyException;
import org.springframework.context.support.StaticMessageSource;
import org.springframework.core.convert.ConversionService;
import org.springframework.core.convert.support.DefaultConversionService;
import org.springframework.core.io.ByteArrayResource;
import org.springframework.core.io.support.PropertiesLoaderUtils;
import org.springframework.validation.BindingResult;
import org.springframework.validation.DataBinder;
import org.springframework.validation.FieldError;
import org.springframework.validation.beanvalidation.LocalValidatorFactoryBean;

import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;

/**
 * Tests for {@link RelaxedDataBinder}.
 *
 * @author Dave Syer
 * @author Phillip Webb
 * @author Stephane Nicoll
 */
public class RelaxedDataBinderTests {

	@Rule
	public ExpectedException expected = ExpectedException.none();

	private ConversionService conversionService;

	@Test
	public void testBindString() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "foo: bar");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindChars() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "bar: foo");
		assertEquals("foo", new String(target.getBar()));
	}

	@Test
	public void testBindStringWithPrefix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "test.foo: bar", "test");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindStringWithPrefixDotSuffix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "some.test.foo: bar", "some.test.");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindFromEnvironmentStyleWithPrefix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "TEST_FOO: bar", "test");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindToCamelCaseFromEnvironmentStyleWithPrefix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "TEST_FOO_BAZ: bar", "test");
		assertEquals("bar", target.getFooBaz());
	}

	@Test
	public void testBindToCamelCaseFromEnvironmentStyle() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "test.FOO_BAZ: bar", "test");
		assertEquals("bar", target.getFooBaz());
	}

	@Test
	public void testBindFromEnvironmentStyleWithNestedPrefix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "TEST_IT_FOO: bar", "test.it");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindCapitals() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "FOO: bar");
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testBindUnderscoreInActualPropertyName() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "foo-bar: bar");
		assertEquals("bar", target.getFoo_bar());
	}

	@Test
	public void testBindUnderscoreToCamelCase() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "foo_baz: bar");
		assertEquals("bar", target.getFooBaz());
	}

	@Test
	public void testBindHyphen() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "foo-baz: bar");
		assertEquals("bar", target.getFooBaz());
	}

	@Test
	public void testBindCamelCase() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "fooBaz: bar");
		assertEquals("bar", target.getFooBaz());
	}

	@Test
	public void testBindNumber() throws Exception {
		VanillaTarget target = new VanillaTarget();
		bind(target, "foo: bar\n" + "value: 123");
		assertEquals(123, target.getValue());
	}

	@Test
	public void testSimpleValidation() throws Exception {
		ValidatedTarget target = new ValidatedTarget();
		BindingResult result = bind(target, "");
		assertEquals(1, result.getErrorCount());
	}

	@Test
	public void testRequiredFieldsValidation() throws Exception {
		TargetWithValidatedMap target = new TargetWithValidatedMap();
		BindingResult result = bind(target, "info[foo]: bar");
		assertEquals(2, result.getErrorCount());
		for (FieldError error : result.getFieldErrors()) {
			System.err.println(
					new StaticMessageSource().getMessage(error, Locale.getDefault()));
		}
	}

	@Test
	public void testAllowedFields() throws Exception {
		VanillaTarget target = new VanillaTarget();
		RelaxedDataBinder binder = getBinder(target, null);
		binder.setAllowedFields("foo");
		binder.setIgnoreUnknownFields(false);
		BindingResult result = bind(binder, target,
				"foo: bar\n" + "value: 123\n" + "bar: spam");
		assertEquals(0, target.getValue());
		assertEquals("bar", target.getFoo());
		assertEquals(0, result.getErrorCount());
	}

	@Test
	public void testDisallowedFields() throws Exception {
		VanillaTarget target = new VanillaTarget();
		RelaxedDataBinder binder = getBinder(target, null);
		// Disallowed fields are not unknown...
		binder.setDisallowedFields("foo", "bar");
		binder.setIgnoreUnknownFields(false);
		BindingResult result = bind(binder, target,
				"foo: bar\n" + "value: 123\n" + "bar: spam");
		assertEquals(123, target.getValue());
		assertNull(target.getFoo());
		assertEquals(0, result.getErrorCount());
	}

	@Test
	public void testBindNested() throws Exception {
		TargetWithNestedObject target = new TargetWithNestedObject();
		bind(target, "nested.foo: bar\n" + "nested.value: 123");
		assertEquals(123, target.getNested().getValue());
	}

	@Test
	public void testBindRelaxedNestedValue() throws Exception {
		TargetWithNestedObject target = new TargetWithNestedObject();
		bind(target, "nested_foo_Baz: bar\n" + "nested_value: 123");
		assertEquals("bar", target.getNested().getFooBaz());
		assertEquals(123, target.getNested().getValue());
	}

	@Test
	public void testBindRelaxedNestedCamelValue() throws Exception {
		TargetWithNestedObject target = new TargetWithNestedObject();
		bind(target, "another_nested_foo_Baz: bar\n" + "another-nested_value: 123");
		assertEquals("bar", target.getAnotherNested().getFooBaz());
		assertEquals(123, target.getAnotherNested().getValue());
	}

	@Test
	public void testBindNestedWithEnvironmentStyle() throws Exception {
		TargetWithNestedObject target = new TargetWithNestedObject();
		bind(target, "nested_foo: bar\n" + "nested_value: 123");
		assertEquals(123, target.getNested().getValue());
	}

	@Test
	public void testBindNestedList() throws Exception {
		TargetWithNestedList target = new TargetWithNestedList();
		bind(target, "nested[0]: bar\nnested[1]: foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test
	public void testBindNestedListOfBean() throws Exception {
		TargetWithNestedListOfBean target = new TargetWithNestedListOfBean();
		bind(target, "nested[0].foo: bar\nnested[1].foo: foo");
		assertEquals("bar", target.getNested().get(0).getFoo());
	}

	@Test
	public void testBindNestedListOfBeanWithList() throws Exception {
		TargetWithNestedListOfBeanWithList target = new TargetWithNestedListOfBeanWithList();
		bind(target, "nested[0].nested[0].foo: bar\nnested[1].nested[0].foo: foo");
		assertEquals("bar", target.getNested().get(0).getNested().get(0).getFoo());
	}

	@Test
	public void testBindNestedListCommaDelimitedOnly() throws Exception {
		TargetWithNestedList target = new TargetWithNestedList();
		this.conversionService = new DefaultConversionService();
		bind(target, "nested: bar,foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test
	public void testBindNestedSetCommaDelimitedOnly() throws Exception {
		TargetWithNestedSet target = new TargetWithNestedSet();
		this.conversionService = new DefaultConversionService();
		bind(target, "nested: bar,foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test(expected = NotWritablePropertyException.class)
	public void testBindNestedReadOnlyListCommaSeparated() throws Exception {
		TargetWithReadOnlyNestedList target = new TargetWithReadOnlyNestedList();
		this.conversionService = new DefaultConversionService();
		bind(target, "nested: bar,foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test
	public void testBindNestedReadOnlyListIndexed() throws Exception {
		TargetWithReadOnlyNestedList target = new TargetWithReadOnlyNestedList();
		this.conversionService = new DefaultConversionService();
		bind(target, "nested[0]: bar\nnested[1]:foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test
	public void testBindDoubleNestedReadOnlyListIndexed() throws Exception {
		TargetWithReadOnlyDoubleNestedList target = new TargetWithReadOnlyDoubleNestedList();
		this.conversionService = new DefaultConversionService();
		bind(target, "bean.nested[0]:bar\nbean.nested[1]:foo");
		assertEquals("[bar, foo]", target.getBean().getNested().toString());
	}

	@Test
	public void testBindNestedReadOnlyCollectionIndexed() throws Exception {
		TargetWithReadOnlyNestedCollection target = new TargetWithReadOnlyNestedCollection();
		this.conversionService = new DefaultConversionService();
		bind(target, "nested[0]: bar\nnested[1]:foo");
		assertEquals("[bar, foo]", target.getNested().toString());
	}

	@Test
	public void testBindNestedMap() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested.foo: bar\n" + "nested.value: 123");
		assertEquals("123", target.getNested().get("value"));
	}

	@Test
	public void testBindNestedMapPropsWithUnderscores() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested_foo: bar\n" + "nested_value: 123");
		assertEquals("123", target.getNested().get("value"));
		assertEquals("bar", target.getNested().get("foo"));
	}

	@Test
	public void testBindNestedUntypedMap() throws Exception {
		TargetWithNestedUntypedMap target = new TargetWithNestedUntypedMap();
		bind(target, "nested.foo: bar\n" + "nested.value: 123");
		assertEquals("123", target.getNested().get("value"));
	}

	@Test
	public void testBindNestedMapOfString() throws Exception {
		TargetWithNestedMapOfString target = new TargetWithNestedMapOfString();
		bind(target, "nested.foo: bar\n" + "nested.value.foo: 123");
		assertEquals("bar", target.getNested().get("foo"));
		assertEquals("123", target.getNested().get("value.foo"));
	}

	@Test
	public void testBindNestedMapOfStringWithUnderscore() throws Exception {
		TargetWithNestedMapOfString target = new TargetWithNestedMapOfString();
		bind(target, "nested_foo: bar\n" + "nested_value_foo: 123");
		assertEquals("bar", target.getNested().get("foo"));
		assertEquals("123", target.getNested().get("value_foo"));
	}

	@Test
	public void testBindNestedMapOfStringWithUnderscoreAndUpperCase() throws Exception {
		TargetWithNestedMapOfString target = new TargetWithNestedMapOfString();
		bind(target, "NESTED_FOO: bar\n" + "NESTED_VALUE_FOO: 123");
		assertEquals("bar", target.getNested().get("FOO"));
		assertEquals("123", target.getNested().get("VALUE_FOO"));
	}

	@Test
	public void testBindNestedMapOfStringReferenced() throws Exception {
		TargetWithNestedMapOfString target = new TargetWithNestedMapOfString();
		bind(target, "nested.foo: bar\n" + "nested[value.foo]: 123");
		assertEquals("bar", target.getNested().get("foo"));
		assertEquals("123", target.getNested().get("value.foo"));
	}

	@Test
	public void testBindNestedProperties() throws Exception {
		TargetWithNestedProperties target = new TargetWithNestedProperties();
		bind(target, "nested.foo: bar\n" + "nested.value.foo: 123");
		assertEquals("bar", target.getNested().get("foo"));
		assertEquals("123", target.getNested().get("value.foo"));
	}

	@Test
	public void testBindNestedMapOfEnum() throws Exception {
		this.conversionService = new DefaultConversionService();
		TargetWithNestedMapOfEnum target = new TargetWithNestedMapOfEnum();
		bind(target, "nested.this: bar\n" + "nested.ThAt: 123");
		assertEquals("bar", target.getNested().get(Bingo.THIS));
		assertEquals("123", target.getNested().get(Bingo.THAT));
	}

	@Test
	public void testBindNestedMapOfEnumRelaxedNames() throws Exception {
		this.conversionService = new DefaultConversionService();
		TargetWithNestedMapOfEnum target = new TargetWithNestedMapOfEnum();
		bind(target, "nested.the-other: bar\n" + "nested.that_other: 123");
		assertEquals("bar", target.getNested().get(Bingo.THE_OTHER));
		assertEquals("123", target.getNested().get(Bingo.THAT_OTHER));
	}

	@Test
	public void testBindNestedMapBracketReferenced() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested[foo]: bar\n" + "nested[value]: 123");
		assertEquals("123", target.getNested().get("value"));
	}

	@Test
	public void testBindNestedMapBracketReferencedAndPeriods() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested[foo]: bar\n" + "nested[foo.value]: 123");
		assertEquals("123", target.getNested().get("foo.value"));
	}

	@SuppressWarnings("unchecked")
	@Test
	public void testBindDoubleNestedMapWithDotInKeys() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested.foo: bar.key\n" + "nested[bar.key].spam: bucket\n"
				+ "nested[bar.key].value: 123\nnested[bar.key].foo: crap");
		assertEquals(2, target.getNested().size());
		Map<String, Object> nestedMap = (Map<String, Object>) target.getNested()
				.get("bar.key");
		assertNotNull("nested map should be registered with 'bar.key'", nestedMap);
		assertEquals(3, nestedMap.size());
		assertEquals("123", nestedMap.get("value"));
		assertEquals("bar.key", target.getNested().get("foo"));
		assertFalse(target.getNested().containsValue(target.getNested()));
	}

	@SuppressWarnings("unchecked")
	@Test
	public void testBindDoubleNestedMap() throws Exception {
		TargetWithNestedMap target = new TargetWithNestedMap();
		bind(target, "nested.foo: bar\n" + "nested.bar.spam: bucket\n"
				+ "nested.bar.value: 123\nnested.bar.foo: crap");
		assertEquals(2, target.getNested().size());
		assertEquals(3, ((Map<String, Object>) target.getNested().get("bar")).size());
		assertEquals("123",
				((Map<String, Object>) target.getNested().get("bar")).get("value"));
		assertEquals("bar", target.getNested().get("foo"));
		assertFalse(target.getNested().containsValue(target.getNested()));
	}

	@Test
	public void testBindNestedMapOfListOfString() throws Exception {
		TargetWithNestedMapOfListOfString target = new TargetWithNestedMapOfListOfString();
		bind(target, "nested.foo[0]: bar\n" + "nested.bar[0]: bucket\n"
				+ "nested.bar[1]: 123\nnested.bar[2]: crap");
		assertEquals(2, target.getNested().size());
		assertEquals(3, target.getNested().get("bar").size());
		assertEquals("123", target.getNested().get("bar").get(1));
		assertEquals("[bar]", target.getNested().get("foo").toString());
	}

	@Test
	public void testBindNestedMapOfBean() throws Exception {
		TargetWithNestedMapOfBean target = new TargetWithNestedMapOfBean();
		bind(target, "nested.foo.foo: bar\n" + "nested.bar.foo: bucket");
		assertEquals(2, target.getNested().size());
		assertEquals("bucket", target.getNested().get("bar").getFoo());
	}

	@Test
	public void testBindNestedMapOfListOfBean() throws Exception {
		TargetWithNestedMapOfListOfBean target = new TargetWithNestedMapOfListOfBean();
		bind(target, "nested.foo[0].foo: bar\n" + "nested.bar[0].foo: bucket\n"
				+ "nested.bar[1].value: 123\nnested.bar[2].foo: crap");
		assertEquals(2, target.getNested().size());
		assertEquals(3, target.getNested().get("bar").size());
		assertEquals(123, target.getNested().get("bar").get(1).getValue());
		assertEquals("bar", target.getNested().get("foo").get(0).getFoo());
	}

	@Test
	public void testBindErrorTypeMismatch() throws Exception {
		VanillaTarget target = new VanillaTarget();
		BindingResult result = bind(target, "foo: bar\n" + "value: foo");
		assertEquals(1, result.getErrorCount());
	}

	@Test
	public void testBindErrorNotWritable() throws Exception {
		this.expected.expectMessage("property 'spam'");
		this.expected.expectMessage("not writable");
		VanillaTarget target = new VanillaTarget();
		BindingResult result = bind(target, "spam: bar\n" + "value: 123");
		assertEquals(1, result.getErrorCount());
	}

	@Test
	public void testBindErrorNotWritableWithPrefix() throws Exception {
		VanillaTarget target = new VanillaTarget();
		BindingResult result = bind(target, "spam: bar\n" + "vanilla.value: 123",
				"vanilla");
		assertEquals(0, result.getErrorCount());
		assertEquals(123, target.getValue());
	}

	@Test
	public void testOnlyTopLevelFields() throws Exception {
		VanillaTarget target = new VanillaTarget();
		RelaxedDataBinder binder = getBinder(target, null);
		binder.setIgnoreUnknownFields(false);
		binder.setIgnoreNestedProperties(true);
		BindingResult result = bind(binder, target,
				"foo: bar\n" + "value: 123\n" + "nested.bar: spam");
		assertEquals(123, target.getValue());
		assertEquals("bar", target.getFoo());
		assertEquals(0, result.getErrorCount());
	}

	@Test
	public void testNoNestedFields() throws Exception {
		VanillaTarget target = new VanillaTarget();
		RelaxedDataBinder binder = getBinder(target, "foo");
		binder.setIgnoreUnknownFields(false);
		binder.setIgnoreNestedProperties(true);
		BindingResult result = bind(binder, target,
				"foo.foo: bar\n" + "foo.value: 123\n" + "foo.nested.bar: spam");
		assertEquals(123, target.getValue());
		assertEquals("bar", target.getFoo());
		assertEquals(0, result.getErrorCount());
	}

	@Test
	public void testBindMap() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target, "spam: bar\n" + "vanilla.value: 123",
				"vanilla");
		assertEquals(0, result.getErrorCount());
		assertEquals("123", target.get("value"));
	}

	@Test
	public void testBindMapWithClashInProperties() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target,
				"vanilla.spam: bar\n" + "vanilla.spam.value: 123", "vanilla");
		assertEquals(0, result.getErrorCount());
		assertEquals(2, target.size());
		assertEquals("bar", target.get("spam"));
		assertEquals("123", target.get("spam.value"));
	}

	@Test
	public void testBindMapWithDeepClashInProperties() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target,
				"vanilla.spam.foo: bar\n" + "vanilla.spam.foo.value: 123", "vanilla");
		assertEquals(0, result.getErrorCount());
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) target.get("spam");
		assertEquals("123", map.get("foo.value"));
	}

	@Test
	public void testBindMapWithDifferentDeepClashInProperties() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target,
				"vanilla.spam.bar: bar\n" + "vanilla.spam.bar.value: 123", "vanilla");
		assertEquals(0, result.getErrorCount());
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) target.get("spam");
		assertEquals("123", map.get("bar.value"));
	}

	@Test
	public void testBindShallowMap() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target, "vanilla.spam: bar\n" + "vanilla.value: 123",
				"vanilla");
		assertEquals(0, result.getErrorCount());
		assertEquals("123", target.get("value"));
	}

	@Test
	public void testBindMapNestedMap() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target, "spam: bar\n" + "vanilla.foo.value: 123",
				"vanilla");
		assertEquals(0, result.getErrorCount());
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) target.get("foo");
		assertEquals("123", map.get("value"));
	}

	@SuppressWarnings("unchecked")
	@Test
	public void testBindOverlappingNestedMaps() throws Exception {
		Map<String, Object> target = new LinkedHashMap<String, Object>();
		BindingResult result = bind(target, "a.b.c.d: abc\na.b.c1.d1: efg");
		assertEquals(0, result.getErrorCount());

		Map<String, Object> a = (Map<String, Object>) target.get("a");
		Map<String, Object> b = (Map<String, Object>) a.get("b");
		Map<String, Object> c = (Map<String, Object>) b.get("c");
		assertEquals("abc", c.get("d"));

		Map<String, Object> c1 = (Map<String, Object>) b.get("c1");
		assertEquals("efg", c1.get("d1"));
	}

	@Test
	public void testBindCaseInsensitiveEnumsWithoutConverter() throws Exception {
		VanillaTarget target = new VanillaTarget();
		doTestBindCaseInsensitiveEnums(target);
	}

	@Test
	public void testBindCaseInsensitiveEnumsWithConverter() throws Exception {
		VanillaTarget target = new VanillaTarget();
		this.conversionService = new DefaultConversionService();
		doTestBindCaseInsensitiveEnums(target);
	}

	@Test
	public void testBindWithoutAlias() throws Exception {
		VanillaTarget target = new VanillaTarget();
		MutablePropertyValues properties = new MutablePropertyValues();
		properties.add("flub", "a");
		properties.add("foo", "b");
		new RelaxedDataBinder(target).bind(properties);
		assertThat(target.getFooBaz(), nullValue());
		assertThat(target.getFoo(), equalTo("b"));
	}

	@Test
	public void testBindWithAlias() throws Exception {
		VanillaTarget target = new VanillaTarget();
		MutablePropertyValues properties = new MutablePropertyValues();
		properties.add("flub", "a");
		properties.add("foo", "b");
		new RelaxedDataBinder(target).withAlias("flub", "fooBaz").bind(properties);
		assertThat(target.getFooBaz(), equalTo("a"));
		assertThat(target.getFoo(), equalTo("b"));
	}

	@Test
	public void testMixed() throws Exception {
		// gh-3385
		VanillaTarget target = new VanillaTarget();
		RelaxedDataBinder binder = getBinder(target, "test");
		MutablePropertyValues values = new MutablePropertyValues();
		values.add("test.FOO_BAZ", "boo");
		values.add("test.foo-baz", "bar");
		binder.bind(values);
		assertEquals("boo", target.getFooBaz());
	}

	private void doTestBindCaseInsensitiveEnums(VanillaTarget target) throws Exception {
		BindingResult result = bind(target, "bingo: THIS");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.THIS));

		result = bind(target, "bingo: oR");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.or));

		result = bind(target, "bingo: that");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.THAT));

		result = bind(target, "bingo: the-other");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.THE_OTHER));

		result = bind(target, "bingo: the_other");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.THE_OTHER));

		result = bind(target, "bingo: The_Other");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingo(), equalTo(Bingo.THE_OTHER));

		result = bind(target, "bingos: The_Other");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingos(), contains(Bingo.THE_OTHER));

		result = bind(target, "bingos: The_Other, that");
		assertThat(result.getErrorCount(), equalTo(0));
		assertThat(target.getBingos(), contains(Bingo.THE_OTHER, Bingo.THAT));
	}

	private BindingResult bind(Object target, String values) throws Exception {
		return bind(target, values, null);
	}

	private BindingResult bind(Object target, String values, String namePrefix)
			throws Exception {
		return bind(getBinder(target, namePrefix), target, values);
	}

	private RelaxedDataBinder getBinder(Object target, String namePrefix) {
		RelaxedDataBinder binder = new RelaxedDataBinder(target, namePrefix);
		binder.setIgnoreUnknownFields(false);
		LocalValidatorFactoryBean validatorFactoryBean = new LocalValidatorFactoryBean();
		validatorFactoryBean.afterPropertiesSet();
		binder.setValidator(validatorFactoryBean);
		binder.setConversionService(this.conversionService);
		return binder;
	}

	private BindingResult bind(DataBinder binder, Object target, String values)
			throws Exception {
		Properties properties = PropertiesLoaderUtils
				.loadProperties(new ByteArrayResource(values.getBytes()));
		binder.bind(new MutablePropertyValues(properties));
		binder.validate();

		return binder.getBindingResult();
	}

	@Documented
	@Target({ ElementType.FIELD })
	@Retention(RetentionPolicy.RUNTIME)
	@Constraint(validatedBy = RequiredKeysValidator.class)
	public @interface RequiredKeys {

		String[] value();

		String message() default "Required fields are not provided for field ''{0}''";

		Class<?>[] groups() default {};

		Class<? extends Payload>[] payload() default {};

	}

	public static class RequiredKeysValidator
			implements ConstraintValidator<RequiredKeys, Map<String, Object>> {

		private String[] fields;

		@Override
		public void initialize(RequiredKeys constraintAnnotation) {
			this.fields = constraintAnnotation.value();
		}

		@Override
		public boolean isValid(Map<String, Object> value,
				ConstraintValidatorContext context) {
			boolean valid = true;
			for (String field : this.fields) {
				if (!value.containsKey(field)) {
					context.buildConstraintViolationWithTemplate(
							"Missing field ''" + field + "''").addConstraintViolation();
					valid = false;
				}
			}
			return valid;
		}

	}

	public static class TargetWithValidatedMap {

		@RequiredKeys({ "foo", "value" })
		private Map<String, Object> info;

		public Map<String, Object> getInfo() {
			return this.info;
		}

		public void setInfo(Map<String, Object> nested) {
			this.info = nested;
		}

	}

	public static class TargetWithNestedMap {

		private Map<String, Object> nested;

		public Map<String, Object> getNested() {
			return this.nested;
		}

		public void setNested(Map<String, Object> nested) {
			this.nested = nested;
		}

	}

	@SuppressWarnings("rawtypes")
	public static class TargetWithNestedUntypedMap {

		private Map nested;

		public Map getNested() {
			return this.nested;
		}

		public void setNested(Map nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedProperties {

		private Properties nested;

		public Properties getNested() {
			return this.nested;
		}

		public void setNested(Properties nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedMapOfString {

		private Map<String, String> nested;

		public Map<String, String> getNested() {
			return this.nested;
		}

		public void setNested(Map<String, String> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedMapOfEnum {

		private Map<Bingo, Object> nested;

		public Map<Bingo, Object> getNested() {
			return this.nested;
		}

		public void setNested(Map<Bingo, Object> nested) {
			this.nested = nested;
		}
	}

	public static class TargetWithNestedMapOfListOfString {

		private Map<String, List<String>> nested;

		public Map<String, List<String>> getNested() {
			return this.nested;
		}

		public void setNested(Map<String, List<String>> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedMapOfListOfBean {

		private Map<String, List<VanillaTarget>> nested;

		public Map<String, List<VanillaTarget>> getNested() {
			return this.nested;
		}

		public void setNested(Map<String, List<VanillaTarget>> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedMapOfBean {

		private Map<String, VanillaTarget> nested;

		public Map<String, VanillaTarget> getNested() {
			return this.nested;
		}

		public void setNested(Map<String, VanillaTarget> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedList {

		private List<String> nested;

		public List<String> getNested() {
			return this.nested;
		}

		public void setNested(List<String> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedListOfBean {

		private List<VanillaTarget> nested;

		public List<VanillaTarget> getNested() {
			return this.nested;
		}

		public void setNested(List<VanillaTarget> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedListOfBeanWithList {

		private List<TargetWithNestedListOfBean> nested;

		public List<TargetWithNestedListOfBean> getNested() {
			return this.nested;
		}

		public void setNested(List<TargetWithNestedListOfBean> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithReadOnlyNestedList {

		private final List<String> nested = new ArrayList<String>();

		public List<String> getNested() {
			return this.nested;
		}

	}

	public static class TargetWithReadOnlyDoubleNestedList {

		TargetWithReadOnlyNestedList bean = new TargetWithReadOnlyNestedList();

		public TargetWithReadOnlyNestedList getBean() {
			return this.bean;
		}

	}

	public static class TargetWithReadOnlyNestedCollection {

		private final Collection<String> nested = new ArrayList<String>();

		public Collection<String> getNested() {
			return this.nested;
		}

	}

	public static class TargetWithNestedSet {

		private Set<String> nested = new LinkedHashSet<String>();

		public Set<String> getNested() {
			return this.nested;
		}

		public void setNested(Set<String> nested) {
			this.nested = nested;
		}

	}

	public static class TargetWithNestedObject {

		private VanillaTarget nested;

		private VanillaTarget anotherNested;

		public VanillaTarget getNested() {
			return this.nested;
		}

		public void setNested(VanillaTarget nested) {
			this.nested = nested;
		}

		public VanillaTarget getAnotherNested() {
			return this.anotherNested;
		}

		public void setAnotherNested(VanillaTarget anotherNested) {
			this.anotherNested = anotherNested;
		}

	}

	public static class VanillaTarget {

		private String foo;

		private char[] bar;

		private int value;

		private String foo_bar;

		private String fooBaz;

		private Bingo bingo;

		private List<Bingo> bingos;

		public char[] getBar() {
			return this.bar;
		}

		public void setBar(char[] bar) {
			this.bar = bar;
		}

		public int getValue() {
			return this.value;
		}

		public void setValue(int value) {
			this.value = value;
		}

		public String getFoo() {
			return this.foo;
		}

		public void setFoo(String foo) {
			this.foo = foo;
		}

		public String getFoo_bar() {
			return this.foo_bar;
		}

		public void setFoo_bar(String foo_bar) {
			this.foo_bar = foo_bar;
		}

		public String getFooBaz() {
			return this.fooBaz;
		}

		public void setFooBaz(String fooBaz) {
			this.fooBaz = fooBaz;
		}

		public Bingo getBingo() {
			return this.bingo;
		}

		public void setBingo(Bingo bingo) {
			this.bingo = bingo;
		}

		public List<Bingo> getBingos() {
			return this.bingos;
		}

		public void setBingos(List<Bingo> bingos) {
			this.bingos = bingos;
		}
	}

	enum Bingo {
		THIS, or, THAT, THE_OTHER, THAT_OTHER
	}

	public static class ValidatedTarget {

		@NotNull
		private String foo;

		public String getFoo() {
			return this.foo;
		}

		public void setFoo(String foo) {
			this.foo = foo;
		}

	}
}
