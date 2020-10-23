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

package org.springframework.boot.actuate.autoconfigure;

import java.io.IOException;

import javax.servlet.Filter;
import javax.servlet.FilterChain;
import javax.servlet.ServletException;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.junit.Test;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.springframework.boot.actuate.metrics.CounterService;
import org.springframework.boot.actuate.metrics.GaugeService;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.annotation.Order;
import org.springframework.http.HttpStatus;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.stereotype.Component;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.ResponseBody;
import org.springframework.web.bind.annotation.ResponseStatus;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.filter.OncePerRequestFilter;
import org.springframework.web.util.NestedServletException;

import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertThat;
import static org.mockito.BDDMockito.willAnswer;
import static org.mockito.BDDMockito.willThrow;
import static org.mockito.Matchers.anyDouble;
import static org.mockito.Matchers.anyString;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

/**
 * Tests for {@link MetricFilterAutoConfiguration}.
 *
 * @author Phillip Webb
 * @author Andy Wilkinson
 */
public class MetricFilterAutoConfigurationTests {

	@Test
	public void recordsHttpInteractions() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		Filter filter = context.getBean(Filter.class);
		final MockHttpServletRequest request = new MockHttpServletRequest("GET",
				"/test/path");
		final MockHttpServletResponse response = new MockHttpServletResponse();
		FilterChain chain = mock(FilterChain.class);
		willAnswer(new Answer<Object>() {
			@Override
			public Object answer(InvocationOnMock invocation) throws Throwable {
				response.setStatus(200);
				return null;
			}
		}).given(chain).doFilter(request, response);
		filter.doFilter(request, response, chain);
		verify(context.getBean(CounterService.class)).increment("status.200.test.path");
		verify(context.getBean(GaugeService.class)).submit(eq("response.test.path"),
				anyDouble());
		context.close();
	}

	@Test
	public void recordsHttpInteractionsWithTemplateVariable() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		Filter filter = context.getBean(Filter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).build();
		mvc.perform(get("/templateVarTest/foo")).andExpect(status().isOk());
		verify(context.getBean(CounterService.class)).increment(
				"status.200.templateVarTest.someVariable");
		verify(context.getBean(GaugeService.class)).submit(
				eq("response.templateVarTest.someVariable"), anyDouble());
		context.close();
	}

	@Test
	public void recordsKnown404HttpInteractionsAsSingleMetricWithPathAndTemplateVariable()
			throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		Filter filter = context.getBean(Filter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).build();
		mvc.perform(get("/knownPath/foo")).andExpect(status().isNotFound());
		verify(context.getBean(CounterService.class)).increment(
				"status.404.knownPath.someVariable");
		verify(context.getBean(GaugeService.class)).submit(
				eq("response.knownPath.someVariable"), anyDouble());
		context.close();
	}

	@Test
	public void records404HttpInteractionsAsSingleMetric() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		Filter filter = context.getBean(Filter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).build();
		mvc.perform(get("/unknownPath/1")).andExpect(status().isNotFound());
		mvc.perform(get("/unknownPath/2")).andExpect(status().isNotFound());
		verify(context.getBean(CounterService.class), times(2)).increment(
				"status.404.unmapped");
		verify(context.getBean(GaugeService.class), times(2)).submit(
				eq("response.unmapped"), anyDouble());
		context.close();
	}

	@Test
	public void records302HttpInteractionsAsSingleMetric() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class, RedirectFilter.class);
		MetricsFilter filter = context.getBean(MetricsFilter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).addFilter(context.getBean(RedirectFilter.class))
				.build();
		mvc.perform(get("/unknownPath/1")).andExpect(status().is3xxRedirection());
		mvc.perform(get("/unknownPath/2")).andExpect(status().is3xxRedirection());
		verify(context.getBean(CounterService.class), times(2)).increment(
				"status.302.unmapped");
		verify(context.getBean(GaugeService.class), times(2)).submit(
				eq("response.unmapped"), anyDouble());
		context.close();
	}

	@Test
	public void skipsFilterIfMissingServices() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				MetricFilterAutoConfiguration.class);
		assertThat(context.getBeansOfType(Filter.class).size(), equalTo(0));
		context.close();
	}

	@Test
	public void controllerMethodThatThrowsUnhandledException() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		Filter filter = context.getBean(Filter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).build();
		try {
			mvc.perform(get("/unhandledException")).andExpect(
					status().isInternalServerError());
		}
		catch (NestedServletException ex) {
			// Expected
		}
		verify(context.getBean(CounterService.class)).increment(
				"status.500.unhandledException");
		verify(context.getBean(GaugeService.class)).submit(
				eq("response.unhandledException"), anyDouble());
		context.close();
	}

	@Test
	public void gaugeServiceThatThrows() throws Exception {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(
				Config.class, MetricFilterAutoConfiguration.class);
		GaugeService gaugeService = context.getBean(GaugeService.class);
		willThrow(new IllegalStateException()).given(gaugeService).submit(anyString(),
				anyDouble());
		Filter filter = context.getBean(Filter.class);
		MockMvc mvc = MockMvcBuilders.standaloneSetup(new MetricFilterTestController())
				.addFilter(filter).build();
		mvc.perform(get("/templateVarTest/foo")).andExpect(status().isOk());
		verify(context.getBean(CounterService.class)).increment(
				"status.200.templateVarTest.someVariable");
		verify(context.getBean(GaugeService.class)).submit(
				eq("response.templateVarTest.someVariable"), anyDouble());
		context.close();
	}

	@Configuration
	public static class Config {

		@Bean
		public CounterService counterService() {
			return mock(CounterService.class);
		}

		@Bean
		public GaugeService gaugeService() {
			return mock(GaugeService.class);
		}

	}

	@RestController
	class MetricFilterTestController {

		@RequestMapping("templateVarTest/{someVariable}")
		public String testTemplateVariableResolution(@PathVariable String someVariable) {
			return someVariable;
		}

		@RequestMapping("knownPath/{someVariable}")
		@ResponseStatus(HttpStatus.NOT_FOUND)
		@ResponseBody
		public String testKnownPathWith404Response(@PathVariable String someVariable) {
			return someVariable;
		}

		@ResponseBody
		@RequestMapping("unhandledException")
		public String testException() {
			throw new RuntimeException();
		}
	}

	@Component
	@Order(0)
	public static class RedirectFilter extends OncePerRequestFilter {

		@Override
		protected void doFilterInternal(HttpServletRequest request,
				HttpServletResponse response, FilterChain chain) throws ServletException,
				IOException {
			// send redirect before filter chain is executed, like Spring Security sending
			// us back to a login page
			response.sendRedirect("http://example.com");
		}

	}

}
