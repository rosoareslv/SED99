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

package org.springframework.reactive.codec.encoder;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import org.junit.Test;
import reactor.rx.Streams;

import org.springframework.core.ResolvableType;
import org.springframework.core.codec.support.StringEncoder;
import org.springframework.http.MediaType;

/**
 * @author Sebastien Deleuze
 */
public class StringEncoderTests {

	private final StringEncoder encoder = new StringEncoder();

	@Test
	public void canWrite() {
		assertTrue(encoder.canEncode(ResolvableType.forClass(String.class), MediaType.TEXT_PLAIN));
		assertFalse(encoder.canEncode(ResolvableType.forClass(Integer.class), MediaType.TEXT_PLAIN));
		assertFalse(encoder.canEncode(ResolvableType.forClass(String.class), MediaType.APPLICATION_JSON));
	}

	@Test
	public void write() throws InterruptedException {
		List<String> results = Streams.wrap(encoder.encode(Streams.just("foo"), null, null))
				.map(chunk -> {
					byte[] b = new byte[chunk.remaining()];
					chunk.get(b);
					return new String(b, StandardCharsets.UTF_8);
				}).toList().await();
		assertEquals(1, results.size());
		assertEquals("foo", results.get(0));
	}

}
