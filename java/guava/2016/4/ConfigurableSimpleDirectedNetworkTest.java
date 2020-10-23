/*
 * Copyright (C) 2014 The Guava Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.common.graph;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.util.Set;

/**
 * Tests for a directed {@link ConfigurableNetwork}, creating a simple directed graph (parallel
 * and self-loop edges are not allowed).
 */
@RunWith(JUnit4.class)
public class ConfigurableSimpleDirectedNetworkTest extends AbstractDirectedNetworkTest {

  @Override
  public MutableNetwork<Integer, String> createGraph() {
    return NetworkBuilder.directed().allowsSelfLoops(false).build();
  }

  @Override
  @Test
  public void nodes_checkReturnedSetMutability() {
    Set<Integer> nodes = graph.nodes();
    try {
      nodes.add(N2);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addNode(N1);
      assertThat(graph.nodes()).containsExactlyElementsIn(nodes);
    }
  }

  @Override
  @Test
  public void edges_checkReturnedSetMutability() {
    Set<String> edges = graph.edges();
    try {
      edges.add(E12);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.edges()).containsExactlyElementsIn(edges);
    }
  }

  @Override
  @Test
  public void incidentEdges_checkReturnedSetMutability() {
    addNode(N1);
    Set<String> incidentEdges = graph.incidentEdges(N1);
    try {
      incidentEdges.add(E12);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.incidentEdges(N1)).containsExactlyElementsIn(incidentEdges);
    }
  }

  @Override
  @Test
  public void incidentNodes_checkReturnedSetMutability() {
    addEdge(E12, N1, N2);
    Set<Integer> incidentNodes = graph.incidentNodes(E12);
    try {
      incidentNodes.add(N3);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException expected) {
    }
  }

  @Override
  @Test
  public void adjacentNodes_checkReturnedSetMutability() {
    addNode(N1);
    Set<Integer> adjacentNodes = graph.adjacentNodes(N1);
    try {
      adjacentNodes.add(N2);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.adjacentNodes(N1)).containsExactlyElementsIn(adjacentNodes);
    }
  }

  @Override
  @Test
  public void adjacentEdges_checkReturnedSetMutability() {
    addEdge(E12, N1, N2);
    Set<String> adjacentEdges = graph.adjacentEdges(E12);
    try {
      adjacentEdges.add(E23);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E23, N2, N3);
      assertThat(graph.adjacentEdges(E12)).containsExactlyElementsIn(adjacentEdges);
    }
  }

  @Override
  @Test
  public void edgesConnecting_checkReturnedSetMutability() {
    addNode(N1);
    addNode(N2);
    Set<String> edgesConnecting = graph.edgesConnecting(N1, N2);
    try {
      edgesConnecting.add(E23);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.edgesConnecting(N1, N2)).containsExactlyElementsIn(edgesConnecting);
    }
  }

  @Override
  @Test
  public void inEdges_checkReturnedSetMutability() {
    addNode(N2);
    Set<String> inEdges = graph.inEdges(N2);
    try {
      inEdges.add(E12);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.inEdges(N2)).containsExactlyElementsIn(inEdges);
    }
  }

  @Override
  @Test
  public void outEdges_checkReturnedSetMutability() {
    addNode(N1);
    Set<String> outEdges = graph.outEdges(N1);
    try {
      outEdges.add(E12);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.outEdges(N1)).containsExactlyElementsIn(outEdges);
    }
  }

  @Override
  @Test
  public void predecessors_checkReturnedSetMutability() {
    addNode(N2);
    Set<Integer> predecessors = graph.predecessors(N2);
    try {
      predecessors.add(N1);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(graph.predecessors(N2)).containsExactlyElementsIn(predecessors);
    }
  }

  @Override
  @Test
  public void successors_checkReturnedSetMutability() {
    addNode(N1);
    Set<Integer> successors = graph.successors(N1);
    try {
      successors.add(N2);
      fail(ERROR_MODIFIABLE_SET);
    } catch (UnsupportedOperationException e) {
      addEdge(E12, N1, N2);
      assertThat(successors).containsExactlyElementsIn(graph.successors(N1));
    }
  }

  // Element Mutation

  @Test
  public void addEdge_selfLoop() {
    try {
      addEdge(E11, N1, N1);
      fail(ERROR_ADDED_SELF_LOOP);
    } catch (IllegalArgumentException e) {
      assertThat(e.getMessage()).contains(ERROR_SELF_LOOP);
    }
  }

  /**
   * This test checks an implementation dependent feature. It tests that
   * the method {@code addEdge} will silently add the missing nodes to the graph,
   * then add the edge connecting them. We are not using the proxy methods here
   * as we want to test {@code addEdge} when the end-points are not elements
   * of the graph.
   */
  @Test
  public void addEdge_nodesNotInGraph() {
    graph.addNode(N1);
    assertTrue(graph.addEdge(E15, N1, N5));
    assertTrue(graph.addEdge(E41, N4, N1));
    assertTrue(graph.addEdge(E23, N2, N3));
    assertThat(graph.nodes()).containsExactly(N1, N5, N4, N2, N3).inOrder();
    assertThat(graph.edges()).containsExactly(E15, E41, E23).inOrder();
    assertThat(graph.edgesConnecting(N1, N5)).containsExactly(E15);
    assertThat(graph.edgesConnecting(N4, N1)).containsExactly(E41);
    assertThat(graph.edgesConnecting(N2, N3)).containsExactly(E23);
    // Direction of the added edge is correctly handled
    assertThat(graph.edgesConnecting(N3, N2)).isEmpty();
  }
}
