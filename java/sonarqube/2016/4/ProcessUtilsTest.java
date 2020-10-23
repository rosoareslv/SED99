/*
 * SonarQube
 * Copyright (C) 2009-2016 SonarSource SA
 * mailto:contact AT sonarsource DOT com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.process;

import org.junit.Test;
import org.sonar.test.TestUtils;

import static org.assertj.core.api.Assertions.assertThat;
import static org.sonar.process.ProcessUtils.awaitTermination;

public class ProcessUtilsTest {

  @Test
  public void private_constructor() {
    assertThat(TestUtils.hasOnlyPrivateConstructors(ProcessUtils.class)).isTrue();
  }

  @Test
  public void awaitTermination_does_not_fail_on_null_Thread_argument() {
    awaitTermination((Thread) null);
  }

  @Test(timeout = 100L)
  public void awaitTermination_does_not_wait_on_currentThread() {
    awaitTermination(Thread.currentThread());
  }

  @Test(timeout = 3000)
  public void awaitTermination_ignores_interrupted_exception_of_current_thread() throws InterruptedException {
    final EverRunningThread runningThread = new EverRunningThread();
    final Thread safeJoiner = new Thread() {
      @Override
      public void run() {
        awaitTermination(runningThread);
      }
    };
    final Thread simpleJoiner = new Thread() {
      @Override
      public void run() {
        try {
          runningThread.join();
        } catch (InterruptedException e) {
          System.err.println("runningThread interruption detected in SimpleJoiner");
        }
      }
    };
    runningThread.start();
    safeJoiner.start();
    simpleJoiner.start();

    // interrupt safeJoiner _before simpleJoiner to work around some arbitrary sleep delay_ which should not stop watching
    safeJoiner.interrupt();

    // interrupting simpleJoiner which should stop
    simpleJoiner.interrupt();

    while (simpleJoiner.isAlive()) {
      // wait for simpleJoiner to stop
    }

    // safeJoiner must still be alive
    assertThat(safeJoiner.isAlive()).isTrue() ;

    // stop runningThread
    runningThread.stopIt();

    while (runningThread.isAlive()) {
      // wait for runningThread to stop
    }

    // wait for safeJoiner to stop because runningThread has stopped, if it doesn't, the test will fail with a timeout
    safeJoiner.join();
  }

  @Test(timeout = 100L)
  public void awaitTermination_of_vararg_does_not_fail_when_there_is_a_null_or_current_thread() {
    awaitTermination(null, Thread.currentThread(), null);
  }

  private static class EverRunningThread extends Thread {
    private volatile boolean stop = false;

    @Override
    public void run() {
      while (!stop) {
        // infinite loop!
      }
    }

    public void stopIt() {
      this.stop = true;
    }
  }

}
