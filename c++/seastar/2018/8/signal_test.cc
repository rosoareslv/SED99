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
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 */

#include "core/sleep.hh"
#include "core/do_with.hh"
#include "test-utils.hh"

using namespace seastar;
using namespace std::chrono_literals;

SEASTAR_TEST_CASE(test_sighup) {
    return do_with(false, [](bool& signaled) { 
        engine().handle_signal(SIGHUP, [&] { signaled = true; });
        return seastar::sleep(10ms).then([&] {
            kill(0, SIGHUP);
            return seastar::sleep(10ms).then([&] {
                BOOST_REQUIRE_EQUAL(signaled, true);
                return make_ready_future<>();
            });
        });
    });
} 
