/*
 * SonarQube
 * Copyright (C) 2009-2017 SonarSource SA
 * mailto:info AT sonarsource DOT com
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
package org.sonar.ce.taskprocessor;

import java.util.Optional;
import java.util.function.Supplier;
import javax.annotation.Nullable;
import org.sonar.api.utils.MessageException;
import org.sonar.api.utils.log.Logger;
import org.sonar.api.utils.log.Loggers;
import org.sonar.ce.log.CeLogging;
import org.sonar.ce.queue.CeTask;
import org.sonar.ce.queue.CeTaskResult;
import org.sonar.ce.queue.InternalCeQueue;
import org.sonar.core.util.logs.Profiler;
import org.sonar.db.ce.CeActivityDto;

import static com.google.common.base.Preconditions.checkArgument;
import static java.lang.String.format;
import static org.sonar.ce.taskprocessor.CeWorker.Result.DISABLED;
import static org.sonar.ce.taskprocessor.CeWorker.Result.NO_TASK;
import static org.sonar.ce.taskprocessor.CeWorker.Result.TASK_PROCESSED;

public class CeWorkerImpl implements CeWorker {

  private static final Logger LOG = Loggers.get(CeWorkerImpl.class);

  private final int ordinal;
  private final String uuid;
  private final InternalCeQueue queue;
  private final CeLogging ceLogging;
  private final CeTaskProcessorRepository taskProcessorRepository;
  private final EnabledCeWorkerController enabledCeWorkerController;

  public CeWorkerImpl(int ordinal, String uuid,
    InternalCeQueue queue, CeLogging ceLogging, CeTaskProcessorRepository taskProcessorRepository,
    EnabledCeWorkerController enabledCeWorkerController) {
    this.ordinal = checkOrdinal(ordinal);
    this.uuid = uuid;
    this.queue = queue;
    this.ceLogging = ceLogging;
    this.taskProcessorRepository = taskProcessorRepository;
    this.enabledCeWorkerController = enabledCeWorkerController;
  }

  private static int checkOrdinal(int ordinal) {
    checkArgument(ordinal >= 0, "Ordinal must be >= 0");
    return ordinal;
  }

  @Override
  public Result call() throws Exception {
    return withCustomizedThreadName(this::findAndProcessTask);
  }

  private Result findAndProcessTask() {
    if (!enabledCeWorkerController.isEnabled(this)) {
      return DISABLED;
    }
    Optional<CeTask> ceTask = tryAndFindTaskToExecute();
    if (!ceTask.isPresent()) {
      return NO_TASK;
    }

    try {
      executeTask(ceTask.get());
    } catch (Exception e) {
      LOG.error(format("An error occurred while executing task with uuid '%s'", ceTask.get().getUuid()), e);
    }
    return TASK_PROCESSED;
  }

  private <T> T withCustomizedThreadName(Supplier<T> supplier) {
    Thread currentThread = Thread.currentThread();
    String oldName = currentThread.getName();
    try {
      currentThread.setName(String.format("Worker %s (UUID=%s) on %s", getOrdinal(), getUUID(), oldName));
      return supplier.get();
    } finally {
      currentThread.setName(oldName);
    }
  }

  private Optional<CeTask> tryAndFindTaskToExecute() {
    try {
      return queue.peek(uuid);
    } catch (Exception e) {
      LOG.error("Failed to pop the queue of analysis reports", e);
    }
    return Optional.empty();
  }

  @Override
  public int getOrdinal() {
    return ordinal;
  }

  @Override
  public String getUUID() {
    return uuid;
  }

  private void executeTask(CeTask task) {
    ceLogging.initForTask(task);
    Profiler ceProfiler = startActivityProfiler(task);

    CeActivityDto.Status status = CeActivityDto.Status.FAILED;
    CeTaskResult taskResult = null;
    Throwable error = null;
    try {
      // TODO delegate the message to the related task processor, according to task type
      Optional<CeTaskProcessor> taskProcessor = taskProcessorRepository.getForCeTask(task);
      if (taskProcessor.isPresent()) {
        taskResult = taskProcessor.get().process(task);
        status = CeActivityDto.Status.SUCCESS;
      } else {
        LOG.error("No CeTaskProcessor is defined for task of type {}. Plugin configuration may have changed", task.getType());
        status = CeActivityDto.Status.FAILED;
      }
    } catch (MessageException e) {
      error = e;
    } catch (Throwable e) {
      LOG.error(format("Failed to execute task %s", task.getUuid()), e);
      error = e;
    } finally {
      finalizeTask(task, ceProfiler, status, taskResult, error);
    }
  }

  private void finalizeTask(CeTask task, Profiler ceProfiler, CeActivityDto.Status status,
    @Nullable CeTaskResult taskResult, @Nullable Throwable error) {
    try {
      queue.remove(task, status, taskResult, error);
    } catch (Exception e) {
      String errorMessage = format("Failed to finalize task with uuid '%s' and persist its state to db", task.getUuid());
      if (error instanceof MessageException) {
        LOG.error(format("%s. Task failed with MessageException \"%s\"", errorMessage, error.getMessage()), e);
      } else {
        LOG.error(errorMessage, e);
      }
    } finally {
      stopActivityProfiler(ceProfiler, task, status);
      ceLogging.clearForTask();
    }
  }

  private static Profiler startActivityProfiler(CeTask task) {
    Profiler profiler = Profiler.create(LOG);
    addContext(profiler, task);
    return profiler.startInfo("Execute task");
  }

  private static void stopActivityProfiler(Profiler profiler, CeTask task, CeActivityDto.Status status) {
    addContext(profiler, task);
    if (status == CeActivityDto.Status.FAILED) {
      profiler.stopError("Executed task");
    } else {
      profiler.stopInfo("Executed task");
    }
  }

  private static void addContext(Profiler profiler, CeTask task) {
    profiler
      .logTimeLast(true)
      .addContext("project", task.getComponentKey())
      .addContext("type", task.getType())
      .addContext("id", task.getUuid());
    String submitterLogin = task.getSubmitterLogin();
    if (submitterLogin != null) {
      profiler.addContext("submitter", submitterLogin);
    }
  }
}
