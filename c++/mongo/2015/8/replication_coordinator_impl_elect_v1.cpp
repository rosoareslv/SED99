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
#include "mongo/db/repl/election_winner_declarer.h"
#include "mongo/db/repl/vote_requester.h"
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
        _replCoord->_electionWinnerDeclarer.reset(nullptr);
        _replCoord->_voteRequester.reset(nullptr);
        if (_isDryRun && _replCoord->_electionDryRunFinishedEvent.isValid()) {
            _replCoord->_replExecutor.signalEvent(_replCoord->_electionDryRunFinishedEvent);
        }
        if (_replCoord->_electionFinishedEvent.isValid()) {
            _replCoord->_replExecutor.signalEvent(_replCoord->_electionFinishedEvent);
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
    invariant(!_electionWinnerDeclarer);
    invariant(!_voteRequester);
    invariant(!_freshnessChecker);

    stdx::unique_lock<stdx::mutex> lk(_mutex);
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

    const StatusWith<ReplicationExecutor::EventHandle> finishEvh = _replExecutor.makeEvent();
    if (finishEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28642, finishEvh.getStatus());
    _electionFinishedEvent = finishEvh.getValue();

    const StatusWith<ReplicationExecutor::EventHandle> dryRunFinishEvh = _replExecutor.makeEvent();
    if (dryRunFinishEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28767, dryRunFinishEvh.getStatus());
    _electionDryRunFinishedEvent = dryRunFinishEvh.getValue();

    LoseElectionDryRunGuardV1 lossGuard(this);


    invariant(_rsConfig.getMemberAt(_selfIndex).isElectable());
    OpTime lastOpTimeApplied(_getMyLastOptime_inlock());

    if (lastOpTimeApplied == OpTime()) {
        log() << "not trying to elect self, "
                 "do not yet have a complete set of data from any point in time";
        return;
    }

    log() << "conducting a dry run election to see if we could be elected";
    _voteRequester.reset(new VoteRequester);

    // This is necessary because the voteRequester may call directly into winning an
    // election, if there are no other MaybeUp nodes.  Winning an election attempts to lock
    // _mutex again.
    lk.unlock();

    long long term = _topCoord->getTerm();
    StatusWith<ReplicationExecutor::EventHandle> nextPhaseEvh = _voteRequester->start(
        &_replExecutor,
        _rsConfig,
        _rsConfig.getMemberAt(_selfIndex).getId(),
        _topCoord->getTerm(),
        true,  // dry run
        getMyLastOptime(),
        stdx::bind(&ReplicationCoordinatorImpl::_onDryRunComplete, this, term));
    if (nextPhaseEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28685, nextPhaseEvh.getStatus());
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_onDryRunComplete(long long originalTerm) {
    invariant(_voteRequester);
    invariant(!_electionWinnerDeclarer);
    LoseElectionDryRunGuardV1 lossGuard(this);

    if (_topCoord->getTerm() != originalTerm) {
        log() << "not running for primary, we have been superceded already";
        return;
    }

    const VoteRequester::VoteRequestResult endResult = _voteRequester->getResult();

    if (endResult == VoteRequester::InsufficientVotes) {
        log() << "not running for primary, we received insufficient votes";
        return;
    } else if (endResult == VoteRequester::StaleTerm) {
        log() << "not running for primary, we have been superceded already";
        return;
    } else if (endResult != VoteRequester::SuccessfullyElected) {
        log() << "not running for primary, we received an unexpected problem";
        return;
    }

    log() << "dry election run succeeded, running for election";
    _updateTerm_incallback(originalTerm + 1);
    // Secure our vote for ourself first
    _topCoord->voteForMyselfV1();

    // Store the vote in persistent storage.
    LastVote lastVote;
    lastVote.setTerm(originalTerm + 1);
    lastVote.setCandidateId(getMyId());

    auto cbStatus = _replExecutor.scheduleDBWork(
        [this, lastVote](const ReplicationExecutor::CallbackArgs& cbData) {
            _writeLastVoteForMyElection(lastVote, cbData);
        });
    if (cbStatus.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28769, cbStatus.getStatus());
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_writeLastVoteForMyElection(
    LastVote lastVote, const ReplicationExecutor::CallbackArgs& cbData) {
    invariant(_voteRequester);
    invariant(!_electionWinnerDeclarer);
    LoseElectionDryRunGuardV1 lossGuard(this);

    if (cbData.status == ErrorCodes::CallbackCanceled) {
        return;
    }
    invariant(cbData.txn);

    Status status = _externalState->storeLocalLastVoteDocument(cbData.txn, lastVote);
    if (!status.isOK()) {
        error() << "failed to store LastVote document when voting for myself: " << status;
        return;
    }

    auto cbStatus = _replExecutor.scheduleWork(
        [this, lastVote](const ReplicationExecutor::CallbackArgs& cbData) {
            _startVoteRequester(lastVote.getTerm());
        });
    if (cbStatus.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28768, cbStatus.getStatus());

    _replExecutor.signalEvent(_electionDryRunFinishedEvent);
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_startVoteRequester(long long newTerm) {
    invariant(_voteRequester);
    invariant(!_electionWinnerDeclarer);
    LoseElectionGuardV1 lossGuard(this);

    _voteRequester.reset(new VoteRequester);
    StatusWith<ReplicationExecutor::EventHandle> nextPhaseEvh = _voteRequester->start(
        &_replExecutor,
        _rsConfig,
        _rsConfig.getMemberAt(_selfIndex).getId(),
        _topCoord->getTerm(),
        false,
        getMyLastOptime(),
        stdx::bind(&ReplicationCoordinatorImpl::_onVoteRequestComplete, this, newTerm));
    if (nextPhaseEvh.getStatus() == ErrorCodes::ShutdownInProgress) {
        return;
    }
    fassert(28643, nextPhaseEvh.getStatus());

    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_onVoteRequestComplete(long long originalTerm) {
    invariant(_voteRequester);
    invariant(!_electionWinnerDeclarer);
    LoseElectionGuardV1 lossGuard(this);

    if (_topCoord->getTerm() != originalTerm) {
        log() << "not becoming primary, we have been superceded already";
        return;
    }

    const VoteRequester::VoteRequestResult endResult = _voteRequester->getResult();

    if (endResult == VoteRequester::InsufficientVotes) {
        log() << "not becoming primary, we received insufficient votes";
        return;
    } else if (endResult == VoteRequester::StaleTerm) {
        log() << "not becoming primary, we have been superceded already";
        return;
    } else if (endResult != VoteRequester::SuccessfullyElected) {
        log() << "not becoming primary, we received an unexpected problem";
        return;
    }

    log() << "election succeeded, assuming primary role in term " << _topCoord->getTerm();
    // Prevent last committed optime from updating until we finish draining.
    _setFirstOpTimeOfMyTerm(
        OpTime(Timestamp(std::numeric_limits<int>::max(), 0), std::numeric_limits<int>::max()));
    _performPostMemberStateUpdateAction(kActionWinElection);

    _voteRequester.reset(nullptr);
    _replExecutor.signalEvent(_electionFinishedEvent);
    lossGuard.dismiss();
}

void ReplicationCoordinatorImpl::_onElectionWinnerDeclarerComplete() {
    LoseElectionGuardV1 lossGuard(this);

    invariant(_voteRequester);
    invariant(_electionWinnerDeclarer);

    const Status endResult = _electionWinnerDeclarer->getStatus();

    if (!endResult.isOK()) {
        log() << "stepping down from primary, because: " << endResult;
        _topCoord->prepareForStepDown();
        _stepDownStart();
    }

    lossGuard.dismiss();
    _voteRequester.reset(nullptr);
    _electionWinnerDeclarer.reset(nullptr);
    _replExecutor.signalEvent(_electionFinishedEvent);
}

}  // namespace repl
}  // namespace mongo
