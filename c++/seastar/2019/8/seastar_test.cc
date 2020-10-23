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
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#include <thread>

#include <seastar/testing/entry_point.hh>
#include <seastar/testing/seastar_test.hh>
#include <seastar/testing/test_runner.hh>
#include <seastar/core/future.hh>
#include <seastar/core/app-template.hh>

namespace seastar {

namespace testing {

void seastar_test::run() {
    // HACK: please see https://github.com/cloudius-systems/seastar/issues/10
    BOOST_REQUIRE(true);

    // HACK: please see https://github.com/cloudius-systems/seastar/issues/10
    boost::program_options::variables_map()["dummy"];

    global_test_runner().run_sync([this] {
        return run_test_case();
    });
}

// We store a pointer because tests are registered from dynamic initializers,
// so we must ensure that 'tests' is initialized before any dynamic initializer.
// I use a primitive type, which is guaranteed to be initialized before any
// dynamic initializer and lazily allocate the factor.

static std::vector<seastar_test*>* tests = nullptr;

const std::vector<seastar_test*>& known_tests() {
    if (!tests) {
        throw std::runtime_error("No tests registered");
    }
    return *tests;
}

seastar_test::seastar_test() {
    if (!tests) {
        tests = new std::vector<seastar_test*>();
    }
    tests->push_back(this);
}

}

}
