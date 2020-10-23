/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/embedded/periodic_runner_embedded.h"

#include "mongo/db/client.h"
#include "mongo/db/service_context.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {

template <typename T>
std::shared_ptr<T> lockAndAssertExists(std::weak_ptr<T> ptr, StringData errMsg) {
    if (auto p = ptr.lock()) {
        return p;
    } else {
        uasserted(ErrorCodes::InternalError, errMsg);
    }
}

constexpr auto kPeriodicJobHandleLifetimeErrMsg =
    "The PeriodicRunner job for this handle no longer exists"_sd;

}  // namespace

struct PeriodicRunnerEmbedded::PeriodicJobSorter {
    bool operator()(std::shared_ptr<PeriodicJobImpl> const& lhs,
                    std::shared_ptr<PeriodicJobImpl> const& rhs) const {
        // Use greater-than to make the heap a min-heap
        return lhs->nextScheduledRun() > rhs->nextScheduledRun();
    }
};

PeriodicRunnerEmbedded::PeriodicRunnerEmbedded(ServiceContext* svc, ClockSource* clockSource)
    : _svc(svc), _clockSource(clockSource) {}

PeriodicRunnerEmbedded::~PeriodicRunnerEmbedded() {
    PeriodicRunnerEmbedded::shutdown();
}

std::shared_ptr<PeriodicRunnerEmbedded::PeriodicJobImpl> PeriodicRunnerEmbedded::createAndAddJob(
    PeriodicJob job, bool shouldStart) {
    auto impl = std::make_shared<PeriodicJobImpl>(std::move(job), this->_clockSource, this->_svc);

    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _jobs.push_back(impl);
    std::push_heap(_jobs.begin(), _jobs.end(), PeriodicJobSorter());
    if (shouldStart && _running)
        impl->start();
    return impl;
}

std::unique_ptr<PeriodicRunner::PeriodicJobHandle> PeriodicRunnerEmbedded::makeJob(
    PeriodicJob job) {
    return std::make_unique<PeriodicJobHandleImpl>(createAndAddJob(std::move(job), false));
}

void PeriodicRunnerEmbedded::scheduleJob(PeriodicJob job) {
    createAndAddJob(std::move(job), true);
}

void PeriodicRunnerEmbedded::startup() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    if (_running) {
        return;
    }

    _running = true;

    // schedule any jobs that we have
    for (auto& job : _jobs) {
        job->start();
    }
}

void PeriodicRunnerEmbedded::shutdown() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    if (_running) {
        _running = false;

        for (auto& job : _jobs) {
            if (job->isAlive(lk)) {
                job->stop();
            }
        }
        _jobs.clear();
    }
}

bool PeriodicRunnerEmbedded::tryPump() {
    stdx::unique_lock<stdx::mutex> lock(_mutex, stdx::try_to_lock);
    if (!lock.owns_lock())
        return false;

    const auto now = _clockSource->now();

    // First check if any paused jobs have been set to running again
    for (auto it = _Pausedjobs.begin(); it != _Pausedjobs.end();) {
        auto& job = *(*it);

        PeriodicJobImpl::ExecutionStatus jobExecStatus;
        {
            stdx::lock_guard<stdx::mutex> jobLock(job._mutex);
            jobExecStatus = job._execStatus;
        }

        if (jobExecStatus == PeriodicJobImpl::ExecutionStatus::kPaused ||
            jobExecStatus == PeriodicJobImpl::ExecutionStatus::kNotScheduled) {
            ++it;
            continue;
        }

        // If running job found, push to running heap
        if (jobExecStatus == PeriodicJobImpl::ExecutionStatus::kRunning) {
            _jobs.push_back(std::move(*it));
            std::push_heap(_jobs.begin(), _jobs.end(), PeriodicJobSorter());
        }

        // Running or cancelled jobs should be removed from the paused queue
        std::swap(*it, _Pausedjobs.back());
        _Pausedjobs.pop_back();
    }

    while (!_jobs.empty()) {
        auto& job = *_jobs.front();
        if (now < job.nextScheduledRun())
            break;

        // Begin with taking out current job from the heap
        std::pop_heap(_jobs.begin(), _jobs.end(), PeriodicJobSorter());

        // Just need to hold the job lock while interacting with the execution status, it's the
        // only variable that can be changed from other threads.
        PeriodicJobImpl::ExecutionStatus jobExecStatus;
        {
            stdx::lock_guard<stdx::mutex> jobLock(job._mutex);
            jobExecStatus = job._execStatus;
        }

        switch (jobExecStatus) {
            default:
                invariant(false);
            case PeriodicJobImpl::ExecutionStatus::kPaused:
            case PeriodicJobImpl::ExecutionStatus::kNotScheduled:
                // Paused jobs should be moved to the paused list and removed from the running heap
                _Pausedjobs.push_back(std::move(_jobs.back()));
            // fall through
            case PeriodicJobImpl::ExecutionStatus::kCanceled:
                // Cancelled jobs should be removed
                _jobs.pop_back();
                continue;
            case PeriodicJobImpl::ExecutionStatus::kRunning:
                break;
        };

        // If we get here, the job is in the running state.
        // Run the job without holding the lock so we can pause/cancel concurrently
        job._job.job(Client::getCurrent());

        // Update that the job has executed and put back in heap
        job._lastRun = now;
        std::push_heap(_jobs.begin(), _jobs.end(), PeriodicJobSorter());
    }

    return true;
}

PeriodicRunnerEmbedded::PeriodicJobImpl::PeriodicJobImpl(PeriodicJob job,
                                                         ClockSource* source,
                                                         ServiceContext* svc)
    : _job(std::move(job)), _clockSource(source), _serviceContext(svc) {}

void PeriodicRunnerEmbedded::PeriodicJobImpl::start() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(_execStatus == PeriodicJobImpl::ExecutionStatus::kNotScheduled);
    _execStatus = PeriodicJobImpl::ExecutionStatus::kRunning;
}

void PeriodicRunnerEmbedded::PeriodicJobImpl::pause() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(_execStatus == PeriodicJobImpl::ExecutionStatus::kRunning);
    _execStatus = PeriodicJobImpl::ExecutionStatus::kPaused;
}

void PeriodicRunnerEmbedded::PeriodicJobImpl::resume() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(_execStatus == PeriodicJobImpl::ExecutionStatus::kPaused);
    _execStatus = PeriodicJobImpl::ExecutionStatus::kRunning;
}

void PeriodicRunnerEmbedded::PeriodicJobImpl::stop() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(isAlive(lk));

    _execStatus = PeriodicJobImpl::ExecutionStatus::kCanceled;
}

bool PeriodicRunnerEmbedded::PeriodicJobImpl::isAlive(WithLock lk) {
    return _execStatus == ExecutionStatus::kRunning || _execStatus == ExecutionStatus::kPaused;
}

void PeriodicRunnerEmbedded::PeriodicJobHandleImpl::start() {
    auto job = lockAndAssertExists(_jobWeak, kPeriodicJobHandleLifetimeErrMsg);
    job->start();
}

void PeriodicRunnerEmbedded::PeriodicJobHandleImpl::pause() {
    auto job = lockAndAssertExists(_jobWeak, kPeriodicJobHandleLifetimeErrMsg);
    job->pause();
}

void PeriodicRunnerEmbedded::PeriodicJobHandleImpl::resume() {
    auto job = lockAndAssertExists(_jobWeak, kPeriodicJobHandleLifetimeErrMsg);
    job->resume();
}

}  // namespace mongo
