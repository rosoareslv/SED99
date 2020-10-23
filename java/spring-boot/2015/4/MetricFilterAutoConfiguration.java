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
import javax.servlet.Servlet;
import javax.servlet.ServletException;
import javax.servlet.ServletRegistration;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.actuate.metrics.CounterService;
import org.springframework.boot.actuate.metrics.GaugeService;
import org.springframework.boot.autoconfigure.AutoConfigureAfter;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.http.HttpStatus;
import org.springframework.util.StopWatch;
import org.springframework.web.filter.OncePerRequestFilter;
import org.springframework.web.servlet.HandlerMapping;
import org.springframework.web.util.UrlPathHelper;

/**
 * {@link EnableAutoConfiguration Auto-configuration} that records Servlet interactions
 * with a {@link CounterService} and {@link GaugeService}.
 *
 * @author Dave Syer
 * @author Phillip Webb
 * @author Andy Wilkinson
 */
@Configuration
@ConditionalOnBean({ CounterService.class, GaugeService.class })
@ConditionalOnClass({ Servlet.class, ServletRegistration.class,
		OncePerRequestFilter.class, HandlerMapping.class })
@AutoConfigureAfter(MetricRepositoryAutoConfiguration.class)
public class MetricFilterAutoConfiguration {

	private static final int UNDEFINED_HTTP_STATUS = 999;

	private static final String UNKNOWN_PATH_SUFFIX = "/unmapped";

	@Autowired
	private CounterService counterService;

	@Autowired
	private GaugeService gaugeService;

	@Bean
	public Filter metricFilter() {
		return new MetricsFilter();
	}

	/**
	 * Filter that counts requests and measures processing times.
	 */
	@Order(Ordered.HIGHEST_PRECEDENCE)
	private final class MetricsFilter extends OncePerRequestFilter {

		@Override
		protected void doFilterInternal(HttpServletRequest request,
				HttpServletResponse response, FilterChain chain) throws ServletException,
				IOException {
			UrlPathHelper helper = new UrlPathHelper();
			String suffix = helper.getPathWithinApplication(request);
			StopWatch stopWatch = new StopWatch();
			stopWatch.start();
			int status = HttpStatus.INTERNAL_SERVER_ERROR.value();
			try {
				chain.doFilter(request, response);
				status = getStatus(response);
			}
			finally {
				stopWatch.stop();
				Object bestMatchingPattern = request
						.getAttribute(HandlerMapping.BEST_MATCHING_PATTERN_ATTRIBUTE);
				if (bestMatchingPattern != null) {
					suffix = fixSpecialCharacters(bestMatchingPattern.toString());
				}
				else if (is4xxClientError(status)) {
					suffix = UNKNOWN_PATH_SUFFIX;
				}
				String gaugeKey = getKey("response" + suffix);
				MetricFilterAutoConfiguration.this.gaugeService.submit(gaugeKey,
						stopWatch.getTotalTimeMillis());
				String counterKey = getKey("status." + status + suffix);
				MetricFilterAutoConfiguration.this.counterService.increment(counterKey);
			}
		}

		private String fixSpecialCharacters(String value) {
			String result = value.replaceAll("[{}]", "-");
			result = result.replace("**", "-star-star-");
			result = result.replace("*", "-star-");
			result = result.replace("/-", "/");
			result = result.replace("-/", "/");
			if (result.endsWith("-")) {
				result = result.substring(0, result.length() - 1);
			}
			if (result.startsWith("-")) {
				result = result.substring(1);
			}
			return result;
		}

		private int getStatus(HttpServletResponse response) {
			try {
				return response.getStatus();
			}
			catch (Exception ex) {
				return UNDEFINED_HTTP_STATUS;
			}
		}

		private boolean is4xxClientError(int status) {
			HttpStatus httpStatus = HttpStatus.OK;
			try {
				httpStatus = HttpStatus.valueOf(status);
			}
			catch (Exception ex) {
				// not convertible
			}
			return httpStatus.is4xxClientError();
		}

		private String getKey(String string) {
			// graphite compatible metric names
			String value = string.replace("/", ".");
			value = value.replace("..", ".");
			if (value.endsWith(".")) {
				value = value + "root";
			}
			if (value.startsWith("_")) {
				value = value.substring(1);
			}
			return value;
		}
	}

}
