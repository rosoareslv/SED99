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

package org.springframework.boot.endpoint;

import java.util.Map;

/**
 * An {@code OperationInvoker} is used to invoke an operation on an endpoint.
 *
 * @author Andy Wilkinson
 * @since 2.0.0
 */
@FunctionalInterface
public interface OperationInvoker {

	/**
	 * Invoke the underlying operation using the given {@code arguments}.
	 * @param arguments the arguments to pass to the operation
	 * @return the result of the operation, may be {@code null}
	 */
	Object invoke(Map<String, Object> arguments);

}
