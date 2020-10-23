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

import org.springframework.util.Assert;

/**
 * An {@link OperationInvoker} that caches the response of an operation with a
 * configurable time to live.
 *
 * @author Stephane Nicoll
 * @since 2.0.0
 */
public class CachingOperationInvoker implements OperationInvoker {

	private final OperationInvoker target;

	private final long timeToLive;

	private volatile CachedResponse cachedResponse;

	/**
	 * Create a new instance with the target {@link OperationInvoker} to use to compute
	 * the response and the time to live for the cache.
	 * @param target the {@link OperationInvoker} this instance wraps
	 * @param timeToLive the maximum time in milliseconds that a response can be cached
	 */
	public CachingOperationInvoker(OperationInvoker target, long timeToLive) {
		Assert.state(timeToLive > 0, "TimeToLive must be strictly positive");
		this.target = target;
		this.timeToLive = timeToLive;
	}

	/**
	 * Return the maximum time in milliseconds that a response can be cached.
	 * @return the time to live of a response
	 */
	public long getTimeToLive() {
		return this.timeToLive;
	}

	@Override
	public Object invoke(Map<String, Object> arguments) {
		long accessTime = System.currentTimeMillis();
		CachedResponse cached = this.cachedResponse;
		if (cached == null || cached.isStale(accessTime, this.timeToLive)) {
			Object response = this.target.invoke(arguments);
			this.cachedResponse = new CachedResponse(response, accessTime);
			return response;
		}
		return cached.getResponse();
	}

	/**
	 * A cached response that encapsulates the response itself and the time at which it
	 * was created.
	 */
	static class CachedResponse {

		private final Object response;

		private final long creationTime;

		CachedResponse(Object response, long creationTime) {
			this.response = response;
			this.creationTime = creationTime;
		}

		public boolean isStale(long accessTime, long timeToLive) {
			return (accessTime - this.creationTime) >= timeToLive;
		}

		public Object getResponse() {
			return this.response;
		}

	}

}
