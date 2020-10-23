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

#include "mongo/db/server_recovery.h"

#include "mongo/db/namespace_string.h"

namespace mongo {
namespace {
const auto getInReplicationRecovery = ServiceContext::declareDecoration<bool>();
const auto getSizeRecoveryState = ServiceContext::declareDecoration<SizeRecoveryState>();
}  // namespace

bool SizeRecoveryState::collectionNeedsSizeAdjustment(const std::string& ns) const {
    if (!inReplicationRecovery(getGlobalServiceContext())) {
        return true;
    }

    if (NamespaceString::oplog(ns)) {
        return true;
    }

    stdx::lock_guard<stdx::mutex> lock(_mutex);
    return _collectionsAlwaysNeedingSizeAdjustment.count(ns) > 0;
}

void SizeRecoveryState::markCollectionAsAlwaysNeedsSizeAdjustment(const std::string& ns) {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    _collectionsAlwaysNeedingSizeAdjustment.insert(ns);
}

void SizeRecoveryState::clearStateAfterRecovery() {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    _collectionsAlwaysNeedingSizeAdjustment.clear();
}
}  // namespace mongo

bool& mongo::inReplicationRecovery(ServiceContext* serviceCtx) {
    return getInReplicationRecovery(serviceCtx);
}

mongo::SizeRecoveryState& mongo::sizeRecoveryState(ServiceContext* serviceCtx) {
    return getSizeRecoveryState(serviceCtx);
}
