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
package org.sonar.server.rule.index;

import com.google.common.base.Function;
import com.google.common.base.Joiner;
import com.google.common.base.Predicate;
import com.google.common.collect.Collections2;
import com.google.common.collect.ImmutableList;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import org.apache.commons.lang.StringUtils;
import org.elasticsearch.action.search.SearchRequestBuilder;
import org.elasticsearch.action.search.SearchResponse;
import org.elasticsearch.action.search.SearchType;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.index.query.BoolQueryBuilder;
import org.elasticsearch.index.query.HasParentQueryBuilder;
import org.elasticsearch.index.query.MatchQueryBuilder;
import org.elasticsearch.index.query.QueryBuilder;
import org.elasticsearch.index.query.QueryBuilders;
import org.elasticsearch.index.query.SimpleQueryStringBuilder;
import org.elasticsearch.search.aggregations.AggregationBuilder;
import org.elasticsearch.search.aggregations.AggregationBuilders;
import org.elasticsearch.search.aggregations.bucket.terms.Terms;
import org.elasticsearch.search.aggregations.bucket.terms.TermsBuilder;
import org.elasticsearch.search.sort.FieldSortBuilder;
import org.elasticsearch.search.sort.SortBuilders;
import org.elasticsearch.search.sort.SortOrder;
import org.sonar.api.rule.RuleKey;
import org.sonar.api.rule.RuleStatus;
import org.sonar.api.rule.Severity;
import org.sonar.api.rules.RuleType;
import org.sonar.server.es.BaseIndex;
import org.sonar.server.es.EsClient;
import org.sonar.server.es.SearchIdResult;
import org.sonar.server.es.SearchOptions;
import org.sonar.server.es.StickyFacetBuilder;

import static org.elasticsearch.index.query.QueryBuilders.boolQuery;
import static org.elasticsearch.index.query.QueryBuilders.matchAllQuery;
import static org.elasticsearch.index.query.QueryBuilders.matchQuery;
import static org.elasticsearch.index.query.QueryBuilders.simpleQueryStringQuery;
import static org.sonar.server.es.EsUtils.SCROLL_TIME_IN_MINUTES;
import static org.sonar.server.es.EsUtils.escapeSpecialRegexChars;
import static org.sonar.server.es.EsUtils.scrollIds;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_ACTIVE_RULE_INHERITANCE;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_ACTIVE_RULE_PROFILE_KEY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_ACTIVE_RULE_SEVERITY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_ALL_TAGS;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_CREATED_AT;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_HTML_DESCRIPTION;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_INTERNAL_KEY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_IS_TEMPLATE;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_KEY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_LANGUAGE;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_NAME;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_REPOSITORY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_RULE_KEY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_SEVERITY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_STATUS;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_TEMPLATE_KEY;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_TYPE;
import static org.sonar.server.rule.index.RuleIndexDefinition.FIELD_RULE_UPDATED_AT;
import static org.sonar.server.rule.index.RuleIndexDefinition.INDEX;
import static org.sonar.server.rule.index.RuleIndexDefinition.TYPE_ACTIVE_RULE;
import static org.sonar.server.rule.index.RuleIndexDefinition.TYPE_RULE;

/**
 * The unique entry-point to interact with Elasticsearch index "rules".
 * All the requests are listed here.
 */
public class RuleIndex extends BaseIndex {

  public static final String FACET_LANGUAGES = "languages";
  public static final String FACET_TAGS = "tags";
  public static final String FACET_REPOSITORIES = "repositories";
  public static final String FACET_SEVERITIES = "severities";
  public static final String FACET_ACTIVE_SEVERITIES = "active_severities";
  public static final String FACET_STATUSES = "statuses";
  public static final String FACET_TYPES = "types";
  public static final String FACET_OLD_DEFAULT = "true";

  public static final List<String> ALL_STATUSES_EXCEPT_REMOVED = ImmutableList.copyOf(
    Collections2.filter(Collections2.transform(Arrays.asList(RuleStatus.values()), RuleStatusToString.INSTANCE), NotRemoved.INSTANCE));

  public RuleIndex(EsClient client) {
    super(client);
  }

  public SearchIdResult<RuleKey> search(RuleQuery query, SearchOptions options) {
    SearchRequestBuilder esSearch = getClient()
      .prepareSearch(INDEX)
      .setTypes(TYPE_RULE);

    QueryBuilder qb = buildQuery(query);
    Map<String, QueryBuilder> filters = buildFilters(query);

    if (!options.getFacets().isEmpty()) {
      for (AggregationBuilder aggregation : getFacets(query, options, qb, filters).values()) {
        esSearch.addAggregation(aggregation);
      }
    }

    setSorting(query, esSearch);
    setPagination(options, esSearch);

    BoolQueryBuilder fb = boolQuery();
    for (QueryBuilder filterBuilder : filters.values()) {
      fb.must(filterBuilder);
    }

    esSearch.setQuery(boolQuery().must(qb).filter(fb));
    return new SearchIdResult<>(esSearch.get(), ToRuleKey.INSTANCE);
  }

  /**
   * Return all keys matching the search query, without pagination nor facets
   */
  public Iterator<RuleKey> searchAll(RuleQuery query) {
    SearchRequestBuilder esSearch = getClient()
      .prepareSearch(INDEX)
      .setTypes(TYPE_RULE)
      .setSearchType(SearchType.SCAN)
      .setScroll(TimeValue.timeValueMinutes(SCROLL_TIME_IN_MINUTES));

    QueryBuilder qb = buildQuery(query);
    Map<String, QueryBuilder> filters = buildFilters(query);
    setSorting(query, esSearch);

    BoolQueryBuilder fb = boolQuery();
    for (QueryBuilder filterBuilder : filters.values()) {
      fb.must(filterBuilder);
    }

    esSearch.setQuery(boolQuery().must(qb).filter(fb));
    SearchResponse response = esSearch.get();
    return scrollIds(getClient(), response.getScrollId(), ToRuleKey.INSTANCE);
  }

  /* Build main query (search based) */
  private static QueryBuilder buildQuery(RuleQuery query) {

    // No contextual query case
    String queryText = query.getQueryText();
    if (StringUtils.isEmpty(queryText)) {
      return matchAllQuery();
    }

    // Build RuleBased contextual query
    BoolQueryBuilder qb = boolQuery();
    String queryString = query.getQueryText();

    // Human readable type of querying
    qb.should(simpleQueryStringQuery(query.getQueryText())
      .field(FIELD_RULE_NAME + "." + SEARCH_WORDS_SUFFIX, 20f)
      .field(FIELD_RULE_HTML_DESCRIPTION, 3f)
      .defaultOperator(SimpleQueryStringBuilder.Operator.AND)).boost(20f);

    // Match and partial Match queries
    // Search by key uses the "sortable" sub-field as it requires to be case-insensitive (lower-case filtering)
    qb.should(matchQuery(FIELD_RULE_KEY + "." + SORT_SUFFIX, queryString).operator(MatchQueryBuilder.Operator.AND).boost(30f));
    qb.should(matchQuery(FIELD_RULE_RULE_KEY + "." + SORT_SUFFIX, queryString).operator(MatchQueryBuilder.Operator.AND).boost(15f));
    qb.should(termQuery(FIELD_RULE_LANGUAGE, queryString, 3f));
    qb.should(termQuery(FIELD_RULE_ALL_TAGS, queryString, 10f));
    qb.should(termAnyQuery(FIELD_RULE_ALL_TAGS, queryString, 1f));

    return qb;
  }

  private static QueryBuilder termQuery(String field, String query, float boost) {
    return QueryBuilders.multiMatchQuery(query,
      field, field + "." + SEARCH_PARTIAL_SUFFIX)
      .operator(MatchQueryBuilder.Operator.AND)
      .boost(boost);
  }

  private static QueryBuilder termAnyQuery(String field, String query, float boost) {
    return QueryBuilders.multiMatchQuery(query,
      field, field + "." + SEARCH_PARTIAL_SUFFIX)
      .operator(MatchQueryBuilder.Operator.OR)
      .boost(boost);
  }

  /* Build main filter (match based) */
  private static Map<String, QueryBuilder> buildFilters(RuleQuery query) {

    Map<String, QueryBuilder> filters = new HashMap<>();

    /* Add enforced filter on rules that are REMOVED */
    filters.put(FIELD_RULE_STATUS,
      boolQuery().mustNot(
        QueryBuilders.termQuery(FIELD_RULE_STATUS,
          RuleStatus.REMOVED.toString())));

    if (StringUtils.isNotEmpty(query.getInternalKey())) {
      filters.put(FIELD_RULE_INTERNAL_KEY,
        QueryBuilders.termQuery(FIELD_RULE_INTERNAL_KEY, query.getInternalKey()));
    }

    if (StringUtils.isNotEmpty(query.getRuleKey())) {
      filters.put(FIELD_RULE_RULE_KEY,
        QueryBuilders.termQuery(FIELD_RULE_RULE_KEY, query.getRuleKey()));
    }

    if (isNotEmpty(query.getLanguages())) {
      filters.put(FIELD_RULE_LANGUAGE,
        QueryBuilders.termsQuery(FIELD_RULE_LANGUAGE, query.getLanguages()));
    }

    if (isNotEmpty(query.getRepositories())) {
      filters.put(FIELD_RULE_REPOSITORY,
        QueryBuilders.termsQuery(FIELD_RULE_REPOSITORY, query.getRepositories()));
    }

    if (isNotEmpty(query.getSeverities())) {
      filters.put(FIELD_RULE_SEVERITY,
        QueryBuilders.termsQuery(FIELD_RULE_SEVERITY, query.getSeverities()));
    }

    if (StringUtils.isNotEmpty(query.getKey())) {
      filters.put(FIELD_RULE_KEY,
        QueryBuilders.termQuery(FIELD_RULE_KEY, query.getKey()));
    }

    if (isNotEmpty(query.getTags())) {
      filters.put(FIELD_RULE_ALL_TAGS,
        QueryBuilders.termsQuery(FIELD_RULE_ALL_TAGS, query.getTags()));
    }

    if (isNotEmpty(query.getTypes())) {
      filters.put(FIELD_RULE_TYPE,
        QueryBuilders.termsQuery(FIELD_RULE_TYPE, query.getTypes()));
    }

    if (query.getAvailableSinceLong() != null) {
      filters.put("availableSince", QueryBuilders.rangeQuery(FIELD_RULE_CREATED_AT)
        .gte(query.getAvailableSinceLong()));
    }

    if (isNotEmpty(query.getStatuses())) {
      Collection<String> stringStatus = new ArrayList<>();
      for (RuleStatus status : query.getStatuses()) {
        stringStatus.add(status.name());
      }
      filters.put(FIELD_RULE_STATUS,
        QueryBuilders.termsQuery(FIELD_RULE_STATUS, stringStatus));
    }

    Boolean isTemplate = query.isTemplate();
    if (isTemplate != null) {
      filters.put(FIELD_RULE_IS_TEMPLATE,
        QueryBuilders.termQuery(FIELD_RULE_IS_TEMPLATE, Boolean.toString(isTemplate)));
    }

    String template = query.templateKey();
    if (template != null) {
      filters.put(FIELD_RULE_TEMPLATE_KEY,
        QueryBuilders.termQuery(FIELD_RULE_TEMPLATE_KEY, template));
    }

    // ActiveRule Filter (profile and inheritance)
    BoolQueryBuilder childrenFilter = boolQuery();
    addTermFilter(childrenFilter, FIELD_ACTIVE_RULE_PROFILE_KEY, query.getQProfileKey());
    addTermFilter(childrenFilter, FIELD_ACTIVE_RULE_INHERITANCE, query.getInheritance());
    addTermFilter(childrenFilter, FIELD_ACTIVE_RULE_SEVERITY, query.getActiveSeverities());

    // ChildQuery
    QueryBuilder childQuery;
    if (childrenFilter.hasClauses()) {
      childQuery = childrenFilter;
    } else {
      childQuery = matchAllQuery();
    }

    /** Implementation of activation query */
    if (Boolean.TRUE.equals(query.getActivation())) {
      filters.put("activation",
        QueryBuilders.hasChildQuery(TYPE_ACTIVE_RULE,
          childQuery));
    } else if (Boolean.FALSE.equals(query.getActivation())) {
      filters.put("activation",
        boolQuery().mustNot(
          QueryBuilders.hasChildQuery(TYPE_ACTIVE_RULE,
            childQuery)));
    }

    return filters;
  }

  private static BoolQueryBuilder addTermFilter(BoolQueryBuilder filter, String field, @Nullable Collection<String> values) {
    if (isNotEmpty(values)) {
      BoolQueryBuilder valuesFilter = boolQuery();
      for (String value : values) {
        QueryBuilder valueFilter = QueryBuilders.termQuery(field, value);
        valuesFilter.should(valueFilter);
      }
      filter.must(valuesFilter);
    }
    return filter;
  }

  private static BoolQueryBuilder addTermFilter(BoolQueryBuilder filter, String field, @Nullable String value) {
    if (StringUtils.isNotEmpty(value)) {
      filter.must(QueryBuilders.termQuery(field, value));
    }
    return filter;
  }

  private static Map<String, AggregationBuilder> getFacets(RuleQuery query, SearchOptions options, QueryBuilder queryBuilder, Map<String, QueryBuilder> filters) {
    Map<String, AggregationBuilder> aggregations = new HashMap<>();
    StickyFacetBuilder stickyFacetBuilder = stickyFacetBuilder(queryBuilder, filters);

    addDefaultFacets(query, options, aggregations, stickyFacetBuilder);

    addStatusFacetIfNeeded(options, aggregations, stickyFacetBuilder);

    if (options.getFacets().contains(FACET_SEVERITIES)) {
      aggregations.put(FACET_SEVERITIES,
        stickyFacetBuilder.buildStickyFacet(FIELD_RULE_SEVERITY, FACET_SEVERITIES, Severity.ALL.toArray()));
    }

    addActiveSeverityFacetIfNeeded(query, options, aggregations, stickyFacetBuilder);
    return aggregations;
  }

  private static void addDefaultFacets(RuleQuery query, SearchOptions options, Map<String, AggregationBuilder> aggregations, StickyFacetBuilder stickyFacetBuilder) {
    if (options.getFacets().contains(FACET_LANGUAGES) || options.getFacets().contains(FACET_OLD_DEFAULT)) {
      Collection<String> languages = query.getLanguages();
      aggregations.put(FACET_LANGUAGES,
        stickyFacetBuilder.buildStickyFacet(FIELD_RULE_LANGUAGE, FACET_LANGUAGES,
          (languages == null) ? (new String[0]) : languages.toArray()));
    }
    if (options.getFacets().contains(FACET_TAGS) || options.getFacets().contains(FACET_OLD_DEFAULT)) {
      Collection<String> tags = query.getTags();
      aggregations.put(FACET_TAGS,
        stickyFacetBuilder.buildStickyFacet(FIELD_RULE_ALL_TAGS, FACET_TAGS,
          (tags == null) ? (new String[0]) : tags.toArray()));
    }
    if (options.getFacets().contains(FACET_TYPES)) {
      Collection<RuleType> types = query.getTypes();
      aggregations.put(FACET_TYPES,
        stickyFacetBuilder.buildStickyFacet(FIELD_RULE_TYPE, FACET_TYPES,
          (types == null) ? (new String[0]) : types.toArray()));
    }
    if (options.getFacets().contains("repositories") || options.getFacets().contains(FACET_OLD_DEFAULT)) {
      Collection<String> repositories = query.getRepositories();
      aggregations.put(FACET_REPOSITORIES,
        stickyFacetBuilder.buildStickyFacet(FIELD_RULE_REPOSITORY, FACET_REPOSITORIES,
          (repositories == null) ? (new String[0]) : repositories.toArray()));
    }
  }

  private static void addStatusFacetIfNeeded(SearchOptions options, Map<String, AggregationBuilder> aggregations, StickyFacetBuilder stickyFacetBuilder) {
    if (options.getFacets().contains(FACET_STATUSES)) {
      BoolQueryBuilder facetFilter = stickyFacetBuilder.getStickyFacetFilter(FIELD_RULE_STATUS);
      AggregationBuilder statuses = AggregationBuilders.filter(FACET_STATUSES + "_filter")
        .filter(facetFilter)
        .subAggregation(
          AggregationBuilders
            .terms(FACET_STATUSES)
            .field(FIELD_RULE_STATUS)
            .include(Joiner.on('|').join(ALL_STATUSES_EXCEPT_REMOVED))
            .exclude(RuleStatus.REMOVED.toString())
            .size(ALL_STATUSES_EXCEPT_REMOVED.size()));

      aggregations.put(FACET_STATUSES, AggregationBuilders.global(FACET_STATUSES).subAggregation(statuses));
    }
  }

  private static void addActiveSeverityFacetIfNeeded(RuleQuery query, SearchOptions options, Map<String, AggregationBuilder> aggregations, StickyFacetBuilder stickyFacetBuilder) {
    if (options.getFacets().contains(FACET_ACTIVE_SEVERITIES)) {
      // We are building a children aggregation on active rules
      // so the rule filter has to be used as parent filter for active rules
      // from which we remove filters that concern active rules ("activation")
      HasParentQueryBuilder ruleFilter = QueryBuilders.hasParentQuery(
        TYPE_RULE,
        stickyFacetBuilder.getStickyFacetFilter("activation"));

      // Rebuilding the active rule filter without severities
      BoolQueryBuilder childrenFilter = boolQuery();
      addTermFilter(childrenFilter, FIELD_ACTIVE_RULE_PROFILE_KEY, query.getQProfileKey());
      RuleIndex.addTermFilter(childrenFilter, FIELD_ACTIVE_RULE_INHERITANCE, query.getInheritance());
      QueryBuilder activeRuleFilter;
      if (childrenFilter.hasClauses()) {
        activeRuleFilter = childrenFilter.must(ruleFilter);
      } else {
        activeRuleFilter = ruleFilter;
      }

      AggregationBuilder activeSeverities = AggregationBuilders.children(FACET_ACTIVE_SEVERITIES + "_children")
        .childType(TYPE_ACTIVE_RULE)
        .subAggregation(AggregationBuilders.filter(FACET_ACTIVE_SEVERITIES + "_filter")
          .filter(activeRuleFilter)
          .subAggregation(
            AggregationBuilders
              .terms(FACET_ACTIVE_SEVERITIES)
              .field(FIELD_ACTIVE_RULE_SEVERITY)
              .include(Joiner.on('|').join(Severity.ALL))
              .size(Severity.ALL.size())));

      aggregations.put(FACET_ACTIVE_SEVERITIES, AggregationBuilders.global(FACET_ACTIVE_SEVERITIES).subAggregation(activeSeverities));
    }
  }

  private static StickyFacetBuilder stickyFacetBuilder(QueryBuilder query, Map<String, QueryBuilder> filters) {
    return new StickyFacetBuilder(query, filters);
  }

  private static void setSorting(RuleQuery query, SearchRequestBuilder esSearch) {
    /* integrate Query Sort */
    String queryText = query.getQueryText();
    if (query.getSortField() != null) {
      FieldSortBuilder sort = SortBuilders.fieldSort(appendSortSuffixIfNeeded(query.getSortField()));
      if (query.isAscendingSort()) {
        sort.order(SortOrder.ASC);
      } else {
        sort.order(SortOrder.DESC);
      }
      esSearch.addSort(sort);
    } else if (StringUtils.isNotEmpty(queryText)) {
      esSearch.addSort(SortBuilders.scoreSort());
    } else {
      esSearch.addSort(appendSortSuffixIfNeeded(FIELD_RULE_UPDATED_AT), SortOrder.DESC);
      // deterministic sort when exactly the same updated_at (same millisecond)
      esSearch.addSort(appendSortSuffixIfNeeded(FIELD_RULE_KEY), SortOrder.ASC);
    }
  }

  private static String appendSortSuffixIfNeeded(String field) {
    return field +
      ((field.equals(FIELD_RULE_NAME) || field.equals(FIELD_RULE_KEY))
        ? ("." + SORT_SUFFIX)
        : "");
  }

  private static void setPagination(SearchOptions options, SearchRequestBuilder esSearch) {
    esSearch.setFrom(options.getOffset());
    esSearch.setSize(options.getLimit());
  }

  public Set<String> terms(String fields) {
    return terms(fields, null, Integer.MAX_VALUE);
  }

  public Set<String> terms(String fields, @Nullable String query, int size) {
    String aggregationKey = "_ref";

    TermsBuilder termsAggregation = AggregationBuilders.terms(aggregationKey)
      .field(fields)
      .size(size)
      .minDocCount(1);
    if (query != null) {
      termsAggregation.include(".*" + escapeSpecialRegexChars(query) + ".*");
    }
    SearchRequestBuilder request = getClient()
      .prepareSearch(INDEX)
      .setQuery(matchAllQuery())
      .setSize(0)
      .addAggregation(termsAggregation);

    SearchResponse esResponse = request.get();

    Set<String> terms = new HashSet<>();
    Terms aggregation = esResponse.getAggregations().get(aggregationKey);
    if (aggregation != null) {
      aggregation.getBuckets().forEach(value -> terms.add(value.getKeyAsString()));
    }
    return terms;
  }

  private enum ToRuleKey implements Function<String, RuleKey> {
    INSTANCE;

    @Override
    public RuleKey apply(@Nonnull String input) {
      return RuleKey.parse(input);
    }

  }
  private enum RuleStatusToString implements Function<RuleStatus, String> {
    INSTANCE;

    @Override
    public String apply(@Nonnull RuleStatus input) {
      return input.toString();
    }

  }
  private enum NotRemoved implements Predicate<String> {
    INSTANCE;

    @Override
    public boolean apply(@Nonnull String input) {
      return !RuleStatus.REMOVED.toString().equals(input);
    }

  }

  private static boolean isNotEmpty(@Nullable Collection list) {
    return list != null && !list.isEmpty();
  }

}
