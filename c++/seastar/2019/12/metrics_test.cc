/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (C) 2019 ScyllaDB.
 */

#include <seastar/core/metrics_registration.hh>
#include <seastar/core/metrics.hh>
#include <seastar/core/metrics_api.hh>
#include <seastar/core/scheduling.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/io_queue.hh>
#include <seastar/testing/test_case.hh>
#include <seastar/testing/thread_test_case.hh>
#include <seastar/testing/test_runner.hh>
#include <boost/range/irange.hpp>

SEASTAR_TEST_CASE(test_add_group) {
    using namespace seastar::metrics;
    // Just has to compile:
    metric_groups()
        .add_group("g1", {})
        .add_group("g2", std::vector<metric_definition>());
    return seastar::make_ready_future();
}

/**
 * This function return the different name label values
 *  for the named metric.
 *
 *  @note: If the statistic or label doesn't exist, the test
 *  that calls this function will fail.
 *
 * @param metric_name - the metric name
 * @param label_name - the label name
 * @return a set containing all the different values
 *         of the label.
 */
static std::set<seastar::sstring> get_label_values(seastar::sstring metric_name, seastar::sstring label_name) {
    namespace smi = seastar::metrics::impl;
    auto all_metrics = smi::get_values();
    const auto& all_metadata = *all_metrics->metadata;
    const auto qp_group = find_if(cbegin(all_metadata), cend(all_metadata),
        [&metric_name] (const auto& x) { return x.mf.name == metric_name; });
    BOOST_REQUIRE(qp_group != cend(all_metadata));
    std::set<seastar::sstring> labels;
    for (const auto& metric : qp_group->metrics) {
        const auto found = metric.id.labels().find(label_name);
        BOOST_REQUIRE(found != metric.id.labels().cend());
        labels.insert(found->second);
    }
    return labels;
}

SEASTAR_THREAD_TEST_CASE(test_renaming_scheuling_groups) {
    // this seams a little bit out of place but the
    // renaming functionality is primarily for statistics
    // otherwise those classes could have just been reused
    // without renaming them.
    using namespace seastar;

    static const char* name1 = "A";
    static const char* name2 = "B";
    scheduling_group sg =  create_scheduling_group("hello", 111).get0();
    boost::integer_range<int> rng(0, 1000);
    // repeatedly change the group name back and forth in
    // decresing time intervals to see if it generate double
    //registration statistics errors.
    for (auto&& i : rng) {
        const char* name = i%2 ? name1 : name2;
        const char* prev_name = i%2 ? name2 : name1;
        sleep(std::chrono::microseconds(100000/(i+1))).get();
        rename_scheduling_group(sg, name).get();
        std::set<sstring> label_vals = get_label_values(sstring("scheduler_shares"), sstring("group"));
        // validate that the name that we *renamed to* is in the stats
        BOOST_REQUIRE(label_vals.find(sstring(name)) != label_vals.end());
        // validate that the name that we *renamed from* is *not* in the stats
        BOOST_REQUIRE(label_vals.find(sstring(prev_name)) == label_vals.end());
    }

    smp::invoke_on_all([sg] () {
        return do_with(std::uniform_int_distribution<int>(), boost::irange<int>(0, 1000),
                [sg] (std::uniform_int_distribution<int>& dist, boost::integer_range<int>& rng) {
            // flip a fair coin and rename to one of two options and rename to that
            // scheduling group name, do it 1000 in parallel on all shards so there
            // is a chance of collision.
            return do_for_each(rng, [sg, &dist] (auto i) {
                bool odd = dist(seastar::testing::local_random_engine)%2;
                return rename_scheduling_group(sg, odd ? name1 : name2);
            });
        });
    }).get();

    std::set<sstring> label_vals = get_label_values(sstring("scheduler_shares"), sstring("group"));
    // validate that only one of the names is eventually in the metrics
    bool name1_found = label_vals.find(sstring(name1)) != label_vals.end();
    bool name2_found = label_vals.find(sstring(name2)) != label_vals.end();
    BOOST_REQUIRE((name1_found && !name2_found) || (name2_found && !name1_found));
}

SEASTAR_THREAD_TEST_CASE(test_renaming_io_priority_classes) {
    // this seams a little bit out of place but the
    // renaming functionality is primarily for statistics
    // otherwise those classes could have just been reused
    // without renaming them.
    using namespace seastar;
    static const char* name1 = "A";
    static const char* name2 = "B";
    seastar::io_priority_class pc = engine().register_one_priority_class("hello",100);
    smp::invoke_on_all([pc] () {
        // this is a trick to get all of the queues actually register their
        // stats.
        return engine().update_shares_for_class(pc,101);
    }).get();

    boost::integer_range<int> rng(0, 1000);
    // repeatedly change the group name back and forth in
    // decresing time intervals to see if it generate double
    //registration statistics errors.
    for (auto&& i : rng) {
        const char* name = i%2 ? name1 : name2;
        const char* prev_name = i%2 ? name2 : name1;
        sleep(std::chrono::microseconds(100000/(i+1))).get();
        rename_priority_class(pc, name).get();
        std::set<sstring> label_vals = get_label_values(sstring("io_queue_shares"), sstring("class"));
        // validate that the name that we *renamed to* is in the stats
        BOOST_REQUIRE(label_vals.find(sstring(name)) != label_vals.end());
        // validate that the name that we *renamed from* is *not* in the stats
        BOOST_REQUIRE(label_vals.find(sstring(prev_name)) == label_vals.end());
    }

    smp::invoke_on_all([pc] () {
        return do_with(std::uniform_int_distribution<int>(), boost::irange<int>(0, 1000),
                [pc] (std::uniform_int_distribution<int>& dist, boost::integer_range<int>& rng) {
            // flip a fair coin and rename to one of two options and rename to that
            // scheduling group name, do it 1000 in parallel on all shards so there
            // is a chance of collision.
            return do_for_each(rng, [pc, &dist] (auto i) {
                bool odd = dist(seastar::testing::local_random_engine)%2;
                return rename_priority_class(pc, odd ? name1 : name2);
            });
        });
    }).get();

    std::set<sstring> label_vals = get_label_values(sstring("io_queue_shares"), sstring("class"));
    // validate that only one of the names is eventually in the metrics
    bool name1_found = label_vals.find(sstring(name1)) != label_vals.end();
    bool name2_found = label_vals.find(sstring(name2)) != label_vals.end();
    BOOST_REQUIRE((name1_found && !name2_found) || (name2_found && !name1_found));
}
