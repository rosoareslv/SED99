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

package org.springframework.boot.actuate.metrics.web.servlet;

import java.lang.reflect.AnnotatedElement;
import java.util.Arrays;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import io.micrometer.core.annotation.Timed;
import io.micrometer.core.annotation.TimedSet;
import io.micrometer.core.instrument.LongTaskTimer;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Tag;
import io.micrometer.core.instrument.Tags;
import io.micrometer.core.instrument.Timer;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.springframework.core.annotation.AnnotationUtils;
import org.springframework.util.ObjectUtils;
import org.springframework.web.context.request.RequestAttributes;
import org.springframework.web.context.request.RequestContextHolder;
import org.springframework.web.method.HandlerMethod;
import org.springframework.web.servlet.mvc.ParameterizableViewController;
import org.springframework.web.servlet.resource.ResourceHttpRequestHandler;

/**
 * Support class for Spring MVC metrics.
 *
 * @author Jon Schneider
 * @since 2.0.0
 */
public class WebMvcMetrics {

	private static final String TIMING_REQUEST_ATTRIBUTE = "micrometer.requestStartTime";

	private static final String HANDLER_REQUEST_ATTRIBUTE = "micrometer.requestHandler";

	private static final String EXCEPTION_ATTRIBUTE = "micrometer.requestException";

	private static final Log logger = LogFactory.getLog(WebMvcMetrics.class);

	private final Map<HttpServletRequest, Long> longTaskTimerIds = Collections
			.synchronizedMap(new IdentityHashMap<>());

	private final MeterRegistry registry;

	private final WebMvcTagsProvider tagsProvider;

	private final String metricName;

	private final boolean autoTimeRequests;

	private final boolean recordAsPercentiles;

	public WebMvcMetrics(MeterRegistry registry, WebMvcTagsProvider tagsProvider,
			String metricName, boolean autoTimeRequests, boolean recordAsPercentiles) {
		this.registry = registry;
		this.tagsProvider = tagsProvider;
		this.metricName = metricName;
		this.autoTimeRequests = autoTimeRequests;
		this.recordAsPercentiles = recordAsPercentiles;
	}

	public void tagWithException(Throwable exception) {
		RequestAttributes attributes = RequestContextHolder.getRequestAttributes();
		attributes.setAttribute(EXCEPTION_ATTRIBUTE, exception,
				RequestAttributes.SCOPE_REQUEST);
	}

	void preHandle(HttpServletRequest request, Object handler) {
		request.setAttribute(TIMING_REQUEST_ATTRIBUTE, System.nanoTime());
		request.setAttribute(HANDLER_REQUEST_ATTRIBUTE, handler);
		longTaskTimed(handler).forEach((config) -> {
			if (config.getName() == null) {
				logWarning(request, handler);
				return;
			}
			this.longTaskTimerIds.put(request,
					longTaskTimer(config, request, handler).start());
		});
	}

	private void logWarning(HttpServletRequest request, Object handler) {
		if (handler instanceof HandlerMethod) {
			logger.warn("Unable to perform metrics timing on "
					+ ((HandlerMethod) handler).getShortLogMessage()
					+ ": @Timed annotation must have a value used to name the metric");
			return;
		}
		logger.warn("Unable to perform metrics timing for request "
				+ request.getRequestURI()
				+ ": @Timed annotation must have a value used to name the metric");
	}

	void record(HttpServletRequest request, HttpServletResponse response, Throwable ex) {
		Object handler = request.getAttribute(HANDLER_REQUEST_ATTRIBUTE);
		Long startTime = (Long) request.getAttribute(TIMING_REQUEST_ATTRIBUTE);
		long endTime = System.nanoTime();
		completeLongTimerTasks(request, handler);
		Throwable thrown = (ex != null ? ex
				: (Throwable) request.getAttribute(EXCEPTION_ATTRIBUTE));
		recordTimerTasks(request, response, handler, startTime, endTime, thrown);
	}

	private void completeLongTimerTasks(HttpServletRequest request, Object handler) {
		longTaskTimed(handler)
				.forEach((config) -> completeLongTimerTask(request, handler, config));
	}

	private void completeLongTimerTask(HttpServletRequest request, Object handler,
			TimerConfig config) {
		if (config.getName() != null) {
			longTaskTimer(config, request, handler)
					.stop(this.longTaskTimerIds.remove(request));
		}
	}

	private void recordTimerTasks(HttpServletRequest request,
			HttpServletResponse response, Object handler, Long startTime, long endTime,
			Throwable thrown) {
		// record Timer values
		timed(handler).forEach((config) -> {
			Timer.Builder builder = getTimerBuilder(request, handler, response, thrown,
					config);
			long amount = endTime - startTime;
			builder.register(this.registry).record(amount, TimeUnit.NANOSECONDS);
		});
	}

	private Timer.Builder getTimerBuilder(HttpServletRequest request, Object handler,
			HttpServletResponse response, Throwable thrown, TimerConfig config) {
		Timer.Builder builder = Timer.builder(config.getName())
				.tags(this.tagsProvider.httpRequestTags(request, handler, response,
						thrown))
				.tags(config.getExtraTags()).description("Timer of servlet request")
				.publishPercentileHistogram(config.isHistogram());
		if (config.getPercentiles().length > 0) {
			builder = builder.publishPercentiles(config.getPercentiles());
		}
		return builder;
	}

	private LongTaskTimer longTaskTimer(TimerConfig config, HttpServletRequest request,
			Object handler) {
		return LongTaskTimer.builder(config.getName())
				.tags(this.tagsProvider.httpLongRequestTags(request, handler))
				.tags(config.getExtraTags()).description("Timer of long servlet request")
				.register(this.registry);
	}

	private Set<TimerConfig> longTaskTimed(Object handler) {
		if (handler instanceof HandlerMethod) {
			return longTaskTimed((HandlerMethod) handler);
		}
		return Collections.emptySet();
	}

	private Set<TimerConfig> longTaskTimed(HandlerMethod handler) {
		Set<TimerConfig> timed = getLongTaskAnnotationConfig(handler.getMethod());
		if (timed.isEmpty()) {
			return getLongTaskAnnotationConfig(handler.getBeanType());
		}
		return timed;
	}

	private Set<TimerConfig> timed(Object handler) {
		if (handler instanceof HandlerMethod) {
			return timed((HandlerMethod) handler);
		}
		if ((handler == null || handler instanceof ResourceHttpRequestHandler
				|| handler instanceof ParameterizableViewController)
				&& this.autoTimeRequests) {
			return Collections.singleton(
					new TimerConfig(getServerRequestName(), this.recordAsPercentiles));
		}
		return Collections.emptySet();
	}

	private Set<TimerConfig> timed(HandlerMethod handler) {
		Set<TimerConfig> config = getNonLongTaskAnnotationConfig(handler.getMethod());
		if (config.isEmpty()) {
			config = getNonLongTaskAnnotationConfig(handler.getBeanType());
			if (config.isEmpty() && this.autoTimeRequests) {
				return Collections.singleton(new TimerConfig(getServerRequestName(),
						this.recordAsPercentiles));
			}
		}
		return config;
	}

	private Set<TimerConfig> getNonLongTaskAnnotationConfig(AnnotatedElement element) {
		return findTimedAnnotations(element).filter((t) -> !t.longTask())
				.map(this::fromAnnotation).collect(Collectors.toSet());
	}

	private Set<TimerConfig> getLongTaskAnnotationConfig(AnnotatedElement element) {
		return findTimedAnnotations(element).filter(Timed::longTask)
				.map(this::fromAnnotation).collect(Collectors.toSet());
	}

	private Stream<Timed> findTimedAnnotations(AnnotatedElement element) {
		Timed timed = AnnotationUtils.findAnnotation(element, Timed.class);
		if (timed != null) {
			return Stream.of(timed);
		}
		TimedSet ts = AnnotationUtils.findAnnotation(element, TimedSet.class);
		if (ts != null) {
			return Arrays.stream(ts.value());
		}
		return Stream.empty();
	}

	private TimerConfig fromAnnotation(Timed timed) {
		return new TimerConfig(timed, this::getServerRequestName);
	}

	private String getServerRequestName() {
		return this.metricName;
	}

	private static class TimerConfig {

		private final String name;

		private final Iterable<Tag> extraTags;

		private final double[] percentiles;

		private final boolean histogram;

		TimerConfig(String name, boolean histogram) {
			this.name = name;
			this.extraTags = Collections.emptyList();
			this.percentiles = new double[0];
			this.histogram = histogram;
		}

		TimerConfig(Timed timed, Supplier<String> name) {
			this.name = buildName(timed, name);
			this.extraTags = Tags.zip(timed.extraTags());
			this.percentiles = timed.percentiles();
			this.histogram = timed.histogram();
		}

		private String buildName(Timed timed, Supplier<String> name) {
			if (timed.longTask() && timed.value().isEmpty()) {
				// the user MUST name long task timers, we don't lump them in with regular
				// timers with the same name
				return null;
			}
			return (timed.value().isEmpty() ? name.get() : timed.value());
		}

		public String getName() {
			return this.name;
		}

		Iterable<Tag> getExtraTags() {
			return this.extraTags;
		}

		double[] getPercentiles() {
			return this.percentiles;
		}

		boolean isHistogram() {
			return this.histogram;
		}

		@Override
		public boolean equals(Object o) {
			if (this == o) {
				return true;
			}
			if (o == null || getClass() != o.getClass()) {
				return false;
			}
			TimerConfig other = (TimerConfig) o;
			return ObjectUtils.nullSafeEquals(this.name, other.name);
		}

		@Override
		public int hashCode() {
			return ObjectUtils.nullSafeHashCode(this.name);
		}

	}

}
