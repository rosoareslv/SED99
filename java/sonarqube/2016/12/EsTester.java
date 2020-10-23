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
package org.sonar.server.es;

import com.google.common.base.Function;
import com.google.common.base.Throwables;
import com.google.common.collect.Collections2;
import com.google.common.collect.FluentIterable;
import com.google.common.collect.Iterables;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import javax.annotation.Nonnull;
import org.apache.commons.lang.math.RandomUtils;
import org.apache.commons.lang.reflect.ConstructorUtils;
import org.elasticsearch.action.admin.indices.delete.DeleteIndexResponse;
import org.elasticsearch.action.bulk.BulkRequestBuilder;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.action.search.SearchRequestBuilder;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.action.search.SearchType;
import org.elasticsearch.cluster.metadata.IndexMetaData;
import org.elasticsearch.cluster.node.DiscoveryNode;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.index.query.QueryBuilders;
import org.elasticsearch.node.Node;
import org.elasticsearch.node.NodeBuilder;
import org.elasticsearch.search.SearchHit;
import org.junit.rules.ExternalResource;
import org.sonar.api.config.MapSettings;
import org.sonar.core.platform.ComponentContainer;

import static com.google.common.base.Preconditions.checkState;
import static com.google.common.collect.Lists.newArrayList;
import static java.util.Arrays.asList;

public class EsTester extends ExternalResource {

  private final List<IndexDefinition> indexDefinitions;
  private EsClient client = new EsClient(NodeHolder.INSTANCE.node.client());
  private ComponentContainer container;

  public EsTester(IndexDefinition... defs) {
    this.indexDefinitions = asList(defs);
  }

  @Override
  protected void before() throws Throwable {
    truncateIndices();

    if (!indexDefinitions.isEmpty()) {
      container = new ComponentContainer();
      container.addSingleton(new MapSettings());
      container.addSingletons(indexDefinitions);
      container.addSingleton(client);
      container.addSingleton(IndexDefinitions.class);
      container.addSingleton(IndexCreator.class);
      container.startComponents();
    }
  }

  @Override
  protected void after() {
    if (container != null) {
      container.stopComponents();
    }
    if (client != null) {
      client.close();
    }
  }

  private void truncateIndices() {
    client.nativeClient().admin().indices().prepareDelete("_all").get();
  }

  public void putDocuments(String index, String type, BaseDoc... docs) throws Exception {
    BulkRequestBuilder bulk = client.prepareBulk().setRefresh(true);
    for (BaseDoc doc : docs) {
      bulk.add(new IndexRequest(index, type, doc.getId())
        .parent(doc.getParent())
        .routing(doc.getRouting())
        .source(doc.getFields()));
    }
    EsUtils.executeBulkRequest(bulk, "");
  }

  public long countDocuments(String indexName, String typeName) {
    return client().prepareSearch(indexName).setTypes(typeName).setSize(0).get().getHits().totalHits();
  }

  /**
   * Get all the indexed documents (no paginated results). Results are converted to BaseDoc objects.
   * Results are not sorted.
   */
  public <E extends BaseDoc> List<E> getDocuments(String indexName, String typeName, final Class<E> docClass) {
    List<SearchHit> hits = getDocuments(indexName, typeName);
    return newArrayList(Collections2.transform(hits, new Function<SearchHit, E>() {
      @Override
      public E apply(SearchHit input) {
        try {
          return (E) ConstructorUtils.invokeConstructor(docClass, input.getSource());
        } catch (Exception e) {
          throw Throwables.propagate(e);
        }
      }
    }));
  }

  /**
   * Get all the indexed documents (no paginated results). Results are not sorted.
   */
  public List<SearchHit> getDocuments(String indexName, String typeName) {
    SearchRequestBuilder req = client.nativeClient().prepareSearch(indexName).setTypes(typeName).setQuery(QueryBuilders.matchAllQuery());
    req.setSearchType(SearchType.SCAN)
      .setScroll(new TimeValue(60000))
      .setSize(100);

    SearchResponse response = req.get();
    List<SearchHit> result = newArrayList();
    while (true) {
      Iterables.addAll(result, response.getHits());
      response = client.nativeClient().prepareSearchScroll(response.getScrollId()).setScroll(new TimeValue(600000)).execute().actionGet();
      // Break condition: No hits are returned
      if (response.getHits().getHits().length == 0) {
        break;
      }
    }
    return result;
  }

  /**
   * Get a list of a specific field from all indexed documents.
   */
  public <T> List<T> getDocumentFieldValues(String indexName, String typeName, final String fieldNameToReturn) {
    return newArrayList(Iterables.transform(getDocuments(indexName, typeName), new Function<SearchHit, T>() {
      @Override
      public T apply(SearchHit input) {
        return (T) input.sourceAsMap().get(fieldNameToReturn);
      }
    }));
  }

  public List<String> getIds(String indexName, String typeName) {
    return FluentIterable.from(getDocuments(indexName, typeName)).transform(SearchHitToId.INSTANCE).toList();
  }

  public EsClient client() {
    return client;
  }

  private enum SearchHitToId implements Function<SearchHit, String> {
    INSTANCE;

    @Override
    public String apply(@Nonnull org.elasticsearch.search.SearchHit input) {
      return input.id();
    }
  }

  private static class NodeHolder {
    private static final NodeHolder INSTANCE = new NodeHolder();

    private final Node node;

    private NodeHolder() {
      try {
        String nodeName = "tmp-es-" + RandomUtils.nextInt();
        Path tmpDir = Files.createTempDirectory("tmp-es");
        tmpDir.toFile().deleteOnExit();

        node = NodeBuilder.nodeBuilder().local(true).data(true).settings(org.elasticsearch.common.settings.Settings.builder()
          .put("cluster.name", nodeName)
          .put("node.name", nodeName)
          // the two following properties are probably not used because they are
          // declared on indices too
          .put(IndexMetaData.SETTING_NUMBER_OF_SHARDS, 1)
          .put(IndexMetaData.SETTING_NUMBER_OF_REPLICAS, 0)
          // limit the number of threads created (see org.elasticsearch.common.util.concurrent.EsExecutors)
          .put("processors", 1)
          .put("http.enabled", false)
          .put("config.ignore_system_properties", true)
          .put("path.home", tmpDir))
          .build();
        node.start();
        checkState(DiscoveryNode.localNode(node.settings()));
        checkState(!node.isClosed());

        // wait for node to be ready
        node.client().admin().cluster().prepareHealth().setWaitForGreenStatus().get();

        // delete the indices (should not exist)
        DeleteIndexResponse response = node.client().admin().indices().prepareDelete("_all").get();
        checkState(response.isAcknowledged());
      } catch (IOException e) {
        throw Throwables.propagate(e);
      }
    }
  }
}
