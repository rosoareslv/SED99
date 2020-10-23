/**
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */

package org.apache.hadoop.yarn.submarine.client.cli;

import com.google.common.annotations.VisibleForTesting;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.GnuParser;
import org.apache.commons.cli.HelpFormatter;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.ParseException;
import org.apache.hadoop.yarn.api.records.ApplicationId;
import org.apache.hadoop.yarn.exceptions.YarnException;
import org.apache.hadoop.yarn.submarine.client.cli.param.RunJobParameters;
import org.apache.hadoop.yarn.submarine.common.ClientContext;
import org.apache.hadoop.yarn.submarine.common.exception.SubmarineException;
import org.apache.hadoop.yarn.submarine.runtimes.common.JobMonitor;
import org.apache.hadoop.yarn.submarine.runtimes.common.JobSubmitter;
import org.apache.hadoop.yarn.submarine.runtimes.common.StorageKeyConstants;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

public class RunJobCli extends AbstractCli {
  private static final Logger LOG =
      LoggerFactory.getLogger(RunJobCli.class);

  private Options options;
  private RunJobParameters parameters = new RunJobParameters();

  private JobSubmitter jobSubmitter;
  private JobMonitor jobMonitor;

  public RunJobCli(ClientContext cliContext) {
    this(cliContext, cliContext.getRuntimeFactory().getJobSubmitterInstance(),
        cliContext.getRuntimeFactory().getJobMonitorInstance());
  }

  @VisibleForTesting
  public RunJobCli(ClientContext cliContext, JobSubmitter jobSubmitter,
      JobMonitor jobMonitor) {
    super(cliContext);
    options = generateOptions();
    this.jobSubmitter = jobSubmitter;
    this.jobMonitor = jobMonitor;
  }

  public void printUsages() {
    new HelpFormatter().printHelp("job run", options);
  }

  private Options generateOptions() {
    Options options = new Options();
    options.addOption(CliConstants.NAME, true, "Name of the job");
    options.addOption(CliConstants.INPUT_PATH, true,
        "Input of the job, could be local or other FS directory");
    options.addOption(CliConstants.CHECKPOINT_PATH, true,
        "Training output directory of the job, "
            + "could be local or other FS directory. This typically includes "
            + "checkpoint files and exported model ");
    options.addOption(CliConstants.SAVED_MODEL_PATH, true,
        "Model exported path (savedmodel) of the job, which is needed when "
            + "exported model is not placed under ${checkpoint_path}"
            + "could be local or other FS directory. This will be used to serve.");
    options.addOption(CliConstants.N_WORKERS, true,
        "Numnber of worker tasks of the job, by default it's 1");
    options.addOption(CliConstants.N_PS, true,
        "Number of PS tasks of the job, by default it's 0");
    options.addOption(CliConstants.WORKER_RES, true,
        "Resource of each worker, for example "
            + "memory-mb=2048,vcores=2,yarn.io/gpu=2");
    options.addOption(CliConstants.PS_RES, true,
        "Resource of each PS, for example "
            + "memory-mb=2048,vcores=2,yarn.io/gpu=2");
    options.addOption(CliConstants.DOCKER_IMAGE, true, "Docker image name/tag");
    options.addOption(CliConstants.QUEUE, true,
        "Name of queue to run the job, by default it uses default queue");
    options.addOption(CliConstants.TENSORBOARD, true,
        "Should we run TensorBoard" + " for this job? By default it's true");
    options.addOption(CliConstants.WORKER_LAUNCH_CMD, true,
        "Commandline of worker, arguments will be "
            + "directly used to launch the worker");
    options.addOption(CliConstants.PS_LAUNCH_CMD, true,
        "Commandline of worker, arguments will be "
            + "directly used to launch the PS");
    options.addOption(CliConstants.ENV, true,
        "Common environment variable of worker/ps");
    options.addOption(CliConstants.VERBOSE, false,
        "Print verbose log for troubleshooting");
    options.addOption(CliConstants.WAIT_JOB_FINISH, false,
        "Specified when user want to wait the job finish");
    options.addOption(CliConstants.PS_DOCKER_IMAGE, true,
        "Specify docker image for PS, when this is not specified, PS uses --"
            + CliConstants.DOCKER_IMAGE + " as default.");
    options.addOption(CliConstants.WORKER_DOCKER_IMAGE, true,
        "Specify docker image for WORKER, when this is not specified, WORKER "
            + "uses --" + CliConstants.DOCKER_IMAGE + " as default.");
    options.addOption("h", "help", false, "Print help");
    return options;
  }

  private void replacePatternsInParameters() throws IOException {
    if (parameters.getPSLaunchCmd() != null && !parameters.getPSLaunchCmd()
        .isEmpty()) {
      String afterReplace = CliUtils.replacePatternsInLaunchCommand(
          parameters.getPSLaunchCmd(), parameters,
          clientContext.getRemoteDirectoryManager());
      parameters.setPSLaunchCmd(afterReplace);
    }

    if (parameters.getWorkerLaunchCmd() != null && !parameters
        .getWorkerLaunchCmd().isEmpty()) {
      String afterReplace = CliUtils.replacePatternsInLaunchCommand(
          parameters.getWorkerLaunchCmd(), parameters,
          clientContext.getRemoteDirectoryManager());
      parameters.setWorkerLaunchCmd(afterReplace);
    }
  }

  private void parseCommandLineAndGetRunJobParameters(String[] args)
      throws ParseException, IOException, YarnException {
    try {
      // Do parsing
      GnuParser parser = new GnuParser();
      CommandLine cli = parser.parse(options, args);
      parameters.updateParametersByParsedCommandline(cli, options, clientContext);
    } catch (ParseException e) {
      LOG.error("Exception in parse:", e.getMessage());
      printUsages();
      throw e;
    }

    // replace patterns
    replacePatternsInParameters();
  }

  private void storeJobInformation(String jobName, ApplicationId applicationId,
      String[] args) throws IOException {
    Map<String, String> jobInfo = new HashMap<>();
    jobInfo.put(StorageKeyConstants.JOB_NAME, jobName);
    jobInfo.put(StorageKeyConstants.APPLICATION_ID, applicationId.toString());

    if (parameters.getCheckpointPath() != null) {
      jobInfo.put(StorageKeyConstants.CHECKPOINT_PATH,
          parameters.getCheckpointPath());
    }
    if (parameters.getInputPath() != null) {
      jobInfo.put(StorageKeyConstants.INPUT_PATH,
          parameters.getInputPath());
    }
    if (parameters.getSavedModelPath() != null) {
      jobInfo.put(StorageKeyConstants.SAVED_MODEL_PATH,
          parameters.getSavedModelPath());
    }

    String joinedArgs = String.join(" ", args);
    jobInfo.put(StorageKeyConstants.JOB_RUN_ARGS, joinedArgs);
    clientContext.getRuntimeFactory().getSubmarineStorage().addNewJob(jobName,
        jobInfo);
  }

  @Override
  public int run(String[] args)
      throws ParseException, IOException, YarnException, InterruptedException,
      SubmarineException {
    if (CliUtils.argsForHelp(args)) {
      printUsages();
      return 0;
    }

    parseCommandLineAndGetRunJobParameters(args);
    ApplicationId applicationId = this.jobSubmitter.submitJob(parameters);
    storeJobInformation(parameters.getName(), applicationId, args);
    if (parameters.isWaitJobFinish()) {
      this.jobMonitor.waitTrainingFinal(parameters.getName());
    }

    return 0;
  }

  @VisibleForTesting
  public JobSubmitter getJobSubmitter() {
    return jobSubmitter;
  }

  @VisibleForTesting
  RunJobParameters getRunJobParameters() {
    return parameters;
  }
}
