/*
 * Copyright 2012-2015 the original author or authors.
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

package org.springframework.boot.gradle;

import java.io.IOException;

import org.gradle.tooling.ProjectConnection;
import org.junit.BeforeClass;
import org.junit.Test;

/**
 * Tests for configuring a project's main class
 *
 * @author Dave Syer
 */
public class MainClassTests {

	private static ProjectConnection project;

	private static final String BOOT_VERSION = Versions.getBootVersion();

	@BeforeClass
	public static void createProject() throws IOException {
		project = new ProjectCreator().createProject("main-in-boot-run");
	}

	@Test
	public void mainFromBootRun() {
		project.newBuild().forTasks("build")
				.withArguments("-PbootVersion=" + BOOT_VERSION, "--info").run();
	}

}
