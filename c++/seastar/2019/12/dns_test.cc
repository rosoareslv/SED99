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
 * Copyright (C) 2016 ScyllaDB.
 */
#include <vector>
#include <algorithm>

#include <seastar/core/do_with.hh>
#include <seastar/testing/test_case.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future-util.hh>
#include <seastar/net/dns.hh>
#include <seastar/net/inet_address.hh>

using namespace seastar;
using namespace seastar::net;

static const inet_address google_addr = inet_address("216.58.201.164");
static const sstring google_name = "www.google.com";

static future<> test_resolve(dns_resolver::options opts) {
    auto d = ::make_lw_shared<dns_resolver>(std::move(opts));
    return d->get_host_by_name(google_name, inet_address::family::INET).then([d](hostent e) {
        //BOOST_REQUIRE(std::count(e.addr_list.begin(), e.addr_list.end(), google_addr));
        return d->get_host_by_addr(e.addr_list.front()).then([d, a = e.addr_list.front()](hostent e) {
            return d->get_host_by_name(e.names.front(), inet_address::family::INET).then([a](hostent e) {
                BOOST_REQUIRE(std::count(e.addr_list.begin(), e.addr_list.end(), a));
            });
        });
    }).finally([d]{
        return d->close();
    });
}

static future<> test_bad_name(dns_resolver::options opts) {
    auto d = ::make_lw_shared<dns_resolver>(std::move(opts));
    return d->get_host_by_name("apa.ninja.gnu", inet_address::family::INET).then_wrapped([d](future<hostent> f) {
        try {
            f.get();
            BOOST_FAIL("should not succeed");
        } catch (...) {
            // ok.
        }
    }).finally([d]{
        return d->close();
    });
}

SEASTAR_TEST_CASE(test_resolve_udp) {
    return test_resolve(dns_resolver::options());
}

SEASTAR_TEST_CASE(test_bad_name_udp) {
    return test_bad_name(dns_resolver::options());
}

SEASTAR_TEST_CASE(test_timeout_udp) {
    dns_resolver::options opts;
    opts.servers = std::vector<inet_address>({ inet_address("1.2.3.4") }); // not a server
    opts.udp_port = 29953; // not a dns port
    opts.timeout = std::chrono::milliseconds(500);

    auto d = ::make_lw_shared<dns_resolver>(engine().net(), opts);
    return d->get_host_by_name(google_name, inet_address::family::INET).then_wrapped([d](future<hostent> f) {
        try {
            f.get();
            BOOST_FAIL("should not succeed");
        } catch (...) {
            // ok.
        }
    }).finally([d]{
        return d->close();
    });
}

// Currently failing, disable until fixed (#521)
#if 0
SEASTAR_TEST_CASE(test_resolve_tcp) {
    dns_resolver::options opts;
    opts.use_tcp_query = true;
    return test_resolve(opts);
}
#endif

SEASTAR_TEST_CASE(test_bad_name_tcp) {
    dns_resolver::options opts;
    opts.use_tcp_query = true;
    return test_bad_name(opts);
}

static const sstring imaps_service = "imaps";
static const sstring gmail_domain = "gmail.com";

static future<> test_srv() {
    auto d = ::make_lw_shared<dns_resolver>();
    return d->get_srv_records(dns_resolver::srv_proto::tcp,
                              imaps_service,
                              gmail_domain).then([d](dns_resolver::srv_records records) {
        BOOST_REQUIRE(!records.empty());
        for (auto& record : records) {
            // record.target should end with "gmail.com"
            BOOST_REQUIRE_GT(record.target.size(), gmail_domain.size());
            BOOST_REQUIRE_EQUAL(record.target.compare(record.target.size() - gmail_domain.size(),
                                                      gmail_domain.size(),
                                                      gmail_domain),
                                0);
        }
    }).finally([d]{
        return d->close();
    });
}

SEASTAR_TEST_CASE(test_srv_tcp) {
    return test_srv();
}
