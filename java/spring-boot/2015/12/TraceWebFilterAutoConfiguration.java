/*
 * Copyright 2012-2014 the original author or authors.
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

import javax.servlet.Servlet;
import javax.servlet.ServletRegistration;

import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.actuate.trace.TraceProperties;
import org.springframework.boot.actuate.trace.TraceRepository;
import org.springframework.boot.actuate.trace.WebRequestTraceFilter;
import org.springframework.boot.autoconfigure.AutoConfigureAfter;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.web.ErrorAttributes;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.web.servlet.DispatcherServlet;

/**
 * {@link EnableAutoConfiguration Auto-configuration} for {@link WebRequestTraceFilter
 * tracing}.
 *
 * @author Dave Syer
 */
@ConditionalOnClass({ Servlet.class, DispatcherServlet.class, ServletRegistration.class })
@AutoConfigureAfter(TraceRepositoryAutoConfiguration.class)
@EnableConfigurationProperties(TraceProperties.class)
public class TraceWebFilterAutoConfiguration {

	@Autowired
	private TraceRepository traceRepository;

	@Autowired(required = false)
	private ErrorAttributes errorAttributes;

	@Value("${management.dump_requests:false}")
	private boolean dumpRequests;

	@Autowired
	TraceProperties traceProperties = new TraceProperties();

	@Bean
	public WebRequestTraceFilter webRequestLoggingFilter(BeanFactory beanFactory) {
		WebRequestTraceFilter filter = new WebRequestTraceFilter(this.traceRepository,
				this.traceProperties);
		filter.setDumpRequests(this.dumpRequests);

		if (this.errorAttributes != null) {
			filter.setErrorAttributes(this.errorAttributes);
		}
		return filter;
	}

}
