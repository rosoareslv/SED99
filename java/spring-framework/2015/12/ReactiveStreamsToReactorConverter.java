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

package org.springframework.core.convert.support;

import java.util.LinkedHashSet;
import java.util.Set;

import org.reactivestreams.Publisher;
import reactor.rx.Promise;
import reactor.rx.Stream;
import reactor.rx.Streams;

import org.springframework.core.convert.TypeDescriptor;
import org.springframework.core.convert.converter.GenericConverter;

/**
 * @author Stephane Maldini
 * @author Sebastien Deleuze
 */
public final class ReactiveStreamsToReactorConverter implements GenericConverter {

	@Override
	public Set<GenericConverter.ConvertiblePair> getConvertibleTypes() {
		Set<GenericConverter.ConvertiblePair> pairs = new LinkedHashSet<>();
		pairs.add(new GenericConverter.ConvertiblePair(Publisher.class, Stream.class));
		pairs.add(new GenericConverter.ConvertiblePair(Stream.class, Publisher.class));
		pairs.add(new GenericConverter.ConvertiblePair(Publisher.class, Promise.class));
		pairs.add(new GenericConverter.ConvertiblePair(Promise.class, Publisher.class));
		return pairs;
	}

	@Override
	public Object convert(Object source, TypeDescriptor sourceType, TypeDescriptor targetType) {
		if (source == null) {
			return null;
		}
		if (Stream.class.isAssignableFrom(source.getClass())) {
			return source;
		}
		else if (Stream.class.isAssignableFrom(targetType.getResolvableType().getRawClass())) {
			return Streams.wrap((Publisher)source);
		}
		else if (Promise.class.isAssignableFrom(source.getClass())) {
			return source;
		}
		else if (Promise.class.isAssignableFrom(targetType.getResolvableType().getRawClass())) {
			return Streams.wrap((Publisher)source).next();
		}
		return null;
	}

}
