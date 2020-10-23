/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package org.elasticsearch.search.aggregations.metrics.percentiles;

import org.elasticsearch.common.ParseField;
import org.elasticsearch.search.aggregations.support.ValuesSource.Numeric;
import org.elasticsearch.search.aggregations.support.ValuesSourceAggregationBuilder;

/**
 *
 */
public class PercentileRanksParser extends AbstractPercentilesParser {

    public static final ParseField VALUES_FIELD = new ParseField("values");

    public PercentileRanksParser() {
        super(false);
    }

    @Override
    protected ParseField keysField() {
        return VALUES_FIELD;
    }

    @Override
    protected ValuesSourceAggregationBuilder<Numeric, ?> buildFactory(String aggregationName, double[] keys, PercentilesMethod method,
                                                                      Double compression, Integer numberOfSignificantValueDigits,
                                                                      Boolean keyed) {
        PercentileRanksAggregationBuilder factory = new PercentileRanksAggregationBuilder(aggregationName);
        if (keys != null) {
            factory.values(keys);
        }
        if (method != null) {
            factory.method(method);
        }
        if (compression != null) {
            factory.compression(compression);
        }
        if (numberOfSignificantValueDigits != null) {
            factory.numberOfSignificantValueDigits(numberOfSignificantValueDigits);
        }
        if (keyed != null) {
            factory.keyed(keyed);
        }
        return factory;
    }
}
