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

package org.springframework.boot.actuate.metrics.buffer;

import java.io.FileNotFoundException;
import java.io.PrintWriter;
import java.util.Collection;
import java.util.HashSet;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.LongAdder;

import com.codahale.metrics.MetricRegistry;
import org.junit.BeforeClass;
import org.junit.experimental.theories.DataPoints;
import org.junit.experimental.theories.Theories;
import org.junit.experimental.theories.Theory;
import org.junit.runner.RunWith;

import org.springframework.boot.actuate.metrics.CounterService;
import org.springframework.boot.actuate.metrics.dropwizard.DropwizardMetricServices;
import org.springframework.boot.actuate.metrics.reader.MetricReader;
import org.springframework.boot.actuate.metrics.reader.MetricRegistryMetricReader;
import org.springframework.util.StopWatch;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Speeds tests for {@link DropwizardMetricServices DropwizardMetricServices'}
 * {@link CounterService}.
 *
 * @author Dave Syer
 */
@RunWith(Theories.class)
public class DropwizardCounterServiceSpeedTests {

	@DataPoints
	public static String[] values = new String[10];

	public static String[] names = new String[] { "foo", "bar", "spam", "bucket" };

	public static String[] sample = new String[1000];

	private MetricRegistry registry = new MetricRegistry();

	private CounterService counterService = new DropwizardMetricServices(this.registry);

	private MetricReader reader = new MetricRegistryMetricReader(this.registry);

	private static int threadCount = 2;

	private static final int number = Boolean.getBoolean("performance.test") ? 10000000
			: 1000000;

	private static int count;

	private static StopWatch watch = new StopWatch("count");

	private static PrintWriter err;

	@BeforeClass
	public static void prime() throws FileNotFoundException {
		err = new NullPrintWriter();
		Random random = new Random();
		for (int i = 0; i < 1000; i++) {
			sample[i] = names[random.nextInt(names.length)];
		}
	}

	@Theory
	public void counters(String input) throws Exception {
		watch.start("counters" + count++);
		ExecutorService pool = Executors.newFixedThreadPool(threadCount);
		Runnable task = () -> {
			for (int i = 0; i < number; i++) {
				String name = sample[i % sample.length];
				DropwizardCounterServiceSpeedTests.this.counterService.increment(name);
			}
		};
		Collection<Future<?>> futures = new HashSet<>();
		for (int i = 0; i < threadCount; i++) {
			futures.add(pool.submit(task));
		}
		for (Future<?> future : futures) {
			future.get();
		}
		watch.stop();
		double rate = number / watch.getLastTaskTimeMillis() * 1000;
		System.err.println("Counters rate(" + count + ")=" + rate + ", " + watch);
		watch.start("read" + count);
		this.reader.findAll().forEach(err::println);
		LongAdder total = new LongAdder();
		this.reader.findAll().forEach((value) -> total.add(value.getValue().intValue()));
		watch.stop();
		System.err.println("Read(" + count + ")=" + watch.getLastTaskTimeMillis() + "ms");
		assertThat(total.longValue()).isEqualTo(number * threadCount);
	}

}
