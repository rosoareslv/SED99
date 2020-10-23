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
 * Copyright (C) 2016 ScyllaDB
 */

#include <seastar/core/thread.hh>
#include <seastar/core/do_with.hh>
#include <seastar/testing/test_case.hh>
#include <seastar/testing/thread_test_case.hh>
#include <seastar/testing/test_runner.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/fair_queue.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/print.hh>
#include <boost/range/irange.hpp>
#include <chrono>

using namespace seastar;
using namespace std::chrono_literals;

fair_queue::config make_config(unsigned capacity) {
    fair_queue::config cfg;
    cfg.capacity = capacity;
    cfg.max_req_count = capacity;
    return cfg;
}

struct request {
    fair_queue_request_descriptor fqdesc;
    unsigned index;

    request(unsigned weight, unsigned index)
        : fqdesc({weight, 0})
        , index(index)
    {}
};


class test_env {
    fair_queue _fq;
    std::vector<int> _results;
    std::vector<std::vector<std::exception_ptr>> _exceptions;
    std::vector<priority_class_ptr> _classes;
    std::vector<request> _inflight;

    void drain() {
        do {} while (tick() != 0);
    }
public:
    test_env(unsigned capacity) : _fq(capacity)
    {}

    // As long as there is a request sitting in the queue, tick() will process
    // at least one request. The only situation in which tick() will return nothing
    // is if no requests were sent to the fair_queue (obviously).
    //
    // Because of this property, one useful use of tick() is to implement a drain()
    // method (see above) in which all requests currently sent to the queue are drained
    // before the queue is destroyed.
    unsigned tick(unsigned n = 1) {
        unsigned processed = 0;
        _fq.dispatch_requests();

        for (unsigned i = 0; i < n; ++i) {
            std::vector<request> curr;
            curr.swap(_inflight);

            for (auto& req : curr) {
                processed++;
                _results[req.index]++;
                _fq.notify_requests_finished(req.fqdesc);
            }

            _fq.dispatch_requests();
        }
        return processed;
    }

    ~test_env() {
        drain();
        for (auto& p: _classes) {
            _fq.unregister_priority_class(p);
        }
    }

    size_t register_priority_class(uint32_t shares) {
        _results.push_back(0);
        _exceptions.push_back(std::vector<std::exception_ptr>());
        _classes.push_back(_fq.register_priority_class(shares));
        return _classes.size() - 1;
    }

    void do_op(unsigned index, unsigned weight) {
        auto cl = _classes[index];
        auto req = request(weight, index);

        _fq.queue(cl, req.fqdesc, [this, index, req] () mutable noexcept {
            try {
                _inflight.push_back(std::move(req));
            } catch (...) {
                auto eptr = std::current_exception();
                _exceptions[index].push_back(eptr);
                _fq.notify_requests_finished(req.fqdesc);
            }
        });
    }

    void update_shares(unsigned index, uint32_t shares) {
        auto cl = _classes[index];
        _fq.update_shares(cl, shares);
    }

    void reset_results(unsigned index) {
        _results[index] = 0;
    }

    // Verify if the ratios are what we expect. Because we can't be sure about
    // precise timing issues, we can always be off by some percentage. In simpler
    // tests we really expect it to very low, but in more complex tests, with share
    // changes, for instance, they can accumulate
    //
    // The ratios argument is the ratios towards the first class
    void verify(sstring name, std::vector<unsigned> ratios, unsigned expected_error = 1) {
        assert(ratios.size() == _results.size());
        auto str = name + ":";
        for (auto i = 0ul; i < _results.size(); ++i) {
            str += format(" r[{:d}] = {:d}", i, _results[i]);
        }
        std::cout << str << std::endl;
        for (auto i = 0ul; i < ratios.size(); ++i) {
            int min_expected = ratios[i] * (_results[0] - expected_error);
            int max_expected = ratios[i] * (_results[0] + expected_error);
            BOOST_REQUIRE(_results[i] >= min_expected);
            BOOST_REQUIRE(_results[i] <= max_expected);
            BOOST_REQUIRE(_exceptions[i].size() == 0);
        }
    }
};

// Equal ratios. Expected equal results.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_equal_2classes) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }

    later().get();
    // allow half the requests in
    env.tick(100);
    env.verify("equal_2classes", {1, 1});
}

// Equal results, spread among 4 classes.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_equal_4classes) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);
    auto c = env.register_priority_class(10);
    auto d = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
        env.do_op(c, 1);
        env.do_op(d, 1);
    }
    later().get();
    // allow half the requests in
    env.tick(200);
    env.verify("equal_4classes", {1, 1, 1, 1});
}

// Class2 twice as powerful. Expected class2 to have 2 x more requests.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_different_shares) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(20);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    later().get();
    // allow half the requests in
    env.tick(100);
    return env.verify("different_shares", {1, 2});
}

// Equal ratios, high capacity queue. Should still divide equally.
//
// Note that we sleep less because now more requests will be going through the
// queue.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_equal_hi_capacity_2classes) {
    test_env env(10);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    later().get();

    // queue has capacity 10, 10 x 10 = 100, allow half the requests in
    env.tick(10);
    env.verify("hi_capacity_2classes", {1, 1});
}

// Class2 twice as powerful, queue is high capacity. Still expected class2 to
// have 2 x more requests.
//
// Note that we sleep less because now more requests will be going through the
// queue.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_different_shares_hi_capacity) {
    test_env env(10);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(20);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    later().get();
    // queue has capacity 10, 10 x 10 = 100, allow half the requests in
    env.tick(10);
    env.verify("different_shares_hi_capacity", {1, 2});
}

// Classes equally powerful. But Class1 issues twice as expensive requests. Expected Class2 to have 2 x more requests.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_different_weights) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 2);
        env.do_op(b, 1);
    }
    later().get();
    // allow half the requests in
    env.tick(100);
    env.verify("different_weights", {1, 2});
}

// Class2 pushes many requests over. Right after, don't expect Class2 to be able to push anything else.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_dominant_queue) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(b, 1);
    }
    later().get();

    // consume all requests
    env.tick(100);
    // zero statistics.
    env.reset_results(b);
    for (int i = 0; i < 20; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    // allow half the requests in
    env.tick(20);
    env.verify("dominant_queue", {1, 0});
}

// Class2 pushes many requests at first. After enough time, this shouldn't matter anymore.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_forgiving_queue) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 100; ++i) {
        env.do_op(b, 1);
    }
    later().get();

    // consume all requests
    env.tick(100);
    sleep(500ms).get();
    env.reset_results(b);
    for (int i = 0; i < 100; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    later().get();

    // allow half the requests in
    env.tick(100);
    env.verify("forgiving_queue", {1, 1});
}

// Classes push requests and then update swap their shares. In the end, should have executed
// the same number of requests.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_update_shares) {
    test_env env(1);

    auto a = env.register_priority_class(20);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 500; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }

    later().get();
    // allow 25% of the requests in
    env.tick(250);
    env.update_shares(a, 10);
    env.update_shares(b, 20);

    later().get();
    // allow 25% of the requests in
    env.tick(250);
    env.verify("update_shares", {1, 1}, 2);
}

// Classes run for a longer period of time. Balance must be kept over many timer
// periods.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_longer_run) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(10);

    for (int i = 0; i < 20000; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }
    // In total allow half the requests in, but do it over a
    // long period of time, ticking slowly
    for (int i = 0; i < 1000; ++i) {
        sleep(1ms).get();
        env.tick(2);
    }
    env.verify("longer_run", {1, 1}, 2);
}

// Classes run for a longer period of time. Proportional balance must be kept over many timer
// periods, despite unequal shares..
SEASTAR_THREAD_TEST_CASE(test_fair_queue_longer_run_different_shares) {
    test_env env(1);

    auto a = env.register_priority_class(10);
    auto b = env.register_priority_class(20);

    for (int i = 0; i < 20000; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }

    // In total allow half the requests in, but do it over a
    // long period of time, ticking slowly
    for (int i = 0; i < 1000; ++i) {
        sleep(1ms).get();
        env.tick(2);
    }
    env.verify("longer_run_different_shares", {1, 2}, 2);
}

// Classes run for a random period of time. Equal operations expected.
SEASTAR_THREAD_TEST_CASE(test_fair_queue_random_run) {
    test_env env(1);

    auto a = env.register_priority_class(1);
    auto b = env.register_priority_class(1);

    std::default_random_engine& generator = testing::local_random_engine;
    // multiples of 100usec - which is the approximate length of the request. We will
    // put a minimum of 10. Below that, it is hard to guarantee anything. The maximum is
    // about 50 seconds.
    std::uniform_int_distribution<uint32_t> distribution(10, 500 * 1000);
    auto reqs = distribution(generator);

    // Enough requests for the maximum run (half per queue, + leeway)
    for (uint32_t i = 0; i < reqs; ++i) {
        env.do_op(a, 1);
        env.do_op(b, 1);
    }

    later().get();
    // In total allow half the requests in
    env.tick(reqs);

    // Accept 5 % error.
    auto expected_error = std::max(1, int(round(reqs * 0.05)));
    env.verify(format("random_run ({:d} requests)", reqs), {1, 1}, expected_error);
}
