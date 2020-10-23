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

package org.springframework.boot.actuate.endpoint.cache;

import java.lang.reflect.Method;
import java.util.function.Function;

import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.springframework.boot.actuate.endpoint.OperationInvoker;
import org.springframework.boot.actuate.endpoint.OperationType;
import org.springframework.boot.actuate.endpoint.reflect.OperationMethodInfo;
import org.springframework.core.annotation.AnnotationAttributes;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.util.ReflectionUtils;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;

/**
 * Tests for {@link CachingOperationInvokerAdvisor}.
 *
 * @author Phillip Webb
 */
public class CachingOperationInvokerAdvisorTests {

	@Mock
	private OperationInvoker invoker;

	@Mock
	private Function<String, Long> timeToLive;

	private CachingOperationInvokerAdvisor advisor;

	@Before
	public void setup() {
		MockitoAnnotations.initMocks(this);
		this.advisor = new CachingOperationInvokerAdvisor(this.timeToLive);
	}

	@Test
	public void applyWhenOperationIsNotReadShouldNotAddAdvise() {
		OperationMethodInfo info = mockInfo(OperationType.WRITE, "get");
		OperationInvoker advised = this.advisor.apply("foo", info, this.invoker);
		assertThat(advised).isSameAs(this.invoker);
	}

	@Test
	public void applyWhenHasParametersShouldNotAddAdvise() {
		OperationMethodInfo info = mockInfo(OperationType.READ, "getWithParameter",
				String.class);
		OperationInvoker advised = this.advisor.apply("foo", info, this.invoker);
		assertThat(advised).isSameAs(this.invoker);
	}

	@Test
	public void applyWhenTimeToLiveReturnsNullShouldNotAddAdvise() {
		OperationMethodInfo info = mockInfo(OperationType.READ, "get");
		given(this.timeToLive.apply(any())).willReturn(null);
		OperationInvoker advised = this.advisor.apply("foo", info, this.invoker);
		assertThat(advised).isSameAs(this.invoker);
		verify(this.timeToLive).apply("foo");
	}

	@Test
	public void applyWhenTimeToLiveIsZeroShouldNotAddAdvise() {
		OperationMethodInfo info = mockInfo(OperationType.READ, "get");
		given(this.timeToLive.apply(any())).willReturn(0L);
		OperationInvoker advised = this.advisor.apply("foo", info, this.invoker);
		assertThat(advised).isSameAs(this.invoker);
		verify(this.timeToLive).apply("foo");
	}

	@Test
	public void applyShouldAddCacheAdvise() {
		OperationMethodInfo info = mockInfo(OperationType.READ, "get");
		given(this.timeToLive.apply(any())).willReturn(100L);
		OperationInvoker advised = this.advisor.apply("foo", info, this.invoker);
		assertThat(advised).isInstanceOf(CachingOperationInvoker.class);
		assertThat(ReflectionTestUtils.getField(advised, "target"))
				.isEqualTo(this.invoker);
		assertThat(ReflectionTestUtils.getField(advised, "timeToLive")).isEqualTo(100L);
	}

	private OperationMethodInfo mockInfo(OperationType operationType, String methodName,
			Class<?>... parameterTypes) {
		Method method = ReflectionUtils.findMethod(TestOperations.class, methodName,
				parameterTypes);
		return new OperationMethodInfo(method, operationType, new AnnotationAttributes());
	}

	public static class TestOperations {

		public String get() {
			return "";
		}

		public String getWithParameter(String foo) {
			return "";
		}

	}

}
