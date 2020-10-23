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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kIndex

#include "mongo/platform/basic.h"

#include "mongo/db/database_index_builds_tracker.h"

#include "mongo/db/catalog/index_builds_manager.h"
#include "mongo/util/log.h"

namespace mongo {

DatabaseIndexBuildsTracker::~DatabaseIndexBuildsTracker() {
    invariant(_allIndexBuilds.empty());
}

void DatabaseIndexBuildsTracker::addIndexBuild(
    WithLock, std::shared_ptr<ReplIndexBuildState> replIndexBuildState) {
    invariant(_allIndexBuilds.insert({replIndexBuildState->buildUUID, replIndexBuildState}).second);
}

void DatabaseIndexBuildsTracker::removeIndexBuild(WithLock, const UUID& buildUUID) {
    auto it = _allIndexBuilds.find(buildUUID);
    invariant(it != _allIndexBuilds.end());
    _allIndexBuilds.erase(it);

    if (_allIndexBuilds.empty()) {
        _noIndexBuildsRemainCondVar.notify_all();
    }
}

void DatabaseIndexBuildsTracker::runOperationOnAllBuilds(
    WithLock lk,
    IndexBuildsManager* indexBuildsManager,
    std::function<void(WithLock,
                       IndexBuildsManager* indexBuildsManager,
                       std::shared_ptr<ReplIndexBuildState> replIndexBuildState,
                       const std::string& reason)> func,
    const std::string& reason) {
    for (auto it = _allIndexBuilds.begin(); it != _allIndexBuilds.end(); ++it) {
        func(lk, indexBuildsManager, it->second, reason);
    }
}

int DatabaseIndexBuildsTracker::getNumberOfIndexBuilds(WithLock) const {
    return _allIndexBuilds.size();
}

void DatabaseIndexBuildsTracker::waitUntilNoIndexBuildsRemain(stdx::unique_lock<stdx::mutex>& lk) {
    _noIndexBuildsRemainCondVar.wait(lk, [&] {
        if (_allIndexBuilds.empty()) {
            return true;
        }

        log() << "Waiting until the following index builds are finished:";
        for (const auto& indexBuild : _allIndexBuilds) {
            log() << "    Index build with UUID: " << indexBuild.first;
        }

        return false;
    });
}

}  // namespace mongo
