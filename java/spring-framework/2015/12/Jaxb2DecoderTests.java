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

package org.springframework.reactive.codec.decoder;

import java.nio.ByteBuffer;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import org.junit.Test;
import reactor.io.buffer.Buffer;
import reactor.rx.Stream;
import reactor.rx.Streams;

import org.springframework.core.ResolvableType;
import org.springframework.core.codec.support.Jaxb2Decoder;
import org.springframework.http.MediaType;
import org.springframework.reactive.codec.Pojo;

/**
 * @author Sebastien Deleuze
 */
public class Jaxb2DecoderTests {

	private final Jaxb2Decoder decoder = new Jaxb2Decoder();

	@Test
	public void canDecode() {
		assertTrue(decoder.canDecode(null, MediaType.APPLICATION_XML));
		assertTrue(decoder.canDecode(null, MediaType.TEXT_XML));
		assertFalse(decoder.canDecode(null, MediaType.APPLICATION_JSON));
	}

	@Test
	public void decode() throws InterruptedException {
		Stream<ByteBuffer> source = Streams.just(Buffer.wrap("<?xml version=\"1.0\" encoding=\"UTF-8\"?><pojo><bar>barbar</bar><foo>foofoo</foo></pojo>").byteBuffer());
		List<Object> results = Streams.wrap(decoder.decode(source, ResolvableType.forClass(Pojo.class), null))
				.toList().await();
		assertEquals(1, results.size());
		assertEquals("foofoo", ((Pojo) results.get(0)).getFoo());
	}

}
