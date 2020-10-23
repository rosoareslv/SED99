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

package org.springframework.http;

import java.nio.ByteBuffer;

import org.reactivestreams.Publisher;

/**
 * A "reactive" HTTP output message that accepts output as a {@link Publisher}.
 *
 * <p>Typically implemented by an HTTP request on the client-side or a response
 * on the server-side.
 *
 * @author Arjen Poutsma
 */
public interface ReactiveHttpOutputMessage extends HttpMessage {

	/**
	 * Set the body of the message to the given {@link Publisher} which will be
	 * used to write to the underlying HTTP layer.
	 *
	 * @param body the body content publisher
	 * @return a publisher that indicates completion or error.
	 */
	Publisher<Void> setBody(Publisher<ByteBuffer> body);

}
