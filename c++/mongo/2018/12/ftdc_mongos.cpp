
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
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC

#include "mongo/platform/basic.h"

#include "mongo/db/ftdc/ftdc_mongos.h"

#include <boost/filesystem.hpp>

#include "mongo/db/ftdc/controller.h"
#include "mongo/db/ftdc/ftdc_server.h"
#include "mongo/db/server_parameters.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/log.h"
#include "mongo/util/synchronized_value.h"

namespace mongo {

namespace {

/**
 * Expose diagnosticDataCollectionDirectoryPath set parameter to specify the MongoS FTDC path.
 */
synchronized_value<boost::filesystem::path> ftdcDirectoryPathParameter;
}  // namespace

void ftdcDirectoryAppendBSON(OperationContext* opCtx, BSONObjBuilder* b, StringData name) {
    b->append(name, ftdcDirectoryPathParameter->generic_string());
}

Status ftdcDirectoryFromString(StringData str) {
    if (hasGlobalServiceContext()) {
        FTDCController* controller = FTDCController::get(getGlobalServiceContext());
        if (controller) {
            Status s = controller->setDirectory(str.toString());
            if (!s.isOK()) {
                return s;
            }
        }
    }

    ftdcDirectoryPathParameter = str.toString();

    return Status::OK();
}

void registerMongoSCollectors(FTDCController* controller) {
    // PoolStats
    controller->addPeriodicCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "connPoolStats", "connPoolStats", "", BSON("connPoolStats" << 1)));
}

void startMongoSFTDC() {
    // Get the path to use for FTDC:
    // 1. Check if the user set one.
    // 2. If not, check if the user has a logpath and derive one.
    // 3. Otherwise, tell the user FTDC cannot run.

    // Only attempt to enable FTDC if we have a path to log files to.
    FTDCStartMode startMode = FTDCStartMode::kStart;
    auto directory = ftdcDirectoryPathParameter.get();

    if (directory.empty()) {
        if (serverGlobalParams.logpath.empty()) {
            warning() << "FTDC is disabled because neither '--logpath' nor set parameter "
                         "'diagnosticDataCollectionDirectoryPath' are specified.";
            startMode = FTDCStartMode::kSkipStart;
        } else {
            directory = boost::filesystem::absolute(
                FTDCUtil::getMongoSPath(serverGlobalParams.logpath), serverGlobalParams.cwd);

            // Update the server parameter with the computed path.
            // Note: If the computed FTDC directory conflicts with an existing file, then FTDC will
            // warn about the conflict, and not startup. It will not terminate MongoS in this
            // situation.
            ftdcDirectoryPathParameter = directory;
        }
    }

    startFTDC(directory, startMode, registerMongoSCollectors);
}

void stopMongoSFTDC() {
    stopFTDC();
}

}  // namespace mongo
