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

import org.reactivestreams.Publisher;

import org.springframework.http.server.reactive.ServerHttpRequest;

/**
 * Interface to be implemented by objects that define a mapping between
 * requests and handler objects.
 *
 * @author Rossen Stoyanchev
 * @author Sebastien Deleuze
 */
public interface HandlerMapping {

	/**
	 * Return a handler for this request.
	 * @param request current HTTP request
	 * @return A {@link Publisher} object that produces a single handler element
	 */
	Publisher<Object> getHandler(ServerHttpRequest request);

}
