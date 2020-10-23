/*
 * Copyright 2010-2014 the original author or authors.
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

package org.springframework.boot.actuate.system;

import java.io.File;

import org.springframework.boot.context.event.ApplicationStartedEvent;

/**
 * An {@link org.springframework.context.ApplicationListener} that saves application PID
 * into file. This application listener will be triggered exactly once per JVM, and the
 * file name can be overridden at runtime with a System property or environment variable
 * named "PIDFILE" (or "pidfile").
 *
 * @author Jakub Kubrynski
 * @author Dave Syer
 * @author Phillip Webb
 * @since 1.0.2
 * @deprecated since 1.2.0 in favor of {@link ApplicationPidFileWriter}
 */
@Deprecated
public class ApplicationPidListener extends ApplicationPidFileWriter {

	public ApplicationPidListener() {
		super();
		setTriggerEventType(ApplicationStartedEvent.class);
	}

	public ApplicationPidListener(File file) {
		super(file);
		setTriggerEventType(ApplicationStartedEvent.class);
	}

	public ApplicationPidListener(String filename) {
		super(filename);
		setTriggerEventType(ApplicationStartedEvent.class);
	}

}
