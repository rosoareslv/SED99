/*
 * Copyright 2002-2015 the original author or authors.
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

package org.springframework.web.reactive;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.Function;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.reactivestreams.Publisher;
import org.reactivestreams.Subscriber;
import reactor.Publishers;
import reactor.fn.BiConsumer;

import org.springframework.beans.BeansException;
import org.springframework.beans.factory.BeanFactoryUtils;
import org.springframework.context.ApplicationContext;
import org.springframework.context.ApplicationContextAware;
import org.springframework.core.annotation.AnnotationAwareOrderComparator;
import org.springframework.http.server.reactive.HttpHandler;
import org.springframework.http.server.reactive.ServerHttpRequest;
import org.springframework.http.server.reactive.ServerHttpResponse;

/**
 * Central dispatcher for HTTP request handlers/controllers. Dispatches to registered
 * handlers for processing a web request, providing convenient mapping facilities.
 *
 * <li>It can use any {@link HandlerMapping} implementation to control the routing of
 * requests to handler objects. HandlerMapping objects can be defined as beans in
 * the application context.
 *
 * <li>It can use any {@link HandlerAdapter}; this allows for using any handler interface.
 * HandlerAdapter objects can be added as beans in the application context.
 *
 * <li>It can use any {@link HandlerResultHandler}; this allows to process the result of
 * the request handling. HandlerResultHandler objects can be added as beans in the
 * application context.
 *
 * @author Rossen Stoyanchev
 * @author Sebastien Deleuze
 */
public class DispatcherHandler implements HttpHandler, ApplicationContextAware {

	private static final Log logger = LogFactory.getLog(DispatcherHandler.class);


	private List<HandlerMapping> handlerMappings;

	private List<HandlerAdapter> handlerAdapters;

	private List<HandlerResultHandler> resultHandlers;

	private Function<Throwable, Throwable> errorMapper = new DispatcherHandlerExceptionMapper();


	@Override
	public void setApplicationContext(ApplicationContext applicationContext) throws BeansException {
		initStrategies(applicationContext);
	}

	/**
	 * Configure a function to map error signals from the {@code DispatcherHandler}.
	 * <p>By default this is set to {@link DispatcherHandlerExceptionMapper}.
	 * @param errorMapper the function
	 */
	public void setErrorMapper(Function<Throwable, Throwable> errorMapper) {
		this.errorMapper = errorMapper;
	}

	/**
	 * Return the configured function for mapping exceptions.
	 */
	public Function<Throwable, Throwable> getErrorMapper() {
		return this.errorMapper;
	}

	protected void initStrategies(ApplicationContext context) {

		Map<String, HandlerMapping> mappingBeans = BeanFactoryUtils.beansOfTypeIncludingAncestors(
				context, HandlerMapping.class, true, false);

		this.handlerMappings = new ArrayList<>(mappingBeans.values());
		this.handlerMappings.add(new NotFoundHandlerMapping());
		AnnotationAwareOrderComparator.sort(this.handlerMappings);

		Map<String, HandlerAdapter> adapterBeans = BeanFactoryUtils.beansOfTypeIncludingAncestors(
				context, HandlerAdapter.class, true, false);

		this.handlerAdapters = new ArrayList<>(adapterBeans.values());
		AnnotationAwareOrderComparator.sort(this.handlerAdapters);

		Map<String, HandlerResultHandler> beans = BeanFactoryUtils.beansOfTypeIncludingAncestors(
				context, HandlerResultHandler.class, true, false);

		this.resultHandlers = new ArrayList<>(beans.values());
		AnnotationAwareOrderComparator.sort(this.resultHandlers);
	}


	@Override
	public Publisher<Void> handle(ServerHttpRequest request, ServerHttpResponse response) {
		if (logger.isDebugEnabled()) {
			logger.debug("Processing " + request.getMethod() + " request for [" + request.getURI() + "]");
		}

		Publisher<HandlerMapping> mappings = Publishers.from(this.handlerMappings);
		Publisher<Object> handlerPublisher = Publishers.concatMap(mappings, m -> m.getHandler(request));
		handlerPublisher = first(handlerPublisher);

		Publisher<HandlerResult> resultPublisher = Publishers.concatMap(handlerPublisher, handler -> {
			HandlerAdapter handlerAdapter = getHandlerAdapter(handler);
			return handlerAdapter.handle(request, response, handler);
		});

		Publisher<Void> completionPublisher = Publishers.concatMap(resultPublisher, result -> {
			Publisher<Void> publisher;
			if (result.hasError()) {
				publisher = Publishers.error(result.getError());
			}
			else {
				HandlerResultHandler handler = getResultHandler(result);
				publisher = handler.handleResult(request, response, result);
			}
			if (result.hasExceptionMapper()) {
				return Publishers.onErrorResumeNext(publisher, ex -> {
					return Publishers.concatMap(result.getExceptionMapper().apply(ex),
							errorResult -> {
								HandlerResultHandler handler = getResultHandler(errorResult);
								return handler.handleResult(request, response, errorResult);
							});
				});
			}
			return publisher;
		});

		return mapError(completionPublisher, this.errorMapper);
	}

	protected HandlerAdapter getHandlerAdapter(Object handler) {
		for (HandlerAdapter handlerAdapter : this.handlerAdapters) {
			if (handlerAdapter.supports(handler)) {
				return handlerAdapter;
			}
		}
		throw new IllegalStateException("No HandlerAdapter: " + handler);
	}

	protected HandlerResultHandler getResultHandler(HandlerResult handlerResult) {
		for (HandlerResultHandler resultHandler : resultHandlers) {
			if (resultHandler.supports(handlerResult)) {
				return resultHandler;
			}
		}
		throw new IllegalStateException("No HandlerResultHandler for " + handlerResult.getResult());
	}


	private static <E> Publisher<E> first(Publisher<E> source) {
		return Publishers.lift(source, (e, subscriber) -> {
			subscriber.onNext(e);
			subscriber.onComplete();
		});
	}

	private static <E> Publisher<E> mapError(Publisher<E> source, Function<Throwable, Throwable> function) {
		return Publishers.lift(source, null, new BiConsumer<Throwable, Subscriber<? super E>>() {
			@Override
			public void accept(Throwable throwable, Subscriber<? super E> subscriber) {
				subscriber.onError(function.apply(throwable));
			}
		}, null);
	}

	private static class NotFoundHandlerMapping implements HandlerMapping {

		@SuppressWarnings("ThrowableInstanceNeverThrown")
		private static final Exception HANDLER_NOT_FOUND_EXCEPTION = new HandlerNotFoundException();


		@Override
		public Publisher<Object> getHandler(ServerHttpRequest request) {
			return Publishers.error(HANDLER_NOT_FOUND_EXCEPTION);
		}
	}

}