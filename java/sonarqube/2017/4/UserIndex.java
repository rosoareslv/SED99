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
package org.sonar.server.user.index;

import com.google.common.base.Function;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import javax.annotation.CheckForNull;
import org.apache.commons.lang.StringUtils;
import org.elasticsearch.action.get.GetRequestBuilder;
import org.elasticsearch.action.get.GetResponse;
import org.elasticsearch.action.search.SearchRequestBuilder;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.action.search.SearchType;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.index.query.BoolQueryBuilder;
import org.elasticsearch.index.query.MatchQueryBuilder;
import org.elasticsearch.index.query.QueryBuilder;
import org.elasticsearch.index.query.QueryBuilders;
import org.elasticsearch.search.SearchHit;
import org.elasticsearch.search.sort.SortBuilders;
import org.elasticsearch.search.sort.SortOrder;
import org.sonar.api.ce.ComputeEngineSide;
import org.sonar.api.server.ServerSide;
import org.sonar.core.util.NonNullInputFunction;
import org.sonar.server.es.EsClient;
import org.sonar.server.es.EsUtils;
import org.sonar.server.es.SearchOptions;
import org.sonar.server.es.SearchResult;

import static org.elasticsearch.index.query.QueryBuilders.boolQuery;
import static org.elasticsearch.index.query.QueryBuilders.matchAllQuery;
import static org.elasticsearch.index.query.QueryBuilders.termQuery;
import static org.elasticsearch.index.query.QueryBuilders.termsQuery;
import static org.sonar.server.es.DefaultIndexSettingsElement.USER_SEARCH_GRAMS_ANALYZER;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_ACTIVE;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_EMAIL;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_LOGIN;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_NAME;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_ORGANIZATION_UUIDS;
import static org.sonar.server.user.index.UserIndexDefinition.FIELD_SCM_ACCOUNTS;

@ServerSide
@ComputeEngineSide
public class UserIndex {

  /**
   * Convert an Elasticsearch result (a map) to an {@link UserDoc}. It's
   * used for {@link org.sonar.server.es.SearchResult}.
   */
  private static final Function<Map<String, Object>, UserDoc> DOC_CONVERTER = new NonNullInputFunction<Map<String, Object>, UserDoc>() {
    @Override
    protected UserDoc doApply(Map<String, Object> input) {
      return new UserDoc(input);
    }
  };

  private final EsClient esClient;

  public UserIndex(EsClient esClient) {
    this.esClient = esClient;
  }

  @CheckForNull
  public UserDoc getNullableByLogin(String login) {
    GetRequestBuilder request = esClient.prepareGet(UserIndexDefinition.INDEX_TYPE_USER, login)
      .setFetchSource(true)
      .setRouting(login);
    GetResponse response = request.get();
    if (response.isExists()) {
      return DOC_CONVERTER.apply(response.getSource());
    }
    return null;
  }

  /**
   * Returns the active users (at most 3) who are associated to the given SCM account. This method can be used
   * to detect user conflicts.
   */
  public List<UserDoc> getAtMostThreeActiveUsersForScmAccount(String scmAccount) {
    List<UserDoc> result = new ArrayList<>();
    if (!StringUtils.isEmpty(scmAccount)) {
      SearchRequestBuilder request = esClient.prepareSearch(UserIndexDefinition.INDEX_TYPE_USER)
        .setQuery(boolQuery().must(matchAllQuery()).filter(
          boolQuery()
            .must(termQuery(FIELD_ACTIVE, true))
            .should(termQuery(FIELD_LOGIN, scmAccount))
            .should(termQuery(FIELD_EMAIL, scmAccount))
            .should(termQuery(FIELD_SCM_ACCOUNTS, scmAccount))))
        .setSize(3);
      for (SearchHit hit : request.get().getHits().getHits()) {
        result.add(DOC_CONVERTER.apply(hit.sourceAsMap()));
      }
    }
    return result;
  }

  public Iterator<UserDoc> selectUsersForBatch(List<String> logins) {
    BoolQueryBuilder filter = boolQuery()
      .filter(termsQuery(FIELD_LOGIN, logins));

    SearchRequestBuilder requestBuilder = esClient
      .prepareSearch(UserIndexDefinition.INDEX_TYPE_USER)
      .setSearchType(SearchType.SCAN)
      .addSort(SortBuilders.fieldSort(FIELD_LOGIN).order(SortOrder.ASC))
      .setScroll(TimeValue.timeValueMinutes(EsUtils.SCROLL_TIME_IN_MINUTES))
      .setSize(10_000)
      .setFetchSource(
        new String[] {FIELD_LOGIN, FIELD_NAME},
        null)
      .setQuery(filter);
    SearchResponse response = requestBuilder.get();

    return EsUtils.scroll(esClient, response.getScrollId(), DOC_CONVERTER);
  }

  public SearchResult<UserDoc> search(UserQuery userQuery, SearchOptions options) {
    SearchRequestBuilder request = esClient.prepareSearch(UserIndexDefinition.INDEX_TYPE_USER)
      .setSize(options.getLimit())
      .setFrom(options.getOffset())
      .addSort(FIELD_NAME, SortOrder.ASC);

    BoolQueryBuilder filter = boolQuery().must(termQuery(FIELD_ACTIVE, true));
    userQuery.getOrganizationUuid()
      .ifPresent(o -> filter.must(termQuery(FIELD_ORGANIZATION_UUIDS, o)));
    userQuery.getExcludedOrganizationUuid()
      .ifPresent(o -> filter.mustNot(termQuery(FIELD_ORGANIZATION_UUIDS, o)));

    QueryBuilder esQuery = matchAllQuery();
    Optional<String> textQuery = userQuery.getTextQuery();
    if (textQuery.isPresent()) {
      esQuery = QueryBuilders.multiMatchQuery(textQuery.get(),
        FIELD_LOGIN,
        USER_SEARCH_GRAMS_ANALYZER.subField(FIELD_LOGIN),
        FIELD_NAME,
        USER_SEARCH_GRAMS_ANALYZER.subField(FIELD_NAME),
        FIELD_EMAIL,
        USER_SEARCH_GRAMS_ANALYZER.subField(FIELD_EMAIL))
        .operator(MatchQueryBuilder.Operator.AND);
    }

    request.setQuery(boolQuery().must(esQuery).filter(filter));

    return new SearchResult<>(request.get(), DOC_CONVERTER);
  }

}
