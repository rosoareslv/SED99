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

package org.springframework.boot.actuate.endpoint;

import java.util.Collections;
import java.util.Map;

import org.junit.Test;

import org.springframework.boot.endpoint.EndpointInfo;
import org.springframework.boot.endpoint.OperationType;
import org.springframework.boot.endpoint.web.OperationRequestPredicate;
import org.springframework.boot.endpoint.web.WebEndpointHttpMethod;
import org.springframework.boot.endpoint.web.WebEndpointOperation;
import org.springframework.boot.endpoint.web.mvc.WebEndpointServletHandlerMapping;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Lazy;
import org.springframework.context.annotation.Scope;
import org.springframework.context.annotation.ScopedProxyMode;
import org.springframework.context.support.StaticApplicationContext;
import org.springframework.web.servlet.handler.AbstractHandlerMethodMapping;
import org.springframework.web.servlet.handler.AbstractUrlHandlerMapping;
import org.springframework.web.servlet.handler.SimpleUrlHandlerMapping;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tests for {@link RequestMappingEndpoint}.
 *
 * @author Dave Syer
 */
public class RequestMappingEndpointTests {

	private RequestMappingEndpoint endpoint = new RequestMappingEndpoint();

	@Test
	public void concreteUrlMappings() {
		SimpleUrlHandlerMapping mapping = new SimpleUrlHandlerMapping();
		mapping.setUrlMap(Collections.singletonMap("/foo", new Object()));
		mapping.setApplicationContext(new StaticApplicationContext());
		mapping.initApplicationContext();
		this.endpoint.setHandlerMappings(
				Collections.<AbstractUrlHandlerMapping>singletonList(mapping));
		Map<String, Object> result = this.endpoint.mappings();
		assertThat(result).hasSize(1);
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) result.get("/foo");
		assertThat(map.get("type")).isEqualTo("java.lang.Object");
	}

	@Test
	public void beanUrlMappings() {
		StaticApplicationContext context = new StaticApplicationContext();
		SimpleUrlHandlerMapping mapping = new SimpleUrlHandlerMapping();
		mapping.setUrlMap(Collections.singletonMap("/foo", new Object()));
		mapping.setApplicationContext(context);
		mapping.initApplicationContext();
		context.getDefaultListableBeanFactory().registerSingleton("mapping", mapping);
		this.endpoint.setApplicationContext(context);
		Map<String, Object> result = this.endpoint.mappings();
		assertThat(result).hasSize(1);
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) result.get("/foo");
		assertThat(map.get("bean")).isEqualTo("mapping");
	}

	@Test
	public void beanUrlMappingsProxy() {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				MappingConfiguration.class);
		this.endpoint.setApplicationContext(context);
		Map<String, Object> result = this.endpoint.mappings();
		assertThat(result).hasSize(1);
		@SuppressWarnings("unchecked")
		Map<String, Object> map = (Map<String, Object>) result.get("/foo");
		assertThat(map.get("bean")).isEqualTo("scopedTarget.mapping");
	}

	@SuppressWarnings("unchecked")
	@Test
	public void beanMethodMappings() {
		StaticApplicationContext context = new StaticApplicationContext();
		context.getDefaultListableBeanFactory().registerSingleton("mapping",
				createHandlerMapping());
		this.endpoint.setApplicationContext(context);
		Map<String, Object> result = this.endpoint.mappings();
		assertThat(result).hasSize(2);
		assertThat(result.keySet())
				.filteredOn((key) -> key.contains("[/application/test]"))
				.hasOnlyOneElementSatisfying(
						(key) -> assertThat((Map<String, Object>) result.get(key))
								.containsOnlyKeys("bean", "method"));
		assertThat(result.keySet()).filteredOn((key) -> key.contains("[/application]"))
				.hasOnlyOneElementSatisfying(
						(key) -> assertThat((Map<String, Object>) result.get(key))
								.containsOnlyKeys("bean", "method"));
	}

	@SuppressWarnings("unchecked")
	@Test
	public void concreteMethodMappings() {
		WebEndpointServletHandlerMapping mapping = createHandlerMapping();
		this.endpoint.setMethodMappings(
				Collections.<AbstractHandlerMethodMapping<?>>singletonList(mapping));
		Map<String, Object> result = this.endpoint.mappings();
		assertThat(result).hasSize(2);
		assertThat(result.keySet())
				.filteredOn((key) -> key.contains("[/application/test]"))
				.hasOnlyOneElementSatisfying(
						(key) -> assertThat((Map<String, Object>) result.get(key))
								.containsOnlyKeys("method"));
		assertThat(result.keySet()).filteredOn((key) -> key.contains("[/application]"))
				.hasOnlyOneElementSatisfying(
						(key) -> assertThat((Map<String, Object>) result.get(key))
								.containsOnlyKeys("method"));
	}

	private WebEndpointServletHandlerMapping createHandlerMapping() {
		OperationRequestPredicate requestPredicate = new OperationRequestPredicate("test",
				WebEndpointHttpMethod.GET, Collections.singletonList("application/json"),
				Collections.singletonList("application/json"));
		WebEndpointOperation operation = new WebEndpointOperation(OperationType.READ,
				(arguments) -> "Invoked", true, requestPredicate, "test");
		WebEndpointServletHandlerMapping mapping = new WebEndpointServletHandlerMapping(
				"application", Collections.singleton(new EndpointInfo<>("test", true,
						Collections.singleton(operation))));
		mapping.setApplicationContext(new StaticApplicationContext());
		mapping.afterPropertiesSet();
		return mapping;
	}

	@Configuration
	protected static class MappingConfiguration {

		@Bean
		@Lazy
		@Scope(proxyMode = ScopedProxyMode.TARGET_CLASS)
		public AbstractUrlHandlerMapping mapping() {
			SimpleUrlHandlerMapping mapping = new SimpleUrlHandlerMapping();
			mapping.setUrlMap(Collections.singletonMap("/foo", new Object()));
			return mapping;
		}

	}

}
