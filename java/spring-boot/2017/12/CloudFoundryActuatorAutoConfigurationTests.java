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

package org.springframework.boot.actuate.autoconfigure.cloudfoundry.servlet;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import org.springframework.boot.actuate.autoconfigure.cloudfoundry.CloudFoundryHealthWebEndpointAutoConfiguration;
import org.springframework.boot.actuate.autoconfigure.endpoint.EndpointAutoConfiguration;
import org.springframework.boot.actuate.autoconfigure.endpoint.web.WebEndpointAutoConfiguration;
import org.springframework.boot.actuate.autoconfigure.health.HealthEndpointAutoConfiguration;
import org.springframework.boot.actuate.autoconfigure.web.server.ManagementContextAutoConfiguration;
import org.springframework.boot.actuate.autoconfigure.web.servlet.ServletManagementContextAutoConfiguration;
import org.springframework.boot.actuate.endpoint.EndpointInfo;
import org.springframework.boot.actuate.endpoint.annotation.Endpoint;
import org.springframework.boot.actuate.endpoint.annotation.ReadOperation;
import org.springframework.boot.actuate.endpoint.http.ActuatorMediaType;
import org.springframework.boot.actuate.endpoint.reflect.ReflectiveOperationInvoker;
import org.springframework.boot.actuate.endpoint.web.WebOperation;
import org.springframework.boot.autoconfigure.context.PropertyPlaceholderAutoConfiguration;
import org.springframework.boot.autoconfigure.http.HttpMessageConvertersAutoConfiguration;
import org.springframework.boot.autoconfigure.jackson.JacksonAutoConfiguration;
import org.springframework.boot.autoconfigure.security.SecurityAutoConfiguration;
import org.springframework.boot.autoconfigure.web.client.RestTemplateAutoConfiguration;
import org.springframework.boot.autoconfigure.web.servlet.DispatcherServletAutoConfiguration;
import org.springframework.boot.autoconfigure.web.servlet.WebMvcAutoConfiguration;
import org.springframework.boot.context.properties.source.ConfigurationPropertySources;
import org.springframework.boot.test.util.TestPropertyValues;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpMethod;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockServletContext;
import org.springframework.security.config.BeanIds;
import org.springframework.security.web.FilterChainProxy;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.context.support.AnnotationConfigWebApplicationContext;
import org.springframework.web.cors.CorsConfiguration;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;

/**
 * Tests for {@link CloudFoundryActuatorAutoConfiguration}.
 *
 * @author Madhura Bhave
 */
public class CloudFoundryActuatorAutoConfigurationTests {

	private AnnotationConfigWebApplicationContext context;

	@Before
	public void setup() {
		this.context = new AnnotationConfigWebApplicationContext();
		this.context.setServletContext(new MockServletContext());
		this.context.register(SecurityAutoConfiguration.class,
				WebMvcAutoConfiguration.class, JacksonAutoConfiguration.class,
				DispatcherServletAutoConfiguration.class,
				HttpMessageConvertersAutoConfiguration.class,
				PropertyPlaceholderAutoConfiguration.class,
				RestTemplateAutoConfiguration.class,
				ManagementContextAutoConfiguration.class,
				ServletManagementContextAutoConfiguration.class,
				EndpointAutoConfiguration.class, WebEndpointAutoConfiguration.class,
				CloudFoundryActuatorAutoConfiguration.class);
	}

	@After
	public void close() {
		if (this.context != null) {
			this.context.close();
		}
	}

	@Test
	public void cloudFoundryPlatformActive() {
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		assertThat(handlerMapping.getEndpointMapping().getPath())
				.isEqualTo("/cloudfoundryapplication");
		CorsConfiguration corsConfiguration = (CorsConfiguration) ReflectionTestUtils
				.getField(handlerMapping, "corsConfiguration");
		assertThat(corsConfiguration.getAllowedOrigins()).contains("*");
		assertThat(corsConfiguration.getAllowedMethods()).containsAll(
				Arrays.asList(HttpMethod.GET.name(), HttpMethod.POST.name()));
		assertThat(corsConfiguration.getAllowedHeaders()).containsAll(
				Arrays.asList("Authorization", "X-Cf-App-Instance", "Content-Type"));
	}

	@Test
	public void cloudfoundryapplicationProducesActuatorMediaType() throws Exception {
		TestPropertyValues
				.of("VCAP_APPLICATION:---", "vcap.application.application_id:my-app-id",
						"vcap.application.cf_api:http://my-cloud-controller.com")
				.applyTo(this.context);
		this.context.refresh();
		MockMvc mockMvc = MockMvcBuilders.webAppContextSetup(this.context).build();
		mockMvc.perform(get("/cloudfoundryapplication")).andExpect(header()
				.string("Content-Type", ActuatorMediaType.V2_JSON + ";charset=UTF-8"));
	}

	@Test
	public void cloudFoundryPlatformActiveSetsApplicationId() {
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		Object interceptor = ReflectionTestUtils.getField(handlerMapping,
				"securityInterceptor");
		String applicationId = (String) ReflectionTestUtils.getField(interceptor,
				"applicationId");
		assertThat(applicationId).isEqualTo("my-app-id");
	}

	@Test
	public void cloudFoundryPlatformActiveSetsCloudControllerUrl() {
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		Object interceptor = ReflectionTestUtils.getField(handlerMapping,
				"securityInterceptor");
		Object interceptorSecurityService = ReflectionTestUtils.getField(interceptor,
				"cloudFoundrySecurityService");
		String cloudControllerUrl = (String) ReflectionTestUtils
				.getField(interceptorSecurityService, "cloudControllerUrl");
		assertThat(cloudControllerUrl).isEqualTo("http://my-cloud-controller.com");
	}

	@Test
	public void skipSslValidation() {
		TestPropertyValues.of("management.cloudfoundry.skipSslValidation:true")
				.applyTo(this.context);
		ConfigurationPropertySources.attach(this.context.getEnvironment());
		this.context.refresh();
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		Object interceptor = ReflectionTestUtils.getField(handlerMapping,
				"securityInterceptor");
		Object interceptorSecurityService = ReflectionTestUtils.getField(interceptor,
				"cloudFoundrySecurityService");
		RestTemplate restTemplate = (RestTemplate) ReflectionTestUtils
				.getField(interceptorSecurityService, "restTemplate");
		assertThat(restTemplate.getRequestFactory())
				.isInstanceOf(SkipSslVerificationHttpRequestFactory.class);
	}

	@Test
	public void cloudFoundryPlatformActiveAndCloudControllerUrlNotPresent() {
		TestPropertyValues
				.of("VCAP_APPLICATION:---", "vcap.application.application_id:my-app-id")
				.applyTo(this.context);
		this.context.refresh();
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = this.context
				.getBean("cloudFoundryWebEndpointServletHandlerMapping",
						CloudFoundryWebEndpointServletHandlerMapping.class);
		Object securityInterceptor = ReflectionTestUtils.getField(handlerMapping,
				"securityInterceptor");
		Object interceptorSecurityService = ReflectionTestUtils
				.getField(securityInterceptor, "cloudFoundrySecurityService");
		assertThat(interceptorSecurityService).isNull();
	}

	@Test
	public void cloudFoundryPathsIgnoredBySpringSecurity() {
		TestPropertyValues
				.of("VCAP_APPLICATION:---", "vcap.application.application_id:my-app-id")
				.applyTo(this.context);
		this.context.refresh();
		FilterChainProxy securityFilterChain = (FilterChainProxy) this.context
				.getBean(BeanIds.SPRING_SECURITY_FILTER_CHAIN);
		SecurityFilterChain chain = securityFilterChain.getFilterChains().get(0);
		MockHttpServletRequest request = new MockHttpServletRequest();
		request.setServletPath("/cloudfoundryapplication/my-path");
		assertThat(chain.getFilters()).isEmpty();
		assertThat(chain.matches(request)).isTrue();
		request.setServletPath("/some-other-path");
		assertThat(chain.matches(request)).isFalse();
	}

	@Test
	public void cloudFoundryPlatformInactive() {
		this.context.refresh();
		assertThat(
				this.context.containsBean("cloudFoundryWebEndpointServletHandlerMapping"))
						.isFalse();
	}

	@Test
	public void cloudFoundryManagementEndpointsDisabled() {
		TestPropertyValues
				.of("VCAP_APPLICATION=---", "management.cloudfoundry.enabled:false")
				.applyTo(this.context);
		this.context.refresh();
		assertThat(this.context.containsBean("cloudFoundryEndpointHandlerMapping"))
				.isFalse();
	}

	@Test
	public void allEndpointsAvailableUnderCloudFoundryWithoutExposeAllOnWeb() {
		this.context.register(TestConfiguration.class);
		this.context.refresh();
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		List<EndpointInfo<WebOperation>> endpoints = (List<EndpointInfo<WebOperation>>) handlerMapping
				.getEndpoints();
		assertThat(endpoints.stream()
				.filter((candidate) -> "test".equals(candidate.getId())).findFirst())
						.isNotEmpty();
	}

	@Test
	public void endpointPathCustomizationIsNotApplied() {
		TestPropertyValues.of("management.endpoints.web.path-mapping.test=custom")
				.applyTo(this.context);
		this.context.register(TestConfiguration.class);
		this.context.refresh();
		CloudFoundryWebEndpointServletHandlerMapping handlerMapping = getHandlerMapping();
		List<EndpointInfo<WebOperation>> endpoints = (List<EndpointInfo<WebOperation>>) handlerMapping
				.getEndpoints();
		EndpointInfo<WebOperation> endpoint = endpoints.stream()
				.filter((candidate) -> "test".equals(candidate.getId())).findFirst()
				.get();
		Collection<WebOperation> operations = endpoint.getOperations();
		assertThat(operations).hasSize(1);
		assertThat(operations.iterator().next().getRequestPredicate().getPath())
				.isEqualTo("test");
	}

	@Test
	public void healthEndpointInvokerShouldBeCloudFoundryWebExtension() {
		TestPropertyValues
				.of("VCAP_APPLICATION:---", "vcap.application.application_id:my-app-id",
						"vcap.application.cf_api:http://my-cloud-controller.com")
				.applyTo(this.context);
		this.context.register(HealthEndpointAutoConfiguration.class,
				CloudFoundryHealthWebEndpointAutoConfiguration.class);
		this.context.refresh();
		Collection<EndpointInfo<WebOperation>> endpoints = this.context
				.getBean("cloudFoundryWebEndpointServletHandlerMapping",
						CloudFoundryWebEndpointServletHandlerMapping.class)
				.getEndpoints();
		EndpointInfo<WebOperation> endpointInfo = endpoints.iterator().next();
		WebOperation webOperation = endpointInfo.getOperations().iterator().next();
		ReflectiveOperationInvoker invoker = (ReflectiveOperationInvoker) webOperation
				.getInvoker();
		assertThat(ReflectionTestUtils.getField(invoker, "target"))
				.isInstanceOf(CloudFoundryHealthEndpointWebExtension.class);
	}

	private CloudFoundryWebEndpointServletHandlerMapping getHandlerMapping() {
		TestPropertyValues
				.of("VCAP_APPLICATION:---", "vcap.application.application_id:my-app-id",
						"vcap.application.cf_api:http://my-cloud-controller.com")
				.applyTo(this.context);
		this.context.refresh();
		return this.context.getBean("cloudFoundryWebEndpointServletHandlerMapping",
				CloudFoundryWebEndpointServletHandlerMapping.class);
	}

	@Configuration
	static class TestConfiguration {

		@Bean
		public TestEndpoint testEndpoint() {
			return new TestEndpoint();
		}

	}

	@Endpoint(id = "test")
	static class TestEndpoint {

		@ReadOperation
		public String hello() {
			return "hello world";
		}

	}

}
