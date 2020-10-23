/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.hadoop.yarn.server.resourcemanager.metrics;

import java.util.HashMap;
import java.util.Map;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.classification.InterfaceAudience.Private;
import org.apache.hadoop.classification.InterfaceStability.Unstable;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.security.UserGroupInformation;
import org.apache.hadoop.yarn.api.records.ApplicationAttemptId;
import org.apache.hadoop.yarn.api.records.ApplicationId;
import org.apache.hadoop.yarn.api.records.YarnApplicationState;
import org.apache.hadoop.yarn.api.records.ContainerId;
import org.apache.hadoop.yarn.api.records.timelineservice.ApplicationAttemptEntity;
import org.apache.hadoop.yarn.api.records.timelineservice.ApplicationEntity;
import org.apache.hadoop.yarn.api.records.timelineservice.ContainerEntity;
import org.apache.hadoop.yarn.api.records.timelineservice.TimelineEntities;
import org.apache.hadoop.yarn.api.records.timelineservice.TimelineEntity;
import org.apache.hadoop.yarn.api.records.timelineservice.TimelineEntity.Identifier;
import org.apache.hadoop.yarn.api.records.timelineservice.TimelineEntityType;
import org.apache.hadoop.yarn.api.records.timelineservice.TimelineEvent;
import org.apache.hadoop.yarn.conf.YarnConfiguration;
import org.apache.hadoop.yarn.event.EventHandler;
import org.apache.hadoop.yarn.server.metrics.AppAttemptMetricsConstants;
import org.apache.hadoop.yarn.server.metrics.ApplicationMetricsConstants;
import org.apache.hadoop.yarn.server.metrics.ContainerMetricsConstants;
import org.apache.hadoop.yarn.server.resourcemanager.RMContext;
import org.apache.hadoop.yarn.server.resourcemanager.RMServerUtils;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.RMApp;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.RMAppImpl;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.RMAppMetrics;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.RMAppState;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.attempt.RMAppAttempt;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.attempt.RMAppAttemptState;
import org.apache.hadoop.yarn.server.resourcemanager.rmcontainer.RMContainer;
import org.apache.hadoop.yarn.server.resourcemanager.timelineservice.RMTimelineCollectorManager;
import org.apache.hadoop.yarn.server.timelineservice.collector.TimelineCollector;
import org.apache.hadoop.yarn.util.timeline.TimelineUtils;

import com.google.common.annotations.VisibleForTesting;

/**
 * This class is responsible for posting application, appattempt &amp; Container
 * lifecycle related events to timeline service v2.
 */
@Private
@Unstable
public class TimelineServiceV2Publisher extends AbstractSystemMetricsPublisher {
  private static final Log LOG =
      LogFactory.getLog(TimelineServiceV2Publisher.class);
  private RMTimelineCollectorManager rmTimelineCollectorManager;
  private boolean publishContainerEvents;

  public TimelineServiceV2Publisher(RMContext rmContext) {
    super("TimelineserviceV2Publisher");
    rmTimelineCollectorManager = rmContext.getRMTimelineCollectorManager();
  }

  @Override
  protected void serviceInit(Configuration conf) throws Exception {
    super.serviceInit(conf);
    getDispatcher().register(SystemMetricsEventType.class,
        new TimelineV2EventHandler());
    publishContainerEvents = getConfig().getBoolean(
        YarnConfiguration.RM_PUBLISH_CONTAINER_EVENTS_ENABLED,
        YarnConfiguration.DEFAULT_RM_PUBLISH_CONTAINER_EVENTS_ENABLED);
  }

  @VisibleForTesting
  boolean isPublishContainerEvents() {
    return publishContainerEvents;
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appCreated(RMApp app, long createdTime) {
    ApplicationEntity entity = createApplicationEntity(app.getApplicationId());
    entity.setQueue(app.getQueue());
    entity.setCreatedTime(createdTime);

    Map<String, Object> entityInfo = new HashMap<String, Object>();
    entityInfo.put(ApplicationMetricsConstants.NAME_ENTITY_INFO, app.getName());
    entityInfo.put(ApplicationMetricsConstants.TYPE_ENTITY_INFO,
        app.getApplicationType());
    entityInfo.put(ApplicationMetricsConstants.USER_ENTITY_INFO, app.getUser());
    entityInfo.put(ApplicationMetricsConstants.SUBMITTED_TIME_ENTITY_INFO,
        app.getSubmitTime());
    entityInfo.put(ApplicationMetricsConstants.APP_TAGS_INFO,
        app.getApplicationTags());
    entityInfo.put(
        ApplicationMetricsConstants.UNMANAGED_APPLICATION_ENTITY_INFO,
        app.getApplicationSubmissionContext().getUnmanagedAM());
    entityInfo.put(ApplicationMetricsConstants.APPLICATION_PRIORITY_INFO,
        app.getApplicationSubmissionContext().getPriority().getPriority());
    entity.getConfigs().put(
        ApplicationMetricsConstants.AM_NODE_LABEL_EXPRESSION,
        app.getAmNodeLabelExpression());
    entity.getConfigs().put(
        ApplicationMetricsConstants.APP_NODE_LABEL_EXPRESSION,
        app.getAppNodeLabelExpression());
    if (app.getCallerContext() != null) {
      if (app.getCallerContext().getContext() != null) {
        entityInfo.put(ApplicationMetricsConstants.YARN_APP_CALLER_CONTEXT,
            app.getCallerContext().getContext());
      }
      if (app.getCallerContext().getSignature() != null) {
        entityInfo.put(ApplicationMetricsConstants.YARN_APP_CALLER_SIGNATURE,
            app.getCallerContext().getSignature());
      }
    }

    entity.setInfo(entityInfo);
    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(ApplicationMetricsConstants.CREATED_EVENT_TYPE);
    tEvent.setTimestamp(createdTime);
    entity.addEvent(tEvent);

    getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
        SystemMetricsEventType.PUBLISH_ENTITY, entity, app.getApplicationId()));
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appFinished(RMApp app, RMAppState state, long finishedTime) {
    ApplicationEntity entity = createApplicationEntity(app.getApplicationId());
    RMAppMetrics appMetrics = app.getRMAppMetrics();
    entity.addInfo(ApplicationMetricsConstants.APP_CPU_METRICS,
        appMetrics.getVcoreSeconds());
    entity.addInfo(ApplicationMetricsConstants.APP_MEM_METRICS,
        appMetrics.getMemorySeconds());

    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(ApplicationMetricsConstants.FINISHED_EVENT_TYPE);
    tEvent.setTimestamp(finishedTime);
    Map<String, Object> eventInfo = new HashMap<String, Object>();
    eventInfo.put(ApplicationMetricsConstants.DIAGNOSTICS_INFO_EVENT_INFO,
        app.getDiagnostics().toString());
    eventInfo.put(ApplicationMetricsConstants.FINAL_STATUS_EVENT_INFO,
        app.getFinalApplicationStatus().toString());
    eventInfo.put(ApplicationMetricsConstants.STATE_EVENT_INFO,
        RMServerUtils.createApplicationState(state).toString());
    ApplicationAttemptId appAttemptId = app.getCurrentAppAttempt() == null
        ? null : app.getCurrentAppAttempt().getAppAttemptId();
    if (appAttemptId != null) {
      eventInfo.put(ApplicationMetricsConstants.LATEST_APP_ATTEMPT_EVENT_INFO,
          appAttemptId.toString());
    }
    tEvent.setInfo(eventInfo);

    entity.addEvent(tEvent);

    getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
        SystemMetricsEventType.PUBLISH_ENTITY, entity, app.getApplicationId()));
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appStateUpdated(RMApp app, YarnApplicationState appState,
      long updatedTime) {
    ApplicationEntity entity =
        createApplicationEntity(app.getApplicationId());
    Map<String, Object> eventInfo = new HashMap<String, Object>();
    eventInfo.put(ApplicationMetricsConstants.STATE_EVENT_INFO,
        appState);
    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(ApplicationMetricsConstants.STATE_UPDATED_EVENT_TYPE);
    tEvent.setTimestamp(updatedTime);
    tEvent.setInfo(eventInfo);
    entity.addEvent(tEvent);

    getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
        SystemMetricsEventType.PUBLISH_ENTITY, entity, app.getApplicationId()));
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appACLsUpdated(RMApp app, String appViewACLs, long updatedTime) {
    ApplicationEntity entity = createApplicationEntity(app.getApplicationId());
    TimelineEvent tEvent = new TimelineEvent();
    Map<String, Object> entityInfo = new HashMap<String, Object>();
    entityInfo.put(ApplicationMetricsConstants.APP_VIEW_ACLS_ENTITY_INFO,
        (appViewACLs == null) ? "" : appViewACLs);
    entity.setInfo(entityInfo);
    tEvent.setId(ApplicationMetricsConstants.ACLS_UPDATED_EVENT_TYPE);
    tEvent.setTimestamp(updatedTime);
    entity.addEvent(tEvent);

    getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
        SystemMetricsEventType.PUBLISH_ENTITY, entity, app.getApplicationId()));
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appUpdated(RMApp app, long currentTimeMillis) {
    ApplicationEntity entity = createApplicationEntity(app.getApplicationId());
    Map<String, Object> eventInfo = new HashMap<String, Object>();
    eventInfo.put(ApplicationMetricsConstants.QUEUE_ENTITY_INFO,
        app.getQueue());
    eventInfo.put(ApplicationMetricsConstants.APPLICATION_PRIORITY_INFO,
        app.getApplicationSubmissionContext().getPriority().getPriority());
    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(ApplicationMetricsConstants.UPDATED_EVENT_TYPE);
    tEvent.setTimestamp(currentTimeMillis);
    tEvent.setInfo(eventInfo);
    entity.addEvent(tEvent);
    getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
        SystemMetricsEventType.PUBLISH_ENTITY, entity, app.getApplicationId()));
  }

  private static ApplicationEntity createApplicationEntity(
      ApplicationId applicationId) {
    ApplicationEntity entity = new ApplicationEntity();
    entity.setId(applicationId.toString());
    return entity;
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appAttemptRegistered(RMAppAttempt appAttempt,
      long registeredTime) {
    TimelineEntity entity =
        createAppAttemptEntity(appAttempt.getAppAttemptId());
    entity.setCreatedTime(registeredTime);

    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(AppAttemptMetricsConstants.REGISTERED_EVENT_TYPE);
    tEvent.setTimestamp(registeredTime);
    Map<String, Object> eventInfo = new HashMap<String, Object>();
    eventInfo.put(AppAttemptMetricsConstants.TRACKING_URL_EVENT_INFO,
        appAttempt.getTrackingUrl());
    eventInfo.put(AppAttemptMetricsConstants.ORIGINAL_TRACKING_URL_EVENT_INFO,
        appAttempt.getOriginalTrackingUrl());
    eventInfo.put(AppAttemptMetricsConstants.HOST_EVENT_INFO,
        appAttempt.getHost());
    eventInfo.put(AppAttemptMetricsConstants.RPC_PORT_EVENT_INFO,
        appAttempt.getRpcPort());
    if (appAttempt.getMasterContainer() != null) {
      eventInfo.put(AppAttemptMetricsConstants.MASTER_CONTAINER_EVENT_INFO,
          appAttempt.getMasterContainer().getId().toString());
    }
    tEvent.setInfo(eventInfo);
    entity.addEvent(tEvent);
    getDispatcher().getEventHandler().handle(
        new TimelineV2PublishEvent(SystemMetricsEventType.PUBLISH_ENTITY,
            entity, appAttempt.getAppAttemptId().getApplicationId()));
  }

  @SuppressWarnings("unchecked")
  @Override
  public void appAttemptFinished(RMAppAttempt appAttempt,
      RMAppAttemptState appAttemtpState, RMApp app, long finishedTime) {

    ApplicationAttemptEntity entity =
        createAppAttemptEntity(appAttempt.getAppAttemptId());

    TimelineEvent tEvent = new TimelineEvent();
    tEvent.setId(AppAttemptMetricsConstants.FINISHED_EVENT_TYPE);
    tEvent.setTimestamp(finishedTime);
    Map<String, Object> eventInfo = new HashMap<String, Object>();
    eventInfo.put(AppAttemptMetricsConstants.TRACKING_URL_EVENT_INFO,
        appAttempt.getTrackingUrl());
    eventInfo.put(AppAttemptMetricsConstants.ORIGINAL_TRACKING_URL_EVENT_INFO,
        appAttempt.getOriginalTrackingUrl());
    eventInfo.put(AppAttemptMetricsConstants.DIAGNOSTICS_INFO_EVENT_INFO,
        appAttempt.getDiagnostics());
    // app will get the final status from app attempt, or create one
    // based on app state if it doesn't exist
    eventInfo.put(AppAttemptMetricsConstants.FINAL_STATUS_EVENT_INFO,
        app.getFinalApplicationStatus().toString());
    eventInfo.put(AppAttemptMetricsConstants.STATE_EVENT_INFO, RMServerUtils
        .createApplicationAttemptState(appAttemtpState).toString());
    if (appAttempt.getMasterContainer() != null) {
      eventInfo.put(AppAttemptMetricsConstants.MASTER_CONTAINER_EVENT_INFO,
          appAttempt.getMasterContainer().getId().toString());
    }
    tEvent.setInfo(eventInfo);

    entity.addEvent(tEvent);
    getDispatcher().getEventHandler().handle(
        new TimelineV2PublishEvent(SystemMetricsEventType.PUBLISH_ENTITY,
            entity, appAttempt.getAppAttemptId().getApplicationId()));
  }

  private static ApplicationAttemptEntity createAppAttemptEntity(
      ApplicationAttemptId appAttemptId) {
    ApplicationAttemptEntity entity = new ApplicationAttemptEntity();
    entity.setId(appAttemptId.toString());
    entity.setParent(new Identifier(TimelineEntityType.YARN_APPLICATION.name(),
        appAttemptId.getApplicationId().toString()));
    return entity;
  }

  @SuppressWarnings("unchecked")
  @Override
  public void containerCreated(RMContainer container, long createdTime) {
    if (publishContainerEvents) {
      TimelineEntity entity = createContainerEntity(container.getContainerId());
      entity.setCreatedTime(createdTime);

      TimelineEvent tEvent = new TimelineEvent();
      tEvent.setId(ContainerMetricsConstants.CREATED_IN_RM_EVENT_TYPE);
      tEvent.setTimestamp(createdTime);
      // updated as event info instead of entity info, as entity info is updated
      // by NM
      Map<String, Object> eventInfo = new HashMap<String, Object>();
      eventInfo.put(ContainerMetricsConstants.ALLOCATED_MEMORY_ENTITY_INFO,
          container.getAllocatedResource().getMemorySize());
      eventInfo.put(ContainerMetricsConstants.ALLOCATED_VCORE_ENTITY_INFO,
          container.getAllocatedResource().getVirtualCores());
      eventInfo.put(ContainerMetricsConstants.ALLOCATED_HOST_ENTITY_INFO,
          container.getAllocatedNode().getHost());
      eventInfo.put(ContainerMetricsConstants.ALLOCATED_PORT_ENTITY_INFO,
          container.getAllocatedNode().getPort());
      eventInfo.put(ContainerMetricsConstants.ALLOCATED_PRIORITY_ENTITY_INFO,
          container.getAllocatedPriority().getPriority());
      eventInfo.put(
          ContainerMetricsConstants.ALLOCATED_HOST_HTTP_ADDRESS_ENTITY_INFO,
          container.getNodeHttpAddress());
      tEvent.setInfo(eventInfo);

      entity.addEvent(tEvent);
      getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
          SystemMetricsEventType.PUBLISH_ENTITY, entity, container
              .getContainerId().getApplicationAttemptId().getApplicationId()));
    }
  }

  @SuppressWarnings("unchecked")
  @Override
  public void containerFinished(RMContainer container, long finishedTime) {
    if (publishContainerEvents) {
      TimelineEntity entity = createContainerEntity(container.getContainerId());

      TimelineEvent tEvent = new TimelineEvent();
      tEvent.setId(ContainerMetricsConstants.FINISHED_IN_RM_EVENT_TYPE);
      tEvent.setTimestamp(finishedTime);
      Map<String, Object> eventInfo = new HashMap<String, Object>();
      eventInfo.put(ContainerMetricsConstants.DIAGNOSTICS_INFO_EVENT_INFO,
          container.getDiagnosticsInfo());
      eventInfo.put(ContainerMetricsConstants.EXIT_STATUS_EVENT_INFO,
          container.getContainerExitStatus());
      eventInfo.put(ContainerMetricsConstants.STATE_EVENT_INFO,
          container.getContainerState().toString());
      Map<String, Object> entityInfo = new HashMap<String, Object>();
      entityInfo.put(ContainerMetricsConstants.ALLOCATED_HOST_ENTITY_INFO,
          container.getAllocatedNode().getHost());
      entityInfo.put(ContainerMetricsConstants.ALLOCATED_PORT_ENTITY_INFO,
          container.getAllocatedNode().getPort());
      entity.setInfo(entityInfo);
      tEvent.setInfo(eventInfo);

      entity.addEvent(tEvent);
      getDispatcher().getEventHandler().handle(new TimelineV2PublishEvent(
          SystemMetricsEventType.PUBLISH_ENTITY, entity, container
              .getContainerId().getApplicationAttemptId().getApplicationId()));
    }
  }

  private static ContainerEntity createContainerEntity(
      ContainerId containerId) {
    ContainerEntity entity = new ContainerEntity();
    entity.setId(containerId.toString());
    entity.setParent(new Identifier(TimelineEntityType.YARN_APPLICATION_ATTEMPT
        .name(), containerId.getApplicationAttemptId().toString()));
    return entity;
  }

  private void putEntity(TimelineEntity entity, ApplicationId appId) {
    try {
      if (LOG.isDebugEnabled()) {
        LOG.debug("Publishing the entity " + entity + ", JSON-style content: "
            + TimelineUtils.dumpTimelineRecordtoJSON(entity));
      }
      TimelineCollector timelineCollector =
          rmTimelineCollectorManager.get(appId);
      TimelineEntities entities = new TimelineEntities();
      entities.addEntity(entity);
      timelineCollector.putEntities(entities,
          UserGroupInformation.getCurrentUser());
    } catch (Exception e) {
      LOG.error("Error when publishing entity " + entity, e);
    }
  }

  private class ApplicationFinishPublishEvent extends TimelineV2PublishEvent {
    private RMAppImpl app;

    public ApplicationFinishPublishEvent(SystemMetricsEventType type,
        TimelineEntity entity, RMAppImpl app) {
      super(type, entity, app.getApplicationId());
      this.app = app;
    }

    public RMAppImpl getRMAppImpl() {
      return app;
    }
  }

  private class TimelineV2EventHandler
      implements EventHandler<TimelineV2PublishEvent> {
    @Override
    public void handle(TimelineV2PublishEvent event) {
      switch (event.getType()) {
      case PUBLISH_APPLICATION_FINISHED_ENTITY:
        putEntity(event.getEntity(), event.getApplicationId());
        ((ApplicationFinishPublishEvent) event).getRMAppImpl()
            .stopTimelineCollector();
        break;
      default:
        putEntity(event.getEntity(), event.getApplicationId());
        break;
      }
    }
  }

  private class TimelineV2PublishEvent extends TimelinePublishEvent {
    private TimelineEntity entity;

    public TimelineV2PublishEvent(SystemMetricsEventType type,
        TimelineEntity entity, ApplicationId appId) {
      super(type, appId);
      this.entity = entity;
    }

    public TimelineEntity getEntity() {
      return entity;
    }
  }
}
