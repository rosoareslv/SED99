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

package org.apache.hadoop.yarn.server.resourcemanager.scheduler.capacity;

import org.apache.hadoop.yarn.api.protocolrecords.AllocateResponse;
import org.apache.hadoop.yarn.api.records.Container;
import org.apache.hadoop.yarn.api.records.ContainerId;
import org.apache.hadoop.yarn.api.records.ContainerResourceChangeRequest;
import org.apache.hadoop.yarn.api.records.ContainerState;
import org.apache.hadoop.yarn.api.records.NodeId;
import org.apache.hadoop.yarn.api.records.Resource;
import org.apache.hadoop.yarn.conf.YarnConfiguration;
import org.apache.hadoop.yarn.server.resourcemanager.MockAM;
import org.apache.hadoop.yarn.server.resourcemanager.MockNM;
import org.apache.hadoop.yarn.server.resourcemanager.MockRM;
import org.apache.hadoop.yarn.server.resourcemanager.nodelabels.NullRMNodeLabelsManager;
import org.apache.hadoop.yarn.server.resourcemanager.nodelabels.RMNodeLabelsManager;
import org.apache.hadoop.yarn.server.resourcemanager.rmapp.RMApp;
import org.apache.hadoop.yarn.server.resourcemanager.rmcontainer.RMContainer;
import org.apache.hadoop.yarn.server.resourcemanager.rmcontainer.RMContainerState;
import org.apache.hadoop.yarn.server.resourcemanager.scheduler.ResourceScheduler;
import org.apache.hadoop.yarn.server.resourcemanager.scheduler.SchedulerNode;
import org.apache.hadoop.yarn.server.resourcemanager.scheduler.common.fica.FiCaSchedulerApp;
import org.apache.hadoop.yarn.util.resource.Resources;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class TestIncreaseAllocationExpirer {
  private final int GB = 1024;
  private YarnConfiguration conf;
  RMNodeLabelsManager mgr;

  @Before
  public void setUp() throws Exception {
    conf = new YarnConfiguration();
    conf.setClass(YarnConfiguration.RM_SCHEDULER, CapacityScheduler.class,
        ResourceScheduler.class);
    mgr = new NullRMNodeLabelsManager();
    mgr.init(conf);
  }

  @Test
  public void testContainerIsRemovedFromAllocationExpirer()
      throws Exception {
    /**
     * 1. Allocate 1 container: containerId2 (1G)
     * 2. Increase resource of containerId2: 1G -> 3G
     * 3. AM acquires the token
     * 4. AM uses the token
     * 5. Verify containerId2 is removed from allocation expirer such
     *    that it still runs fine after allocation expiration interval
     */
    // Set the allocation expiration to 5 seconds
    conf.setLong(
        YarnConfiguration.RM_CONTAINER_ALLOC_EXPIRY_INTERVAL_MS, 5000);
    MockRM rm1 = new MockRM(conf);
    rm1.start();
    // Submit an application
    MockNM nm1 = rm1.registerNode("127.0.0.1:1234", 20 * GB);
    RMApp app1 = rm1.submitApp(1 * GB, "app", "user", null, "default");
    MockAM am1 = MockRM.launchAndRegisterAM(app1, rm1, nm1);
    // Report AM container status RUNNING to remove it from expirer
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 1, ContainerState.RUNNING);
    // AM request a new container
    am1.allocate("127.0.0.1", 1 * GB, 1, new ArrayList<ContainerId>());
    ContainerId containerId2 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 2);
    rm1.waitForState(nm1, containerId2, RMContainerState.ALLOCATED);
    // AM acquire a new container to start container allocation expirer
    List<Container> containers =
        am1.allocate(null, null).getAllocatedContainers();
    Assert.assertEquals(containerId2, containers.get(0).getId());
    Assert.assertNotNull(containers.get(0).getContainerToken());
    checkUsedResource(rm1, "default", 2 * GB, null);
    FiCaSchedulerApp app = TestUtils.getFiCaSchedulerApp(
        rm1, app1.getApplicationId());
    Assert.assertEquals(2 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 18 * GB);
    // Report container status
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 2, ContainerState.RUNNING);
    // Wait until container status is RUNNING, and is removed from
    // allocation expirer
    rm1.waitForState(nm1, containerId2, RMContainerState.RUNNING);
    // am1 asks to increase containerId2 from 1GB to 3GB
    am1.sendContainerResizingRequest(Collections.singletonList(
        ContainerResourceChangeRequest.newInstance(
            containerId2, Resources.createResource(3 * GB))), null);
    // Kick off scheduling and sleep for 1 second;
    nm1.nodeHeartbeat(true);
    Thread.sleep(1000);
    // Start container increase allocation expirer"
    am1.allocate(null, null);
    // Remember the resource in order to report status
    Resource resource = Resources.clone(
        rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource());
    nm1.containerIncreaseStatus(getContainer(rm1, containerId2, resource));
    // Wait long enough and verify that the container was removed
    // from allocation expirer, and the container is still running
    Thread.sleep(10000);
    Assert.assertEquals(RMContainerState.RUNNING,
        rm1.getResourceScheduler().getRMContainer(containerId2).getState());
    // Verify container size is 3G
    Assert.assertEquals(
        3 * GB, rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource().getMemory());
    // Verify total resource usage
    checkUsedResource(rm1, "default", 4 * GB, null);
    Assert.assertEquals(4 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    // Verify available resource
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 16 * GB);
    rm1.stop();
  }

  @Test
  public void testContainerIncreaseAllocationExpiration()
      throws Exception {
    /**
     * 1. Allocate 1 container: containerId2 (1G)
     * 2. Increase resource of containerId2: 1G -> 3G
     * 3. AM acquires the token
     * 4. AM does not use the token
     * 5. Verify containerId2's resource usage falls back to
     *    1G after the increase token expires
     */
    // Set the allocation expiration to 5 seconds
    conf.setLong(YarnConfiguration.RM_CONTAINER_ALLOC_EXPIRY_INTERVAL_MS, 5000);
    MockRM rm1 = new MockRM(conf);
    rm1.start();
    MockNM nm1 = rm1.registerNode("127.0.0.1:1234", 20 * GB);
    RMApp app1 = rm1.submitApp(1 * GB, "app", "user", null, "default");
    MockAM am1 = MockRM.launchAndRegisterAM(app1, rm1, nm1);
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 1, ContainerState.RUNNING);
    am1.allocate("127.0.0.1", 1 * GB, 1, new ArrayList<ContainerId>());
    ContainerId containerId2 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 2);
    rm1.waitForState(nm1, containerId2, RMContainerState.ALLOCATED);
    List<Container> containers =
        am1.allocate(null, null).getAllocatedContainers();
    Assert.assertEquals(containerId2, containers.get(0).getId());
    Assert.assertNotNull(containers.get(0).getContainerToken());
    checkUsedResource(rm1, "default", 2 * GB, null);
    FiCaSchedulerApp app = TestUtils.getFiCaSchedulerApp(
        rm1, app1.getApplicationId());
    Assert.assertEquals(2 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 18 * GB);
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 2, ContainerState.RUNNING);
    rm1.waitForState(nm1, containerId2, RMContainerState.RUNNING);
    // am1 asks to increase containerId2 from 1GB to 3GB
    am1.sendContainerResizingRequest(Collections.singletonList(
        ContainerResourceChangeRequest.newInstance(
            containerId2, Resources.createResource(3 * GB))), null);
    // Kick off scheduling and wait for 1 second;
    nm1.nodeHeartbeat(true);
    Thread.sleep(1000);
    // Start container increase allocation expirer
    am1.allocate(null, null);
    // Verify resource usage
    checkUsedResource(rm1, "default", 4 * GB, null);
    Assert.assertEquals(4 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 16 * GB);
    // Wait long enough for the increase token to expire, and for the roll
    // back action to complete
    Thread.sleep(10000);
    // Verify container size is 1G
    Assert.assertEquals(
        1 * GB, rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource().getMemory());
    // Verify total resource usage is 2G
    checkUsedResource(rm1, "default", 2 * GB, null);
    Assert.assertEquals(2 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    // Verify available resource is rolled back to 18GB
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 18 * GB);
    rm1.stop();
  }

  @Test
  public void testConsecutiveContainerIncreaseAllocationExpiration()
      throws Exception {
    /**
     * 1. Allocate 1 container: containerId2 (1G)
     * 2. Increase resource of containerId2: 1G -> 3G
     * 3. AM acquires the token
     * 4. Increase resource of containerId2 again: 3G -> 5G
     * 5. AM acquires the token
     * 6. AM uses the first token to increase the container in NM to 3G
     * 7. AM NEVER uses the second token
     * 8. Verify containerId2 eventually is allocated 3G after token expires
     * 9. Verify NM eventually uses 3G for containerId2
     */
    // Set the allocation expiration to 5 seconds
    conf.setLong(YarnConfiguration.RM_CONTAINER_ALLOC_EXPIRY_INTERVAL_MS, 5000);
    MockRM rm1 = new MockRM(conf);
    rm1.start();
    // Submit an application
    MockNM nm1 = rm1.registerNode("127.0.0.1:1234", 20 * GB);
    RMApp app1 = rm1.submitApp(1 * GB, "app", "user", null, "default");
    MockAM am1 = MockRM.launchAndRegisterAM(app1, rm1, nm1);
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 1, ContainerState.RUNNING);
    // AM request a new container
    am1.allocate("127.0.0.1", 1 * GB, 1, new ArrayList<ContainerId>());
    ContainerId containerId2 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 2);
    rm1.waitForState(nm1, containerId2, RMContainerState.ALLOCATED);
    // AM acquire a new container to start container allocation expirer
    am1.allocate(null, null).getAllocatedContainers();
    // Report container status
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 2, ContainerState.RUNNING);
    // Wait until container status is RUNNING, and is removed from
    // allocation expirer
    rm1.waitForState(nm1, containerId2, RMContainerState.RUNNING);
    // am1 asks to change containerId2 from 1GB to 3GB
    am1.sendContainerResizingRequest(Collections.singletonList(
        ContainerResourceChangeRequest.newInstance(
            containerId2, Resources.createResource(3 * GB))), null);
    // Kick off scheduling and sleep for 1 second to
    // make sure the allocation is done
    nm1.nodeHeartbeat(true);
    Thread.sleep(1000);
    // Start container increase allocation expirer
    am1.allocate(null, null);
    // Remember the resource (3G) in order to report status
    Resource resource1 = Resources.clone(
        rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource());
    // am1 asks to change containerId2 from 3GB to 5GB
    am1.sendContainerResizingRequest(Collections.singletonList(
        ContainerResourceChangeRequest.newInstance(
            containerId2, Resources.createResource(5 * GB))), null);
    // Kick off scheduling and sleep for 1 second to
    // make sure the allocation is done
    nm1.nodeHeartbeat(true);
    Thread.sleep(1000);
    // Reset container increase allocation expirer
    am1.allocate(null, null);
    // Verify current resource allocation in RM
    checkUsedResource(rm1, "default", 6 * GB, null);
    FiCaSchedulerApp app = TestUtils.getFiCaSchedulerApp(
        rm1, app1.getApplicationId());
    Assert.assertEquals(6 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    // Verify available resource is now reduced to 14GB
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 14 * GB);
    // Use the first token (3G)
    nm1.containerIncreaseStatus(getContainer(rm1, containerId2, resource1));
    // Wait long enough for the second token (5G) to expire, and verify that
    // the roll back action is completed as expected
    Thread.sleep(10000);
    // Verify container size is rolled back to 3G
    Assert.assertEquals(
        3 * GB, rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource().getMemory());
    // Verify total resource usage is 4G
    checkUsedResource(rm1, "default", 4 * GB, null);
    Assert.assertEquals(4 * GB,
        app.getAppAttemptResourceUsage().getUsed().getMemory());
    // Verify available resource is rolled back to 14GB
    verifyAvailableResourceOfSchedulerNode(rm1, nm1.getNodeId(), 16 * GB);
    // Verify NM receives the decrease message (3G)
    List<Container> containersToDecrease =
        nm1.nodeHeartbeat(true).getContainersToDecrease();
    Assert.assertEquals(1, containersToDecrease.size());
    Assert.assertEquals(
        3 * GB, containersToDecrease.get(0).getResource().getMemory());
    rm1.stop();
  }

  @Test
  public void testDecreaseAfterIncreaseWithAllocationExpiration()
      throws Exception {
    /**
     * 1. Allocate three containers: containerId2, containerId3, containerId4
     * 2. Increase resource of containerId2: 3G -> 6G
     * 3. Increase resource of containerId3: 3G -> 6G
     * 4. Increase resource of containerId4: 3G -> 6G
     * 5. Do NOT use the increase tokens for containerId2 and containerId3
     * 6. Decrease containerId2: 6G -> 2G (i.e., below last confirmed resource)
     * 7. Decrease containerId3: 6G -> 4G (i.e., above last confirmed resource)
     * 8. Decrease containerId4: 6G -> 4G (i.e., above last confirmed resource)
     * 9. Use token for containerId4 to increase containerId4 on NM to 6G
     * 10. Verify containerId2 eventually uses 2G (removed from expirer)
     * 11. verify containerId3 eventually uses 3G (increase token expires)
     * 12. Verify containerId4 eventually uses 4G (removed from expirer)
     * 13. Verify NM evetually uses 3G for containerId3, 4G for containerId4
     */
    // Set the allocation expiration to 5 seconds
    conf.setLong(YarnConfiguration.RM_CONTAINER_ALLOC_EXPIRY_INTERVAL_MS, 5000);
    MockRM rm1 = new MockRM(conf);
    rm1.start();
    // Submit an application
    MockNM nm1 = rm1.registerNode("127.0.0.1:1234", 20 * GB);
    RMApp app1 = rm1.submitApp(1 * GB, "app", "user", null, "default");
    MockAM am1 = MockRM.launchAndRegisterAM(app1, rm1, nm1);
    nm1.nodeHeartbeat(
        app1.getCurrentAppAttempt()
            .getAppAttemptId(), 1, ContainerState.RUNNING);
    // AM request two new continers
    am1.allocate("127.0.0.1", 3 * GB, 3, new ArrayList<ContainerId>());
    ContainerId containerId2 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 2);
    rm1.waitForState(nm1, containerId2, RMContainerState.ALLOCATED);
    ContainerId containerId3 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 3);
    rm1.waitForState(nm1, containerId3, RMContainerState.ALLOCATED);
    ContainerId containerId4 =
        ContainerId.newContainerId(am1.getApplicationAttemptId(), 4);
    rm1.waitForState(nm1, containerId4, RMContainerState.ALLOCATED);
    // AM acquires tokens to start container allocation expirer
    List<Container> containers =
        am1.allocate(null, null).getAllocatedContainers();
    Assert.assertEquals(3, containers.size());
    Assert.assertNotNull(containers.get(0).getContainerToken());
    Assert.assertNotNull(containers.get(1).getContainerToken());
    Assert.assertNotNull(containers.get(2).getContainerToken());
    // Report container status
    nm1.nodeHeartbeat(app1.getCurrentAppAttempt().getAppAttemptId(),
        2, ContainerState.RUNNING);
    nm1.nodeHeartbeat(app1.getCurrentAppAttempt().getAppAttemptId(),
        3, ContainerState.RUNNING);
    nm1.nodeHeartbeat(app1.getCurrentAppAttempt().getAppAttemptId(),
        4, ContainerState.RUNNING);
    // Wait until container status becomes RUNNING
    rm1.waitForState(nm1, containerId2, RMContainerState.RUNNING);
    rm1.waitForState(nm1, containerId3, RMContainerState.RUNNING);
    rm1.waitForState(nm1, containerId4, RMContainerState.RUNNING);
    // am1 asks to change containerId2 and containerId3 from 1GB to 3GB
    List<ContainerResourceChangeRequest> increaseRequests = new ArrayList<>();
    increaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId2, Resources.createResource(6 * GB)));
    increaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId3, Resources.createResource(6 * GB)));
    increaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId4, Resources.createResource(6 * GB)));
    am1.sendContainerResizingRequest(increaseRequests, null);
    nm1.nodeHeartbeat(true);
    Thread.sleep(1000);
    // Start container increase allocation expirer
    am1.allocate(null, null);
    // Decrease containers
    List<ContainerResourceChangeRequest> decreaseRequests = new ArrayList<>();
    decreaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId2, Resources.createResource(2 * GB)));
    decreaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId3, Resources.createResource(4 * GB)));
    decreaseRequests.add(ContainerResourceChangeRequest.newInstance(
        containerId4, Resources.createResource(4 * GB)));
    AllocateResponse response =
        am1.sendContainerResizingRequest(null, decreaseRequests);
    // Verify containers are decreased in scheduler
    Assert.assertEquals(3, response.getDecreasedContainers().size());
    // Use the token for containerId4 on NM (6G). This should set the last
    // confirmed resource to 4G, and cancel the allocation expirer
    nm1.containerIncreaseStatus(getContainer(
        rm1, containerId4, Resources.createResource(6 * GB)));
    // Wait for containerId3 token to expire,
    Thread.sleep(10000);
    Assert.assertEquals(
        2 * GB, rm1.getResourceScheduler().getRMContainer(containerId2)
            .getAllocatedResource().getMemory());
    Assert.assertEquals(
        3 * GB, rm1.getResourceScheduler().getRMContainer(containerId3)
            .getAllocatedResource().getMemory());
    Assert.assertEquals(
        4 * GB, rm1.getResourceScheduler().getRMContainer(containerId4)
            .getAllocatedResource().getMemory());
    // Verify NM receives 2 decrease message
    List<Container> containersToDecrease =
        nm1.nodeHeartbeat(true).getContainersToDecrease();
    Assert.assertEquals(2, containersToDecrease.size());
    // Sort the list to make sure containerId3 is the first
    Collections.sort(containersToDecrease);
    Assert.assertEquals(
        3 * GB, containersToDecrease.get(0).getResource().getMemory());
    Assert.assertEquals(
        4 * GB, containersToDecrease.get(1).getResource().getMemory());
    rm1.stop();
  }

  private void checkUsedResource(MockRM rm, String queueName, int memory,
      String label) {
    CapacityScheduler cs = (CapacityScheduler) rm.getResourceScheduler();
    CSQueue queue = cs.getQueue(queueName);
    Assert.assertEquals(memory,
        queue.getQueueResourceUsage()
            .getUsed(label == null ? RMNodeLabelsManager.NO_LABEL : label)
            .getMemory());
  }

  private void verifyAvailableResourceOfSchedulerNode(MockRM rm, NodeId nodeId,
      int expectedMemory) {
    CapacityScheduler cs = (CapacityScheduler) rm.getResourceScheduler();
    SchedulerNode node = cs.getNode(nodeId);
    Assert
        .assertEquals(expectedMemory, node.getUnallocatedResource().getMemory());
  }

  private Container getContainer(
      MockRM rm, ContainerId containerId, Resource resource) {
    RMContainer rmContainer = rm.getResourceScheduler()
        .getRMContainer(containerId);
    return Container.newInstance(
        containerId, rmContainer.getAllocatedNode(), null,
            resource, null, null);
  }
}
