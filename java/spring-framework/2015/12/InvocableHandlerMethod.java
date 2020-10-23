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

package org.springframework.web.reactive.method;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import java.util.stream.Stream;

import org.reactivestreams.Publisher;
import reactor.Publishers;
import reactor.fn.tuple.Tuple;
import reactor.rx.Streams;

import org.springframework.core.DefaultParameterNameDiscoverer;
import org.springframework.core.GenericTypeResolver;
import org.springframework.core.MethodParameter;
import org.springframework.core.ParameterNameDiscoverer;
import org.springframework.core.ResolvableType;
import org.springframework.http.server.reactive.ServerHttpRequest;
import org.springframework.util.ObjectUtils;
import org.springframework.util.ReflectionUtils;
import org.springframework.web.method.HandlerMethod;
import org.springframework.web.reactive.HandlerResult;


/**
 * @author Rossen Stoyanchev
 */
public class InvocableHandlerMethod extends HandlerMethod {

	public static final Publisher<Object[]> NO_ARGS = Publishers.just(new Object[0]);

	private final static Object NO_VALUE = new Object();


	private List<HandlerMethodArgumentResolver> resolvers = new ArrayList<>();

	private ParameterNameDiscoverer parameterNameDiscoverer = new DefaultParameterNameDiscoverer();


	public InvocableHandlerMethod(HandlerMethod handlerMethod) {
		super(handlerMethod);
	}

	public InvocableHandlerMethod(Object bean, Method method) {
		super(bean, method);
	}


	public void setHandlerMethodArgumentResolvers(List<HandlerMethodArgumentResolver> resolvers) {
		this.resolvers.clear();
		this.resolvers.addAll(resolvers);
	}

	@Override
	protected Method getBridgedMethod() {
		return super.getBridgedMethod();
	}


	/**
	 * Invoke the method and return a Publisher for the return value.
	 * @param request the current request
	 * @param providedArgs optional list of argument values to check by type
	 * (via {@code instanceof}) for resolving method arguments.
	 * @return Publisher that produces a single HandlerResult or an error signal;
	 * never throws an exception.
	 */
	public Publisher<HandlerResult> invokeForRequest(ServerHttpRequest request, Object... providedArgs) {

		Publisher<Object[]> argsPublisher = NO_ARGS;
		try {
			if (!ObjectUtils.isEmpty(getMethodParameters())) {
				List<Publisher<Object>> publishers = resolveArguments(request, providedArgs);
				argsPublisher = Publishers.zip(publishers, this::initArgs);
				argsPublisher = first(argsPublisher);
			}
		}
		catch (Throwable ex) {
			return Publishers.error(ex);
		}

		return Publishers.concatMap(argsPublisher, args -> {
			try {
				Object value = doInvoke(args);
				ResolvableType type =  ResolvableType.forMethodParameter(getReturnType());
				HandlerResult handlerResult = new HandlerResult(this, value, type);
				return Publishers.just(handlerResult);
			}
			catch (InvocationTargetException ex) {
				return Publishers.error(ex.getTargetException());
			}
			catch (Throwable ex) {
				String s = getInvocationErrorMessage(args);
				return Publishers.error(new IllegalStateException(s));
			}
		});
	}

	private List<Publisher<Object>> resolveArguments(ServerHttpRequest request, Object... providedArgs) {
		return Stream.of(getMethodParameters())
				.map(parameter -> {
					parameter.initParameterNameDiscovery(this.parameterNameDiscoverer);
					GenericTypeResolver.resolveParameterType(parameter, getBean().getClass());
					if (!ObjectUtils.isEmpty(providedArgs)) {
						for (Object providedArg : providedArgs) {
							if (parameter.getParameterType().isInstance(providedArg)) {
								return Publishers.just(providedArg);
							}
						}
					}
					HandlerMethodArgumentResolver resolver = this.resolvers.stream()
							.filter(r -> r.supportsParameter(parameter))
							.findFirst()
							.orElseThrow(() -> getArgError("No resolver for ", parameter, null));
					try {
						Publisher<Object> publisher = resolver.resolveArgument(parameter, request);
						publisher = mapError(publisher, ex -> getArgError("Error resolving ", parameter, ex));
						return Streams.wrap(publisher).defaultIfEmpty(NO_VALUE);
					}
					catch (Exception ex) {
						throw getArgError("Error resolving ", parameter, ex);
					}
				})
				.collect(Collectors.toList());
	}

	private IllegalStateException getArgError(String message, MethodParameter param, Throwable cause) {
		return new IllegalStateException(message +
				"argument [" + param.getParameterIndex() + "] " +
				"of type [" + param.getParameterType().getName() + "] " +
				"on method [" + getBridgedMethod().toGenericString() + "]", cause);
	}

	private Object doInvoke(Object[] args) throws Exception {
		if (logger.isTraceEnabled()) {
			String target = getBeanType().getSimpleName() + "." + getMethod().getName();
			logger.trace("Invoking [" + target + "] method with arguments " + Arrays.toString(args));
		}
		ReflectionUtils.makeAccessible(getBridgedMethod());
		Object returnValue = getBridgedMethod().invoke(getBean(), args);
		if (logger.isTraceEnabled()) {
			String target = getBeanType().getSimpleName() + "." + getMethod().getName();
			logger.trace("Method [" + target + "] returned [" + returnValue + "]");
		}
		return returnValue;
	}

	private String getInvocationErrorMessage(Object[] args) {
		String argumentDetails = IntStream.range(0, args.length)
				.mapToObj(i -> (args[i] != null ?
						"[" + i + "][type=" + args[i].getClass().getName() + "][value=" + args[i] + "]" :
						"[" + i + "][null]"))
				.collect(Collectors.joining(",", " ", " "));
		return "Failed to invoke controller with resolved arguments:" + argumentDetails +
				"on method [" + getBridgedMethod().toGenericString() + "]";
	}

	private Object[] initArgs(Tuple tuple) {
		return Stream.of(tuple.toArray()).map(o -> o != NO_VALUE ? o : null).toArray();
	}


	private static <E> Publisher<E> first(Publisher<E> source) {
		return Publishers.lift(source, (e, subscriber) -> {
			subscriber.onNext(e);
			subscriber.onComplete();
		});
	}

	private static <E> Publisher<E> mapError(Publisher<E> source, Function<Throwable, Throwable> function) {
		return Publishers.lift(source, null,
				(throwable, subscriber) -> subscriber.onError(function.apply(throwable)), null);
	}

}
