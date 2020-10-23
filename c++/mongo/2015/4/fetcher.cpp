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

#include "mongo/platform/basic.h"

#include "mongo/db/repl/fetcher.h"

#include <boost/thread/lock_guard.hpp>

#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/replication_executor.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {
namespace repl {

namespace {

    const char* kCursorFieldName = "cursor";
    const char* kCursorIdFieldName = "id";
    const char* kNamespaceFieldName = "ns";

    const char* kFirstBatchFieldName = "firstBatch";
    const char* kNextBatchFieldName = "nextBatch";

    /**
     * Parses cursor response in command result for cursor ID, namespace and documents.
     * 'batchFieldName' will be 'firstBatch' for the initial remote command invocation and
     * 'nextBatch' for getMore.
     */
    Status parseCursorResponse(const BSONObj& obj,
                               const std::string& batchFieldName,
                               Fetcher::BatchData* batchData,
                               NamespaceString* nss) {
        invariant(batchFieldName == kFirstBatchFieldName || batchFieldName == kNextBatchFieldName);
        invariant(batchData);
        invariant(nss);

        BSONElement cursorElement = obj.getField(kCursorFieldName);
        if (cursorElement.eoo()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "cursor response must contain '" << kCursorFieldName <<
                          "' field: " << obj);
        }
        if (!cursorElement.isABSONObj()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "'" << kCursorFieldName << "' field must be an object: " << obj);
        }
        BSONObj cursorObj = cursorElement.Obj();

        BSONElement cursorIdElement = cursorObj.getField(kCursorIdFieldName);
        if (cursorIdElement.eoo()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "cursor response must contain '" << kCursorFieldName << "." <<
                          kCursorIdFieldName << "' field: " << obj);
        }
        if (cursorIdElement.type() != mongo::NumberLong) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "'" << kCursorFieldName << "." << kCursorIdFieldName <<
                          "' field must be a number of type 'long': " << obj);
        }
        batchData->cursorId = cursorIdElement.numberLong();

        BSONElement namespaceElement = cursorObj.getField(kNamespaceFieldName);
        if (namespaceElement.eoo()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "cursor response must contain " <<
                          "'" << kCursorFieldName << "." << kNamespaceFieldName << "' field: " <<
                          obj);
        }
        if (namespaceElement.type() != mongo::String) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "'" << kCursorFieldName << "." << kNamespaceFieldName <<
                          "' field must be a string: " << obj);
        }
        NamespaceString tempNss(namespaceElement.valuestrsafe());
        if (!tempNss.isValid()) {
            return Status(ErrorCodes::BadValue, str::stream() <<
                          "'" << kCursorFieldName << "." << kNamespaceFieldName <<
                          "' contains an invalid namespace: " << obj);
        }
        *nss = tempNss;

        BSONElement batchElement = cursorObj.getField(batchFieldName);
        if (batchElement.eoo()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "cursor response must contain '" << kCursorFieldName << "." <<
                          batchFieldName << "' field: " << obj);
        }
        if (!batchElement.isABSONObj()) {
            return Status(ErrorCodes::FailedToParse, str::stream() <<
                          "'" << kCursorFieldName << "." << batchFieldName <<
                          "' field must be an array: " << obj);
        }
        BSONObj batchObj = batchElement.Obj();
        for (auto itemElement : batchObj) {
            if (!itemElement.isABSONObj()) {
                return Status(ErrorCodes::FailedToParse, str::stream() <<
                              "found non-object " << itemElement << " in " <<
                              "'" << kCursorFieldName << "." << batchFieldName << "' field: " <<
                              obj);
            }
            batchData->documents.push_back(itemElement.Obj().getOwned());
        }

        return Status::OK();
    }

} // namespace

    Fetcher::BatchData::BatchData() : cursorId(0), documents() { }

    Fetcher::BatchData::BatchData(CursorId theCursorId, Documents theDocuments)
        : cursorId(theCursorId),
          documents(theDocuments) { }

    Fetcher::Fetcher(ReplicationExecutor* executor,
                     const HostAndPort& source,
                     const std::string& dbname,
                     const BSONObj& findCmdObj,
                     const CallbackFn& work)
        : _executor(executor),
          _source(source),
          _dbname(dbname),
          _cmdObj(findCmdObj.getOwned()),
          _work(work),
          _active(false),
          _remoteCommandCallbackHandle() {

        uassert(ErrorCodes::BadValue, "null replication executor", executor);
        uassert(ErrorCodes::BadValue, "database name cannot be empty", !dbname.empty());
        uassert(ErrorCodes::BadValue, "command object cannot be empty", !findCmdObj.isEmpty());
        uassert(ErrorCodes::BadValue, "callback function cannot be null", work);
    }

    Fetcher::~Fetcher() { }

    std::string Fetcher::getDiagnosticString() const {
        boost::lock_guard<boost::mutex> lk(_mutex);
        str::stream output;
        output << "Fetcher";
        output << " executor: " << _executor->getDiagnosticString();
        output << " source: " << _source.toString();
        output << " database: " << _dbname;
        output << " query: " << _cmdObj;
        output << " active: " << _active;
        return output;
    }

    bool Fetcher::isActive() const {
        boost::lock_guard<boost::mutex> lk(_mutex);
        return _active;
    }

    Status Fetcher::schedule() {
        boost::lock_guard<boost::mutex> lk(_mutex);
        return _schedule_inlock(_cmdObj, kFirstBatchFieldName);
    }

    void Fetcher::cancel() {
        ReplicationExecutor::CallbackHandle remoteCommandCallbackHandle;
        {
            boost::lock_guard<boost::mutex> lk(_mutex);

            if (!_active) {
                return;
            }

            remoteCommandCallbackHandle = _remoteCommandCallbackHandle;
        }

        invariant(remoteCommandCallbackHandle.isValid());
        _executor->cancel(remoteCommandCallbackHandle);
    }

    void Fetcher::wait() {
        ReplicationExecutor::CallbackHandle remoteCommandCallbackHandle;
        {
            boost::lock_guard<boost::mutex> lk(_mutex);

            if (!_active) {
                return;
            }

            remoteCommandCallbackHandle = _remoteCommandCallbackHandle;
        }

        invariant(remoteCommandCallbackHandle.isValid());
        _executor->wait(remoteCommandCallbackHandle);
    }

    Status Fetcher::_schedule_inlock(const BSONObj& cmdObj, const char* batchFieldName) {
        if (_active) {
            return Status(ErrorCodes::IllegalOperation, "fetcher already scheduled");
        }

        StatusWith<ReplicationExecutor::CallbackHandle> scheduleResult =
            _executor->scheduleRemoteCommand(
                ReplicationExecutor::RemoteCommandRequest(_source, _dbname, cmdObj),
                stdx::bind(&Fetcher::_callback, this, stdx::placeholders::_1, batchFieldName));

        if (!scheduleResult.isOK()) {
            return scheduleResult.getStatus();
        }

        _active = true;
        _remoteCommandCallbackHandle = scheduleResult.getValue();
        return Status::OK();
    }

    void Fetcher::_callback(const ReplicationExecutor::RemoteCommandCallbackData& rcbd,
                            const char* batchFieldName) {
        boost::lock_guard<boost::mutex> lk(_mutex);
        _active = false;

        NextAction nextAction = NextAction::kNoAction;

        if (!rcbd.response.isOK()) {
            _work(StatusWith<Fetcher::BatchData>(rcbd.response.getStatus()), &nextAction);
            return;
        }

        const BSONObj& cursorReponseObj = rcbd.response.getValue().data;
        Status status = getStatusFromCommandResult(cursorReponseObj);
        if (!status.isOK()) {
            _work(StatusWith<Fetcher::BatchData>(status), &nextAction);
            return;
        }


        BatchData batchData;
        NamespaceString nss;
        status = parseCursorResponse(cursorReponseObj, batchFieldName, &batchData, &nss);
        if (!status.isOK()) {
            _work(StatusWith<Fetcher::BatchData>(status), &nextAction);
            return;
        }

        if (batchData.cursorId) {
            nextAction = NextAction::kContinue;
        }

        _work(StatusWith<BatchData>(batchData), &nextAction);

        // Callback function _work may modify nextAction to request the fetcher
        // not to schedule a getMore command.
        if (nextAction != NextAction::kContinue) {
            return;
        }

        nextAction = NextAction::kNoAction;

        if (batchData.cursorId) {
            BSONObj getMoreCmdObj = BSON("getMore" << batchData.cursorId <<
                                         "collection" << nss.coll());
            status = _schedule_inlock(getMoreCmdObj, kNextBatchFieldName);
            if (!status.isOK()) {
                _work(StatusWith<Fetcher::BatchData>(status), &nextAction);
                return;
            }
        }
    }

} // namespace repl
} // namespace mongo
