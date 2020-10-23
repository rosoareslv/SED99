
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/unittest/unittest.h"

#include "mongo/util/periodic_runner_impl.h"

#include "mongo/db/service_context_test_fixture.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/clock_source_mock.h"

namespace mongo {

class Client;

namespace {

class PeriodicRunnerImplTestNoSetup : public ServiceContextTest {
public:
    void setUp() override {
        _clockSource = std::make_unique<ClockSourceMock>();
        _runner = stdx::make_unique<PeriodicRunnerImpl>(getServiceContext(), _clockSource.get());
    }

    void tearDown() override {
        _runner->shutdown();
    }

    ClockSourceMock& clockSource() {
        return *_clockSource;
    }

    PeriodicRunner& runner() {
        return *_runner;
    }

private:
    std::unique_ptr<ClockSourceMock> _clockSource;
    std::unique_ptr<PeriodicRunner> _runner;
};

class PeriodicRunnerImplTest : public PeriodicRunnerImplTestNoSetup {
public:
    void setUp() override {
        PeriodicRunnerImplTestNoSetup::setUp();
        runner().startup();
    }
};

TEST_F(PeriodicRunnerImplTest, OneJobTest) {
    int count = 0;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add a job, ensure that it runs once
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    runner().scheduleJob(std::move(job));

    // Fast forward ten times, we should run all ten times.
    for (int i = 0; i < 10; i++) {
        clockSource().advance(interval);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&count, &i] { return count > i; });
        }
    }

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, OnePausableJobDoesNotRunWithoutStart) {
    int count = 0;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add a job, ensure that it runs once
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    auto handle = runner().makeJob(std::move(job));
    clockSource().advance(interval);
    ASSERT_EQ(count, 0);

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, OnePausableJobRunsCorrectlyWithStart) {
    int count = 0;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add a job, ensure that it runs once
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    auto handle = runner().makeJob(std::move(job));
    handle->start();
    // Fast forward ten times, we should run all ten times.
    for (int i = 0; i < 10; i++) {
        clockSource().advance(interval);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&count, &i] { return count > i; });
        }
    }

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, OnePausableJobPausesCorrectly) {
    int count = 0;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add a job, ensure that it runs once
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    auto handle = runner().makeJob(std::move(job));
    handle->start();
    // Fast forward ten times, we should run all ten times.
    for (int i = 0; i < 10; i++) {
        clockSource().advance(interval);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&count, &i] { return count > i; });
        }
    }
    auto numExecutionsBeforePause = count;
    handle->pause();
    // Fast forward ten times, we shouldn't run anymore
    for (int i = 0; i < 10; i++) {
        clockSource().advance(interval);
    }
    ASSERT_TRUE(count == numExecutionsBeforePause || count == numExecutionsBeforePause + 1)
        << "Actual values: count: " << count
        << ", numExecutionsBeforePause: " << numExecutionsBeforePause;

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, OnePausableJobResumesCorrectly) {
    int count = 0;
    int numFastForwardsForIterationWhileActive = 10;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add a job, ensure that it runs once
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    auto handle = runner().makeJob(std::move(job));
    handle->start();
    // Fast forward ten times, we should run all ten times.
    for (int i = 0; i < numFastForwardsForIterationWhileActive; i++) {
        clockSource().advance(interval);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&count, &i] { return count > i; });
        }
    }
    auto countBeforePause = count;
    ASSERT_TRUE(countBeforePause == numFastForwardsForIterationWhileActive ||
                countBeforePause == numFastForwardsForIterationWhileActive + 1)
        << "Actual values: countBeforePause: " << countBeforePause
        << ", numFastForwardsForIterationWhileActive: " << numFastForwardsForIterationWhileActive;

    handle->pause();
    // Fast forward ten times, we shouldn't run anymore
    for (int i = 0; i < 10; i++) {
        clockSource().advance(interval);
    }
    handle->resume();
    // Fast forward ten times, we should run all ten times.
    for (int i = 0; i < numFastForwardsForIterationWhileActive; i++) {
        clockSource().advance(interval);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&count, &countBeforePause, &i] { return count > countBeforePause + i; });
        }
    }

    // This is slightly racy so once in a while count will be one extra
    ASSERT_TRUE(count == numFastForwardsForIterationWhileActive * 2 ||
                count == numFastForwardsForIterationWhileActive * 2 + 1)
        << "Actual values: count: " << count
        << ", numFastForwardsForIterationWhileActive: " << numFastForwardsForIterationWhileActive;

    tearDown();
}

TEST_F(PeriodicRunnerImplTestNoSetup, ScheduleBeforeStartupTest) {
    int count = 0;
    Milliseconds interval{5};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Schedule a job before startup
    PeriodicRunner::PeriodicJob job("job",
                                    [&count, &mutex, &cv](Client*) {
                                        {
                                            stdx::unique_lock<stdx::mutex> lk(mutex);
                                            count++;
                                        }
                                        cv.notify_all();
                                    },
                                    interval);

    runner().scheduleJob(std::move(job));

    // Start the runner, job should still run
    runner().startup();

    clockSource().advance(interval);

    {
        stdx::unique_lock<stdx::mutex> lk(mutex);
        cv.wait(lk, [&count] { return count > 0; });
    }

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, TwoJobsTest) {
    int countA = 0;
    int countB = 0;
    Milliseconds intervalA{5};
    Milliseconds intervalB{10};

    stdx::mutex mutex;
    stdx::condition_variable cv;

    // Add two jobs, ensure they both run the proper number of times
    PeriodicRunner::PeriodicJob jobA("job",
                                     [&countA, &mutex, &cv](Client*) {
                                         {
                                             stdx::unique_lock<stdx::mutex> lk(mutex);
                                             countA++;
                                         }
                                         cv.notify_all();
                                     },
                                     intervalA);

    PeriodicRunner::PeriodicJob jobB("job",
                                     [&countB, &mutex, &cv](Client*) {
                                         {
                                             stdx::unique_lock<stdx::mutex> lk(mutex);
                                             countB++;
                                         }
                                         cv.notify_all();
                                     },
                                     intervalB);

    runner().scheduleJob(std::move(jobA));
    runner().scheduleJob(std::move(jobB));

    // Fast forward and wait for both jobs to run the right number of times
    for (int i = 0; i <= 10; i++) {
        clockSource().advance(intervalA);
        {
            stdx::unique_lock<stdx::mutex> lk(mutex);
            cv.wait(lk, [&countA, &countB, &i] { return (countA > i && countB >= i / 2); });
        }
    }

    tearDown();
}

TEST_F(PeriodicRunnerImplTest, TwoJobsDontDeadlock) {
    stdx::mutex mutex;
    stdx::condition_variable cv;
    stdx::condition_variable doneCv;
    bool a = false;
    bool b = false;

    PeriodicRunner::PeriodicJob jobA("job",
                                     [&](Client*) {
                                         stdx::unique_lock<stdx::mutex> lk(mutex);
                                         a = true;

                                         cv.notify_one();
                                         cv.wait(lk, [&] { return b; });
                                         doneCv.notify_one();
                                     },
                                     Milliseconds(1));

    PeriodicRunner::PeriodicJob jobB("job",
                                     [&](Client*) {
                                         stdx::unique_lock<stdx::mutex> lk(mutex);
                                         b = true;

                                         cv.notify_one();
                                         cv.wait(lk, [&] { return a; });
                                         doneCv.notify_one();
                                     },
                                     Milliseconds(1));

    runner().scheduleJob(std::move(jobA));
    runner().scheduleJob(std::move(jobB));

    clockSource().advance(Milliseconds(1));

    {
        stdx::unique_lock<stdx::mutex> lk(mutex);
        doneCv.wait(lk, [&] { return a && b; });

        ASSERT(a);
        ASSERT(b);
    }

    tearDown();
}

}  // namespace
}  // namespace mongo
