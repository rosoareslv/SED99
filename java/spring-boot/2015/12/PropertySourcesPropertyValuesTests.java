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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.junit.Before;
import org.junit.Test;

import org.springframework.beans.PropertyValue;
import org.springframework.core.env.CompositePropertySource;
import org.springframework.core.env.MapPropertySource;
import org.springframework.core.env.MutablePropertySources;
import org.springframework.core.env.PropertySource;
import org.springframework.validation.DataBinder;

import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;

/**
 * Tests for {@link PropertySourcesPropertyValues}.
 *
 * @author Dave Syer
 * @author Phillip Webb
 */
public class PropertySourcesPropertyValuesTests {

	private final MutablePropertySources propertySources = new MutablePropertySources();

	@Before
	public void init() {
		this.propertySources.addFirst(new PropertySource<String>("static", "foo") {

			@Override
			public Object getProperty(String name) {
				if (name.equals(getSource())) {
					return "bar";
				}
				return null;
			}

		});
		this.propertySources.addFirst(new MapPropertySource("map",
				Collections.<String, Object>singletonMap("name", "${foo}")));
	}

	@Test
	public void testTypesPreserved() {
		Map<String, Object> map = Collections.<String, Object>singletonMap("name", 123);
		this.propertySources.replace("map", new MapPropertySource("map", map));
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals(123, propertyValues.getPropertyValues()[0].getValue());
	}

	@Test
	public void testSize() {
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals(1, propertyValues.getPropertyValues().length);
	}

	@Test
	public void testOrderPreserved() {
		LinkedHashMap<String, Object> map = new LinkedHashMap<String, Object>();
		map.put("one", 1);
		map.put("two", 2);
		map.put("three", 3);
		map.put("four", 4);
		map.put("five", 5);
		this.propertySources.addFirst(new MapPropertySource("ordered", map));
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		PropertyValue[] values = propertyValues.getPropertyValues();
		assertEquals(6, values.length);
		Collection<String> names = new ArrayList<String>();
		for (PropertyValue value : values) {
			names.add(value.getName());
		}
		assertEquals("[one, two, three, four, five, name]", names.toString());
	}

	@Test
	public void testNonEnumeratedValue() {
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals("bar", propertyValues.getPropertyValue("foo").getValue());
	}

	@Test
	public void testCompositeValue() {
		PropertySource<?> map = this.propertySources.get("map");
		CompositePropertySource composite = new CompositePropertySource("composite");
		composite.addPropertySource(map);
		this.propertySources.replace("map", composite);
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals("bar", propertyValues.getPropertyValue("foo").getValue());
	}

	@Test
	public void testEnumeratedValue() {
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals("bar", propertyValues.getPropertyValue("name").getValue());
	}

	@Test
	public void testNonEnumeratedPlaceholder() {
		this.propertySources.addFirst(new PropertySource<String>("another", "baz") {

			@Override
			public Object getProperty(String name) {
				if (name.equals(getSource())) {
					return "${foo}";
				}
				return null;
			}

		});
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources, (Collection<String>) null,
				Collections.singleton("baz"));
		assertEquals("bar", propertyValues.getPropertyValue("baz").getValue());
	}

	@Test
	public void testOverriddenValue() {
		this.propertySources.addFirst(new MapPropertySource("new",
				Collections.<String, Object>singletonMap("name", "spam")));
		PropertySourcesPropertyValues propertyValues = new PropertySourcesPropertyValues(
				this.propertySources);
		assertEquals("spam", propertyValues.getPropertyValue("name").getValue());
	}

	@Test
	public void testPlaceholdersBinding() {
		TestBean target = new TestBean();
		DataBinder binder = new DataBinder(target);
		binder.bind(new PropertySourcesPropertyValues(this.propertySources));
		assertEquals("bar", target.getName());
	}

	@Test
	public void testPlaceholdersBindingNonEnumerable() {
		FooBean target = new FooBean();
		DataBinder binder = new DataBinder(target);
		binder.bind(new PropertySourcesPropertyValues(this.propertySources,
				(Collection<String>) null, Collections.singleton("foo")));
		assertEquals("bar", target.getFoo());
	}

	@Test
	public void testPlaceholdersBindingWithError() {
		TestBean target = new TestBean();
		DataBinder binder = new DataBinder(target);
		this.propertySources.addFirst(new MapPropertySource("another",
				Collections.<String, Object>singletonMap("something", "${nonexistent}")));
		binder.bind(new PropertySourcesPropertyValues(this.propertySources));
		assertEquals("bar", target.getName());
	}

	@Test
	public void testPlaceholdersErrorInNonEnumerable() {
		TestBean target = new TestBean();
		DataBinder binder = new DataBinder(target);
		this.propertySources.addFirst(new PropertySource<Object>("application", "STUFF") {

			@Override
			public Object getProperty(String name) {
				return new Object();
			}

		});
		binder.bind(new PropertySourcesPropertyValues(this.propertySources,
				(Collection<String>) null, Collections.singleton("name")));
		assertEquals(null, target.getName());
	}

	@Test
	public void testCollectionProperty() throws Exception {
		ListBean target = new ListBean();
		DataBinder binder = new DataBinder(target);
		Map<String, Object> map = new LinkedHashMap<String, Object>();
		map.put("list[0]", "v0");
		map.put("list[1]", "v1");
		this.propertySources.addFirst(new MapPropertySource("values", map));
		binder.bind(new PropertySourcesPropertyValues(this.propertySources));
		assertThat(target.getList(), equalTo(Arrays.asList("v0", "v1")));
	}

	@Test
	public void testFirstCollectionPropertyWins() throws Exception {
		ListBean target = new ListBean();
		DataBinder binder = new DataBinder(target);
		Map<String, Object> first = new LinkedHashMap<String, Object>();
		first.put("list[0]", "f0");
		Map<String, Object> second = new LinkedHashMap<String, Object>();
		second.put("list[0]", "s0");
		second.put("list[1]", "s1");
		this.propertySources.addFirst(new MapPropertySource("s", second));
		this.propertySources.addFirst(new MapPropertySource("f", first));
		binder.bind(new PropertySourcesPropertyValues(this.propertySources));
		assertThat(target.getList(), equalTo(Collections.singletonList("f0")));
	}

	public static class TestBean {

		private String name;

		public String getName() {
			return this.name;
		}

		public void setName(String name) {
			this.name = name;
		}
	}

	public static class FooBean {

		private String foo;

		public String getFoo() {
			return this.foo;
		}

		public void setFoo(String foo) {
			this.foo = foo;
		}

	}

	public static class ListBean {

		private List<String> list = new ArrayList<String>();

		public List<String> getList() {
			return this.list;
		}

		public void setList(List<String> list) {
			this.list = list;
		}
	}

}
