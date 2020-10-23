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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage
#define DEBUG_LOG_LEVEL 4

#include "mongo/platform/basic.h"

#include "mongo/db/storage/flow_control.h"

#include <algorithm>
#include <fmt/format.h>
#include <limits>

#include "mongo/db/concurrency/flow_control_ticketholder.h"
#include "mongo/db/concurrency/lock_manager_defs.h"
#include "mongo/db/repl/member_data.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage/flow_control_parameters_gen.h"
#include "mongo/util/background.h"
#include "mongo/util/log.h"

namespace mongo {

namespace {
const auto getFlowControl = ServiceContext::declareDecoration<std::unique_ptr<FlowControl>>();
const int kMaxTickets = 1000 * 1000 * 1000;

int multiplyWithOverflowCheck(double term1, double term2, int maxValue) {
    if (static_cast<double>(std::numeric_limits<int>::max()) / term2 < term1) {
        // Multiplying term1 and term2 would overflow, return maxValue.
        return maxValue;
    }

    double ret = term1 * term2;
    if (ret >= maxValue) {
        return maxValue;
    }

    return static_cast<int>(ret);
}

long long getLagMillis(Date_t myLastApplied, Date_t lastCommitted) {
    if (!myLastApplied.isFormattable() || !lastCommitted.isFormattable()) {
        return 0;
    }
    const long long lagMillis = durationCount<Milliseconds>(myLastApplied - lastCommitted);
    return lagMillis;
}

bool isLagged(Date_t myLastApplied, Date_t lastCommitted) {
    const auto lagMillis = getLagMillis(myLastApplied, lastCommitted);
    return lagMillis >= gFlowControlThresholdLagPercentage.load() *
        durationCount<Milliseconds>(Seconds(gFlowControlTargetLagSeconds.load()));
}

Timestamp getMedianAppliedTimestamp(const std::vector<repl::MemberData>& sortedMemberData) {
    if (sortedMemberData.size() == 0) {
        return Timestamp::min();
    }

    const int sustainerIdx = sortedMemberData.size() / 2;
    return sortedMemberData[sustainerIdx].getLastAppliedOpTime().getTimestamp();
}

/**
 * Sanity checks whether the successive queries of topology data are comparable for doing a flow
 * control calculation. In particular, the number of members must be the same and the median
 * applier's timestamp must not go backwards.
 */
bool sustainerAdvanced(const std::vector<repl::MemberData>& prevMemberData,
                       const std::vector<repl::MemberData>& currMemberData) {
    if (currMemberData.size() == 0 || currMemberData.size() != prevMemberData.size()) {
        warning() << "Flow control detected a change in topology. PrevMemberSize: "
                  << prevMemberData.size() << " CurrMemberSize: " << currMemberData.size();
        return false;
    }

    auto currSustainerAppliedTs = getMedianAppliedTimestamp(currMemberData);
    auto prevSustainerAppliedTs = getMedianAppliedTimestamp(prevMemberData);

    if (currSustainerAppliedTs < prevSustainerAppliedTs) {
        warning() << "Flow control's sustainer time decreased. PrevSustainer: "
                  << prevSustainerAppliedTs << " CurrSustainer: " << currSustainerAppliedTs;
        return false;
    }

    return true;
}
}  // namespace

FlowControl::FlowControl(ServiceContext* service, repl::ReplicationCoordinator* replCoord)
    : ServerStatusSection("flowControl"), _replCoord(replCoord) {
    FlowControlTicketholder::set(service, stdx::make_unique<FlowControlTicketholder>(1000));

    service->getPeriodicRunner()->scheduleJob(
        {"FlowControlRefresher",
         [this](Client* client) {
             FlowControlTicketholder::get(client->getServiceContext())->refreshTo(getNumTickets());
         },
         Seconds(1)});
}

FlowControl* FlowControl::get(ServiceContext* service) {
    return getFlowControl(service).get();
}

FlowControl* FlowControl::get(ServiceContext& service) {
    return getFlowControl(service).get();
}

FlowControl* FlowControl::get(OperationContext* ctx) {
    return get(ctx->getClient()->getServiceContext());
}

void FlowControl::set(ServiceContext* service, std::unique_ptr<FlowControl> flowControl) {
    auto& globalFlow = getFlowControl(service);
    globalFlow = std::move(flowControl);
}

/**
 * Returns -1.0 if there are not enough samples.
 */
double FlowControl::_getLocksPerOp() {
    // Primaries sample the number of operations it has applied alongside how many global lock
    // acquisitions (in MODE_IX) it took to process those operations. This method looks at the two
    // most recent samples and returns the ratio of global lock acquisitions to operations processed
    // for the current client workload.
    Sample backTwo;
    Sample backOne;
    std::size_t numSamples;
    {
        stdx::lock_guard<stdx::mutex> lk(_sampledOpsMutex);
        numSamples = _sampledOpsApplied.size();
        if (numSamples >= 2) {
            backTwo = _sampledOpsApplied[numSamples - 2];
            backOne = _sampledOpsApplied[numSamples - 1];
        } else {
            _lastLocksPerOp.store(0.0);
            return -1.0;
        }
    }

    auto ret = (double)(std::get<2>(backOne) - std::get<2>(backTwo)) /
        (double)(std::get<1>(backOne) - std::get<1>(backTwo));
    _lastLocksPerOp.store(ret);
    return ret;
}

BSONObj FlowControl::generateSection(OperationContext* opCtx,
                                     const BSONElement& configElement) const {
    // Lag is not meaningful on arbiters.
    const bool isArbiter =
        _replCoord->getReplicationMode() == repl::ReplicationCoordinator::modeReplSet &&
        _replCoord->getMemberState().arbiter();

    // Flow Control is only enabled if FCV is 4.2.
    const bool isFCV42 =
        (serverGlobalParams.featureCompatibility.isVersionInitialized() &&
         serverGlobalParams.featureCompatibility.getVersion() ==
             ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo42);

    const Date_t myLastAppliedWall = _replCoord->getMyLastAppliedOpTimeAndWallTime().wallTime;
    const Date_t lastCommittedWall = _replCoord->getLastCommittedOpTimeAndWallTime().wallTime;

    BSONObjBuilder bob;
    // Most of these values are only computed and meaningful when flow control is enabled.
    bob.append("enabled", gFlowControlEnabled.load());
    bob.append("targetRateLimit", _lastTargetTicketsPermitted.load());
    bob.append("timeAcquiringMicros",
               FlowControlTicketholder::get(opCtx)->totalTimeAcquiringMicros());
    bob.append("locksPerOp", _lastLocksPerOp.load());
    bob.append("sustainerRate", _lastSustainerAppliedCount.load());
    bob.append("isLagged", isFCV42 && !isArbiter && isLagged(myLastAppliedWall, lastCommittedWall));

    return bob.obj();
}

/**
 * Advance the `_*MemberData` fields and sort the new data by the element's last applied optime.
 */
void FlowControl::_updateTopologyData() {
    _prevMemberData = _currMemberData;
    _currMemberData = _replCoord->getMemberData();

    // Sort MemberData with the 0th index being the node with the lowest applied optime.
    std::sort(_currMemberData.begin(),
              _currMemberData.end(),
              [](const repl::MemberData& left, const repl::MemberData& right) -> bool {
                  return left.getLastAppliedOpTime() < right.getLastAppliedOpTime();
              });
}

int FlowControl::_calculateNewTicketsForLag(const std::vector<repl::MemberData>& prevMemberData,
                                            const std::vector<repl::MemberData>& currMemberData,
                                            std::int64_t locksUsedLastPeriod,
                                            double locksPerOp) {
    using namespace fmt::literals;

    const auto currSustainerAppliedTs = getMedianAppliedTimestamp(currMemberData);
    const auto prevSustainerAppliedTs = getMedianAppliedTimestamp(_prevMemberData);
    invariant(prevSustainerAppliedTs <= currSustainerAppliedTs,
              "PrevSustainer: {} CurrSustainer: {}"_format(prevSustainerAppliedTs.toString(),
                                                           currSustainerAppliedTs.toString()));

    const std::int64_t sustainerAppliedCount =
        _approximateOpsBetween(prevSustainerAppliedTs, currSustainerAppliedTs);
    LOG(DEBUG_LOG_LEVEL) << " PrevApplied: " << prevSustainerAppliedTs
                         << " CurrApplied: " << currSustainerAppliedTs
                         << " NumSustainerApplied: " << sustainerAppliedCount;

    _lastSustainerAppliedCount.store(static_cast<int>(sustainerAppliedCount));
    if (sustainerAppliedCount == -1) {
        // We don't know how many ops the sustainer applied. Hand out less tickets than were
        // used in the last period.
        return std::min(static_cast<int>(locksUsedLastPeriod / 2.0), kMaxTickets);
    }

    // We know how many ops the sustainer applied, use that for calculating the new number of
    // tickets.
    const double sustainerAppliedPenalty = (double)(sustainerAppliedCount) / 2.0;
    LOG(DEBUG_LOG_LEVEL) << "LocksPerOp: " << locksPerOp << " Sustainer: " << sustainerAppliedCount
                         << " Target: " << sustainerAppliedPenalty;

    return multiplyWithOverflowCheck(locksPerOp, sustainerAppliedPenalty, kMaxTickets);
}

int FlowControl::getNumTickets() {
    // Lag is not meaningful on arbiters.
    const bool isArbiter =
        _replCoord->getReplicationMode() == repl::ReplicationCoordinator::modeReplSet &&
        _replCoord->getMemberState().arbiter();

    // Flow Control is only enabled if FCV is 4.2.
    const bool isFCV42 =
        (serverGlobalParams.featureCompatibility.isVersionInitialized() &&
         serverGlobalParams.featureCompatibility.getVersion() ==
             ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo42);

    // It's important to update the topology on each iteration.
    _updateTopologyData();
    const repl::OpTimeAndWallTime myLastApplied = _replCoord->getMyLastAppliedOpTimeAndWallTime();
    const repl::OpTimeAndWallTime lastCommitted = _replCoord->getLastCommittedOpTimeAndWallTime();
    const double locksPerOp = _getLocksPerOp();
    const std::int64_t locksUsedLastPeriod = _getLocksUsedLastPeriod();

    if (serverGlobalParams.enableMajorityReadConcern == false ||
        gFlowControlEnabled.load() == false || isFCV42 == false || isArbiter || locksPerOp < 0.0) {
        _trimSamples(std::min(lastCommitted.opTime.getTimestamp(),
                              getMedianAppliedTimestamp(_prevMemberData)));
        return kMaxTickets;
    }

    int ret = 0;
    const bool isHealthy = !isLagged(myLastApplied.wallTime, lastCommitted.wallTime) ||
        // _approximateOpsBetween will return -1 if the input timestamps are in the same "bucket".
        // This is an indication that there are very few ops between the two timestamps.
        //
        // Don't let the no-op writer on idle systems fool the sophisticated "is the replica set
        // lagged" classifier.
        _approximateOpsBetween(lastCommitted.opTime.getTimestamp(),
                               myLastApplied.opTime.getTimestamp()) == -1;

    if (isHealthy) {
        ret =
            multiplyWithOverflowCheck(_lastTargetTicketsPermitted.load() + 1000, 1.1, kMaxTickets);
    } else if (sustainerAdvanced(_prevMemberData, _currMemberData)) {
        // Expected case where flow control has meaningful data from the last period to make a new
        // calculation.
        ret = _calculateNewTicketsForLag(
            _prevMemberData, _currMemberData, locksUsedLastPeriod, locksPerOp);
    } else {
        // Unexpected case where consecutive readings from the topology state don't meet some basic
        // expectations.
        ret = _lastTargetTicketsPermitted.load();
    }

    ret = std::max(ret, gFlowControlMinTicketsPerSecond.load());

    LOG(DEBUG_LOG_LEVEL) << "Are lagged? " << !isHealthy << " Curr lag millis: "
                         << getLagMillis(myLastApplied.wallTime, lastCommitted.wallTime)
                         << " OpsLagged: "
                         << _approximateOpsBetween(lastCommitted.opTime.getTimestamp(),
                                                   myLastApplied.opTime.getTimestamp())
                         << " Granting: " << ret
                         << " Last granted: " << _lastTargetTicketsPermitted.load()
                         << " Last sustainer applied: " << _lastSustainerAppliedCount.load()
                         << " Acquisitions since last check: " << locksUsedLastPeriod
                         << " Locks per op: " << _lastLocksPerOp.load();

    _lastTargetTicketsPermitted.store(ret);

    _trimSamples(
        std::min(lastCommitted.opTime.getTimestamp(), getMedianAppliedTimestamp(_prevMemberData)));

    return ret;
}

std::int64_t FlowControl::_approximateOpsBetween(Timestamp prevTs, Timestamp currTs) {
    std::int64_t prevApplied = -1;
    std::int64_t currApplied = -1;

    stdx::lock_guard<stdx::mutex> lk(_sampledOpsMutex);
    for (auto&& sample : _sampledOpsApplied) {
        if (prevApplied == -1 && prevTs.asULL() < std::get<0>(sample)) {
            prevApplied = std::get<1>(sample);
        }

        if (currApplied == -1 && currTs.asULL() < std::get<0>(sample)) {
            currApplied = std::get<1>(sample);
            break;
        }
    }

    if (prevApplied != -1 && currApplied == -1) {
        currApplied = std::get<1>(_sampledOpsApplied[_sampledOpsApplied.size() - 1]);
    }

    if (prevApplied != -1 && currApplied != -1) {
        return currApplied - prevApplied;
    }

    return -1;
}

void FlowControl::sample(Timestamp timestamp, std::uint64_t opsApplied) {
    if (serverGlobalParams.enableMajorityReadConcern == false) {
        return;
    }

    stdx::lock_guard<stdx::mutex> lk(_sampledOpsMutex);
    _numOpsSinceStartup += opsApplied;
    if (_numOpsSinceStartup - _lastSample <
        static_cast<std::size_t>(gFlowControlSamplePeriod.load())) {
        // Naively sample once every 1000 or so operations.
        return;
    }

    if (_sampledOpsApplied.size() > 0 &&
        static_cast<std::uint64_t>(timestamp.asULL()) <= std::get<0>(_sampledOpsApplied.back())) {
        // The optime generator mutex is no longer held, these timestamps can come in out of order.
        return;
    }

    SingleThreadedLockStats stats;
    reportGlobalLockingStats(&stats);

    _lastSample = _numOpsSinceStartup;

    const auto lockAcquisitions = stats.get(resourceIdGlobal, LockMode::MODE_IX).numAcquisitions;
    LOG(DEBUG_LOG_LEVEL) << "Sampling. Time: " << timestamp << " Applied: " << _numOpsSinceStartup
                         << " LockAcquisitions: " << lockAcquisitions;

    if (_sampledOpsApplied.size() <
        static_cast<std::deque<Sample>::size_type>(gFlowControlMaxSamples)) {
        _sampledOpsApplied.emplace_back(
            static_cast<std::uint64_t>(timestamp.asULL()), _numOpsSinceStartup, lockAcquisitions);
    } else {
        // At ~24 bytes per sample, 1 million samples is ~24MB of memory. Instead of growing
        // proportionally to replication lag, FlowControl opts to lose resolution (the number of
        // operations between recorded samples increases). Hitting the sample limit implies there's
        // replication lag. When there's replication lag, the oldest values are actively being used
        // to compute the number of tickets to allocate. FlowControl intentionally prioritizes the
        // oldest entries as those are, by definition, the most valuable when there is lag. Instead,
        // we choose to lose resolution at the newest value.
        _sampledOpsApplied[_sampledOpsApplied.size() - 1] = {
            static_cast<std::uint64_t>(timestamp.asULL()), _numOpsSinceStartup, lockAcquisitions};
    }
}

void FlowControl::_trimSamples(const Timestamp trimTo) {
    int numTrimmed = 0;
    stdx::lock_guard<stdx::mutex> lk(_sampledOpsMutex);
    // Always leave at least two samples for calculating `locksPerOp`.
    while (_sampledOpsApplied.size() > 2 &&
           std::get<0>(_sampledOpsApplied.front()) < trimTo.asULL()) {
        _sampledOpsApplied.pop_front();
        ++numTrimmed;
    }

    LOG(DEBUG_LOG_LEVEL) << "Trimmed samples. Num: " << numTrimmed;
}

int64_t FlowControl::_getLocksUsedLastPeriod() {
    SingleThreadedLockStats stats;
    reportGlobalLockingStats(&stats);

    int64_t counter = stats.get(resourceIdGlobal, LockMode::MODE_IX).numAcquisitions;
    int64_t ret = counter - _lastPollLockAcquisitions;
    _lastPollLockAcquisitions = counter;

    return ret;
}

}  // namespace mongo
