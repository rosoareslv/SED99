/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplication

#include "mongo/platform/basic.h"

#include <memory>

#include "mongo/base/disallow_copying.h"
#include "mongo/db/repl/replication_coordinator_impl.h"
#include "mongo/db/repl/topology_coordinator_impl.h"
#include "mongo/db/repl/vote_requester.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace repl {

class ReplicationCoordinatorImpl::LoseElectionGuardV1 {
    MONGO_DISALLOW_COPYING(LoseElectionGuardV1);

public:
    LoseElectionGuardV1(ReplicationCoordinatorImpl* replCoord) : _replCoord(replCoord) {}

    virtual ~LoseElectionGuardV1() {
        if (_dismissed) {
            return;
        }
        _replCoord->_topCoord->processLoseElection();
        _replCoord->_voteRequester.reset(nullptr);
        if (_isDryRun && _replCoord->_electionDryRunFinishedEvent.isValid()) {
            _replCoord->_replExecutor->signalEvent(_replCoord->_electionDryRunFinishedEvent);
        }
        if (_replCoord->_electionFinishedEvent.isValid()) {
            _replCoord->_replExecutor->signalEvent(_replCoord->_electionFinishedEvent);
        }
    }

    void dismiss() {
        _dismissed = true;
    }

protected:
    ReplicationCoordinatorImpl* const _replCoord;
    bool _isDryRun = false;
    bool _dismissed = false;
};

class ReplicationCoordinatorImpl::LoseElectionDryRunGuardV1 : public LoseElectionGuardV1 {
    MONGO_DISALLOW_COPYING(LoseElectionDryRunGuardV1);

public:
    LoseElectionDryRunGuardV1(ReplicationCoordinatorImpl* replCoord)
        : LoseElectionGuardV1(replCoord) {
        _isDryRun = true;
    }
};


void ReplicationCoordinatorImpl::_startElectSelfV1() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _startElectSelfV1_inlock();
}

void ReplicationCoordinatorImpl::_startElectSelfV1_inlock() {
    invariant(!_voteRequester);
    invariant(!_freshnessChecker);

    switch (_rsConfigState) {
        case kConfigSteady:
            break;
        case kConfigInitiating:
        case kConfigReconfiguring:
        case kConfigHBReconfiguring:
            LOG(2) << "Not standing for election; processing a configuration change";
            // Transition out of candidate role.
            _topCoord->processLoseElection();
            return;
        default:
            severe() << "Entered replica set election code while in illegal config state "
                     << int(_rsConfigState);
            fassertFailed(28641);
    }

    auto finishedEvent = _makeEvent();
    if (!finishedEvent) {
        return;
    }
    _electionFinishedEvent = finishedEvent;

    auto dryRunFinishedEvent = _makeEvent();
    if (!dryRunFinishedEvent) {
        return;
    }
    _electionDryRunFinishedEvent = dryRunFinishedEvent;

    LoseElectionDryRunGuardV1 lossGuard(this);


    invariant(_rsConfig.getMemberAt(_selfIndex).isElectable());
    const auto lastOpTime = _getMyLastAppliedOpTime_inlock();

    if (lastOpTime == OpTime()) {
        log() << "not trying to elect self, "
                 "do not yet have a complete set of data from any point in time";
        return;
    }

    log() << "conducting a dry run election to see if we could be elected";
    _voteRequester.reset(new VoteRequester);

    long long term = _topCoord->getTerm();
    StatusWith<executor::TaskExecutor::EventHandle> nextPhaseEvh =
        _voteRequester->start(_replExecutor.get(),
                              _rsConfig,
                              _selfIndex,
                              _topCoord->getTerm(),
                              true,  // dry run
                              lastOpTime);
    if (nextPhaseEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28685, nextPhaseEvh.getStatus());
    _replExecutor->onEvent(nextPhaseEvh.getValue(),
                           stdx::bind(&ReplicationCoordinatorImpl::_onDryRunComplete, this, term));
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_onDryRunComplete(long long originalTerm) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    LoseElectionDryRunGuardV1 lossGuard(this);

    invariant(_voteRequester);

    if (_topCoord->getTerm() != originalTerm) {
        log() << "not running for primary, we have been superceded already";
        return;
    }

    const VoteRequester::Result endResult = _voteRequester->getResult();

    if (endResult == VoteRequester::Result::kInsufficientVotes) {
        log() << "not running for primary, we received insufficient votes";
        return;
    } else if (endResult == VoteRequester::Result::kStaleTerm) {
        log() << "not running for primary, we have been superceded already";
        return;
    } else if (endResult != VoteRequester::Result::kSuccessfullyElected) {
        log() << "not running for primary, we received an unexpected problem";
        return;
    }

    log() << "dry election run succeeded, running for election";
    // Stepdown is impossible from this term update.
    TopologyCoordinator::UpdateTermResult updateTermResult;
    _updateTerm_inlock(originalTerm + 1, &updateTermResult);
    invariant(updateTermResult == TopologyCoordinator::UpdateTermResult::kUpdatedTerm);
    // Secure our vote for ourself first
    _topCoord->voteForMyselfV1();

    // Store the vote in persistent storage.
    LastVote lastVote{originalTerm + 1, _selfIndex};

    auto cbStatus = _replExecutor->scheduleDBWork(
        [this, lastVote](const executor::TaskExecutor::CallbackArgs& cbData) {
            _writeLastVoteForMyElection(lastVote, cbData);
        });
    if (cbStatus.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(34421, cbStatus.getStatus());
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_writeLastVoteForMyElection(
    LastVote lastVote, const executor::TaskExecutor::CallbackArgs& cbData) {
    // storeLocalLastVoteDocument can call back in to the replication coordinator,
    // so _mutex must be unlocked here.  However, we cannot return until we
    // lock it because we want to lose the election on cancel or error and
    // doing so requires _mutex.
    Status status = Status::OK();
    if (cbData.status.isOK()) {
        invariant(cbData.opCtx);
        status = _externalState->storeLocalLastVoteDocument(cbData.opCtx, lastVote);
    }

    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(_voteRequester);
    LoseElectionDryRunGuardV1 lossGuard(this);
    if (!cbData.status.isOK()) {
        return;
    }

    if (!status.isOK()) {
        error() << "failed to store LastVote document when voting for myself: " << status;
        return;
    }

    _startVoteRequester_inlock(lastVote.getTerm());
    _replExecutor->signalEvent(_electionDryRunFinishedEvent);

    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_startVoteRequester_inlock(long long newTerm) {
    invariant(_voteRequester);

    const auto lastOpTime = _getMyLastAppliedOpTime_inlock();

    _voteRequester.reset(new VoteRequester);
    StatusWith<executor::TaskExecutor::EventHandle> nextPhaseEvh = _voteRequester->start(
        _replExecutor.get(), _rsConfig, _selfIndex, _topCoord->getTerm(), false, lastOpTime);
    if (nextPhaseEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28643, nextPhaseEvh.getStatus());
    _replExecutor->onEvent(
        nextPhaseEvh.getValue(),
        stdx::bind(&ReplicationCoordinatorImpl::_onVoteRequestComplete, this, newTerm));
}

void ReplicationCoordinatorImpl::_onVoteRequestComplete(long long originalTerm) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    LoseElectionGuardV1 lossGuard(this);

    invariant(_voteRequester);

    if (_topCoord->getTerm() != originalTerm) {
        log() << "not becoming primary, we have been superceded already";
        return;
    }

    const VoteRequester::Result endResult = _voteRequester->getResult();

    switch (endResult) {
        case VoteRequester::Result::kInsufficientVotes:
            log() << "not becoming primary, we received insufficient votes";
            return;
        case VoteRequester::Result::kStaleTerm:
            log() << "not becoming primary, we have been superceded already";
            return;
        case VoteRequester::Result::kSuccessfullyElected:
            log() << "election succeeded, assuming primary role in term " << _topCoord->getTerm();
            break;
    }

    // Mark all nodes that responded to our vote request as up to avoid immediately
    // relinquishing primary.
    Date_t now = _replExecutor->now();
    const unordered_set<HostAndPort> liveNodes = _voteRequester->getResponders();
    for (auto& nodeInfo : _slaveInfo) {
        if (liveNodes.count(nodeInfo.hostAndPort)) {
            nodeInfo.down = false;
            nodeInfo.lastUpdate = now;
        }
    }

    // Prevent last committed optime from updating until we finish draining.
    _setFirstOpTimeOfMyTerm_inlock(
        OpTime(Timestamp(std::numeric_limits<int>::max(), 0), std::numeric_limits<int>::max()));

    _voteRequester.reset();
    auto electionFinishedEvent = _electionFinishedEvent;

    lk.unlock();
    _performPostMemberStateUpdateAction(kActionWinElection);

    _replExecutor->signalEvent(electionFinishedEvent);
    lossGuard.dismiss();
}

}  // namespace repl
}  // namespace mongo
