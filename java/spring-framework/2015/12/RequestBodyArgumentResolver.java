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

package org.springframework.web.reactive.method.annotation;

import java.nio.ByteBuffer;
import java.util.List;

import org.reactivestreams.Publisher;
import reactor.Publishers;

import org.springframework.core.MethodParameter;
import org.springframework.core.ResolvableType;
import org.springframework.core.convert.ConversionService;
import org.springframework.http.MediaType;
import org.springframework.http.server.reactive.ServerHttpRequest;
import org.springframework.core.codec.Decoder;
import org.springframework.web.reactive.method.HandlerMethodArgumentResolver;
import org.springframework.util.Assert;
import org.springframework.web.bind.annotation.RequestBody;

/**
 * @author Sebastien Deleuze
 * @author Stephane Maldini
 */
public class RequestBodyArgumentResolver implements HandlerMethodArgumentResolver {

	private final List<Decoder<?>> decoders;

	private final ConversionService conversionService;


	public RequestBodyArgumentResolver(List<Decoder<?>> decoders, ConversionService service) {
		Assert.notEmpty(decoders, "At least one decoder is required.");
		Assert.notNull(service, "'conversionService' is required.");
		this.decoders = decoders;
		this.conversionService = service;
	}


	@Override
	public boolean supportsParameter(MethodParameter parameter) {
		return parameter.hasParameterAnnotation(RequestBody.class);
	}

	@Override
	public Publisher<Object> resolveArgument(MethodParameter parameter, ServerHttpRequest request) {
		MediaType mediaType = request.getHeaders().getContentType();
		if (mediaType == null) {
			mediaType = MediaType.APPLICATION_OCTET_STREAM;
		}
		ResolvableType type = ResolvableType.forMethodParameter(parameter);
		Publisher<ByteBuffer> body = request.getBody();
		Publisher<?> elementStream = body;
		ResolvableType elementType = type.hasGenerics() ? type.getGeneric(0) : type;

		Decoder<?> decoder = resolveDecoder(elementType, mediaType);
		if (decoder != null) {
			elementStream = decoder.decode(body, elementType, mediaType);
		}

		if (this.conversionService.canConvert(Publisher.class, type.getRawClass())) {
			return Publishers.just(this.conversionService.convert(elementStream, type.getRawClass()));
		}

		return Publishers.map(elementStream, element -> element);
	}

	private Decoder<?> resolveDecoder(ResolvableType type, MediaType mediaType, Object... hints) {
		for (Decoder<?> decoder : this.decoders) {
			if (decoder.canDecode(type, mediaType, hints)) {
				return decoder;
			}
		}
		return null;
	}

}
