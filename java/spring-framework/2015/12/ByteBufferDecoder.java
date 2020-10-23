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

package org.springframework.core.codec.support;

import java.nio.ByteBuffer;

import org.reactivestreams.Publisher;

import org.springframework.core.ResolvableType;
import org.springframework.util.MimeType;
import org.springframework.util.MimeTypeUtils;

/**
 * @author Sebastien Deleuze
 */
public class ByteBufferDecoder extends AbstractDecoder<ByteBuffer> {


	public ByteBufferDecoder() {
		super(MimeTypeUtils.ALL);
	}


	@Override
	public boolean canDecode(ResolvableType type, MimeType mimeType, Object... hints) {
		Class<?> clazz = type.getRawClass();
		return (super.canDecode(type, mimeType, hints) && ByteBuffer.class.isAssignableFrom(clazz));
	}

	@Override
	public Publisher<ByteBuffer> decode(Publisher<ByteBuffer> inputStream, ResolvableType type,
			MimeType mimeType, Object... hints) {

		return inputStream;
	}

}