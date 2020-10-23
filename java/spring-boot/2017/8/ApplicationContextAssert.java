/*
 * Copyright 2012-2017 the original author or authors.
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

package org.springframework.boot.test.context.assertj;

import org.assertj.core.api.AbstractAssert;
import org.assertj.core.api.AbstractObjectArrayAssert;
import org.assertj.core.api.AbstractObjectAssert;
import org.assertj.core.api.AbstractThrowableAssert;
import org.assertj.core.api.Assertions;
import org.assertj.core.api.MapAssert;
import org.assertj.core.error.BasicErrorMessageFactory;

import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.context.ApplicationContext;
import org.springframework.util.Assert;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * AssertJ {@link org.assertj.core.api.Assert assertions} that can be applied to an
 * {@link ApplicationContext}.
 *
 * @param <C> The application context type
 * @author Phillip Webb
 * @since 2.0.0
 * @see ApplicationContextRunner
 * @see AssertableApplicationContext
 */
public class ApplicationContextAssert<C extends ApplicationContext>
		extends AbstractAssert<ApplicationContextAssert<C>, C> {

	private final Throwable startupFailure;

	/**
	 * Create a new {@link ApplicationContextAssert} instance.
	 * @param applicationContext the source application context
	 * @param startupFailure the startup failure or {@code null}
	 */
	ApplicationContextAssert(C applicationContext, Throwable startupFailure) {
		super(applicationContext, ApplicationContextAssert.class);
		Assert.notNull(applicationContext, "ApplicationContext must not be null");
		this.startupFailure = startupFailure;
	}

	/**
	 * Verifies that the application context contains a bean with the given name.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).hasBean("fooBean"); </pre>
	 * @param name the name of the bean
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context does not contain a bean with the
	 * given name
	 */
	public ApplicationContextAssert<C> hasBean(String name) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have bean named:%n <%s>%nbut context failed to start",
					getApplicationContext(), name));
		}
		if (findBean(name) == null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have bean named:%n <%s>%nbut found no such bean",
					getApplicationContext(), name));
		}
		return this;
	}

	/**
	 * Verifies that the application context contains a single bean with the given type.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).hasSingleBean(Foo.class); </pre>
	 * @param type the bean type
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context does no beans of the given type
	 * @throws AssertionError if the application context contains multiple beans of the
	 * given type
	 */
	public ApplicationContextAssert<C> hasSingleBean(Class<?> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have a single bean of type:%n <%s>%nbut context failed to start",
					getApplicationContext(), type));
		}
		String[] names = getApplicationContext().getBeanNamesForType(type);
		if (names.length == 0) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have a single bean of type:%n <%s>%nbut found no beans of that type",
					getApplicationContext(), type));
		}
		if (names.length > 1) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have a single bean of type:%n <%s>%nbut found:%n <%s>",
					getApplicationContext(), type, names));
		}
		return this;
	}

	/**
	 * Verifies that the application context does not contain any beans of the given type.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).doesNotHaveBean(Foo.class); </pre>
	 * @param type the bean type
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context contains any beans of the given
	 * type
	 */
	public ApplicationContextAssert<C> doesNotHaveBean(Class<?> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nnot to have any beans of type:%n <%s>%nbut context failed to start",
					getApplicationContext(), type));
		}
		String[] names = getApplicationContext().getBeanNamesForType(type);
		if (names.length > 0) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nnot to have a beans of type:%n <%s>%nbut found:%n <%s>",
					getApplicationContext(), type, names));
		}
		return this;
	}

	/**
	 * Verifies that the application context does not contain a beans of the given name.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).doesNotHaveBean("fooBean"); </pre>
	 * @param name the name of the bean
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context contains a beans of the given
	 * name
	 */
	public ApplicationContextAssert<C> doesNotHaveBean(String name) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nnot to have any beans of name:%n <%s>%nbut context failed to start",
					getApplicationContext(), name));
		}
		try {
			Object bean = getApplicationContext().getBean(name);
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nnot to have a bean of name:%n <%s>%nbut found:%n <%s>",
					getApplicationContext(), name, bean));
		}
		catch (NoSuchBeanDefinitionException ex) {
		}
		return this;
	}

	/**
	 * Obtain the beans names of the given type from the application context, the names
	 * becoming the object array under test.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getBeanNames(Foo.class).containsOnly("fooBean"); </pre>
	 * @param <T> the bean type
	 * @param type the bean type
	 * @return array assertions for the bean names
	 * @throws AssertionError if the application context did not start
	 */
	public <T> AbstractObjectArrayAssert<?, String> getBeanNames(Class<T> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto get beans names with type:%n <%s>%nbut context failed to start",
					getApplicationContext(), type));
		}
		return Assertions.assertThat(getApplicationContext().getBeanNamesForType(type))
				.as("Bean names of type <%s> from <%s>", type, getApplicationContext());
	}

	/**
	 * Obtain a single bean of the given type from the application context, the bean
	 * becoming the object under test. If no beans of the specified type can be found an
	 * assert on {@code null} is returned.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getBean(Foo.class).isInstanceOf(DefaultFoo.class);
	 * assertThat(context).getBean(Bar.class).isNull();</pre>
	 * @param <T> the bean type
	 * @param type the bean type
	 * @return bean assertions for the bean, or an assert on {@code null} if the no bean
	 * is found
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context contains multiple beans of the
	 * given type
	 */
	public <T> AbstractObjectAssert<?, T> getBean(Class<T> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto contain bean of type:%n <%s>%nbut context failed to start",
					getApplicationContext(), type));
		}
		String[] names = getApplicationContext().getBeanNamesForType(type);
		if (names.length > 1) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nsingle bean of type:%n <%s>%nbut found:%n <%s>",
					getApplicationContext(), type, names));
		}
		T bean = (names.length == 0 ? null
				: getApplicationContext().getBean(names[0], type));
		return Assertions.assertThat(bean).as("Bean of type <%s> from <%s>", type,
				getApplicationContext());
	}

	/**
	 * Obtain a single bean of the given name from the application context, the bean
	 * becoming the object under test. If no bean of the specified name can be found an
	 * assert on {@code null} is returned.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getBean("foo").isInstanceOf(Foo.class);
	 * assertThat(context).getBean("foo").isNull();</pre>
	 * @param name the name of the bean
	 * @return bean assertions for the bean, or an assert on {@code null} if the no bean
	 * is found
	 * @throws AssertionError if the application context did not start
	 */
	public AbstractObjectAssert<?, Object> getBean(String name) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto contain a bean of name:%n <%s>%nbut context failed to start",
					getApplicationContext(), name));
		}
		Object bean = findBean(name);
		return Assertions.assertThat(bean).as("Bean of name <%s> from <%s>", name,
				getApplicationContext());
	}

	/**
	 * Obtain a single bean of the given name and type from the application context, the
	 * bean becoming the object under test. If no bean of the specified name can be found
	 * an assert on {@code null} is returned.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getBean("foo", Foo.class).isInstanceOf(DefaultFoo.class);
	 * assertThat(context).getBean("foo", Foo.class).isNull();</pre>
	 * @param <T> the bean type
	 * @param name the name of the bean
	 * @param type the bean type
	 * @return bean assertions for the bean, or an assert on {@code null} if the no bean
	 * is found
	 * @throws AssertionError if the application context did not start
	 * @throws AssertionError if the application context contains a bean with the given
	 * name but a different type
	 */
	@SuppressWarnings("unchecked")
	public <T> AbstractObjectAssert<?, T> getBean(String name, Class<T> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto contain a bean of name:%n <%s> (%s)%nbut context failed to start",
					getApplicationContext(), name, type));
		}
		Object bean = findBean(name);
		if (bean != null && type != null && !type.isInstance(bean)) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto contain a bean of name:%n <%s> (%s)%nbut found:%n <%s> of type <%s>",
					getApplicationContext(), name, type, bean, bean.getClass()));
		}
		return Assertions.assertThat((T) bean).as(
				"Bean of name <%s> and type <%s> from <%s>", name, type,
				getApplicationContext());
	}

	private Object findBean(String name) {
		try {
			return getApplicationContext().getBean(name);
		}
		catch (NoSuchBeanDefinitionException ex) {
			return null;
		}
	}

	/**
	 * Obtain a map bean names and instances of the given type from the application
	 * context, the map becoming the object under test. If no bean of the specified type
	 * can be found an assert on an empty {@code map} is returned.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getBeans(Foo.class).containsKey("foo");
	 * </pre>
	 * @param <T> the bean type
	 * @param type the bean type
	 * @return bean assertions for the beans, or an assert on an empty {@code map} if the
	 * no beans are found
	 * @throws AssertionError if the application context did not start
	 */
	public <T> MapAssert<String, T> getBeans(Class<T> type) {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto get beans of type:%n <%s> (%s)%nbut context failed to start",
					getApplicationContext(), type, type));
		}
		return Assertions.assertThat(getApplicationContext().getBeansOfType(type))
				.as("Beans of type <%s> from <%s>", type, getApplicationContext());
	}

	/**
	 * Obtain the failure that stopped the application context from running, the failure
	 * becoming the object under test.
	 * <p>
	 * Example: <pre class="code">
	 * assertThat(context).getFailure().containsMessage("missing bean");
	 * </pre>
	 * @return assertions on the cause of the failure
	 * @throws AssertionError if the application context started without a failure
	 */
	public AbstractThrowableAssert<?, ? extends Throwable> getFailure() {
		hasFailed();
		return assertThat(this.startupFailure);
	}

	/**
	 * Verifies that the application has failed to start.
	 * <p>
	 * Example: <pre class="code"> assertThat(context).hasFailed();
	 * </pre>
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context started without a failure
	 */
	public ApplicationContextAssert<C> hasFailed() {
		if (this.startupFailure == null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have failed%nbut context started successfully",
					getApplicationContext()));
		}
		return this;
	}

	/**
	 * Verifies that the application has not failed to start.
	 * <p>
	 * Example: <pre class="code"> assertThat(context).hasNotFailed();
	 * </pre>
	 * @return {@code this} assertion object.
	 * @throws AssertionError if the application context failed to start
	 */
	public ApplicationContextAssert<C> hasNotFailed() {
		if (this.startupFailure != null) {
			throwAssertionError(new BasicErrorMessageFactory(
					"%nExpecting:%n <%s>%nto have not failed:%nbut context failed to start",
					getApplicationContext()));
		}
		return this;
	}

	protected final C getApplicationContext() {
		return this.actual;
	}

	protected final Throwable getStartupFailure() {
		return this.startupFailure;
	}

}
