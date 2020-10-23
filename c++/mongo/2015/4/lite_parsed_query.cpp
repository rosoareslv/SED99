/**
 *    Copyright 2013 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/db/query/lite_parsed_query.h"

#include <cmath>

#include "mongo/db/dbmessage.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    using std::auto_ptr;
    using std::string;

    const string LiteParsedQuery::cmdOptionMaxTimeMS("maxTimeMS");
    const string LiteParsedQuery::queryOptionMaxTimeMS("$maxTimeMS");

    const string LiteParsedQuery::metaTextScore("textScore");
    const string LiteParsedQuery::metaGeoNearDistance("geoNearDistance");
    const string LiteParsedQuery::metaGeoNearPoint("geoNearPoint");
    const string LiteParsedQuery::metaRecordId("recordId");
    const string LiteParsedQuery::metaIndexKey("indexKey");

    namespace {

        Status checkFieldType(const BSONElement& el, BSONType type) {
            if (type != el.type()) {
                mongoutils::str::stream ss;
                ss << "Failed to parse: " << el.toString() << ". "
                   << "'" << el.fieldName() << "' field must be of BSON type "
                   << typeName(type) << ".";
                return Status(ErrorCodes::FailedToParse, ss);
            }

            return Status::OK();
        }

    } // namespace

    // static
    Status LiteParsedQuery::make(const std::string& fullns,
                                 const BSONObj& cmdObj,
                                 bool isExplain,
                                 LiteParsedQuery** out) {
        auto_ptr<LiteParsedQuery> pq(new LiteParsedQuery());
        pq->_ns = fullns;
        pq->_explain = isExplain;

        // Parse the command BSON by looping through one element at a time.
        BSONObjIterator it(cmdObj);
        while (it.more()) {
            BSONElement el = it.next();
            const char* fieldName = el.fieldName();
            if (mongoutils::str::equals(fieldName, "find")) {
                // We've already parsed the namespace information contained in the 'find'
                // field, so just move on.
                continue;
            }
            else if (mongoutils::str::equals(fieldName, "filter")) {
                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                pq->_filter = el.Obj().getOwned();
            }
            else if (mongoutils::str::equals(fieldName, "projection")) {
                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                pq->_proj = el.Obj().getOwned();
            }
            else if (mongoutils::str::equals(fieldName, "sort")) {
                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                // Sort document normalization.
                BSONObj sort = el.Obj().getOwned();
                if (!isValidSortOrder(sort)) {
                    return Status(ErrorCodes::BadValue, "bad sort specification");
                }

                pq->_sort = sort;
            }
            else if (mongoutils::str::equals(fieldName, "hint")) {
                BSONObj hintObj;
                if (Object == el.type()) {
                    hintObj = cmdObj["hint"].Obj().getOwned();
                }
                else if (String == el.type()) {
                    hintObj = el.wrap("$hint");
                }
                else {
                    return Status(ErrorCodes::FailedToParse,
                                  "hint must be either a string or nested object");
                }

                pq->_hint = hintObj;
            }
            else if (mongoutils::str::equals(fieldName, "skip")) {
                if (!el.isNumber()) {
                    mongoutils::str::stream ss;
                    ss << "Failed to parse: " << cmdObj.toString() << ". "
                       << "'skip' field must be numeric.";
                    return Status(ErrorCodes::FailedToParse, ss);
                }

                int skip = el.numberInt();
                if (skip < 0) {
                    return Status(ErrorCodes::BadValue, "skip value must be non-negative");
                }

                pq->_skip = skip;
            }
            else if (mongoutils::str::equals(fieldName, "limit")) {
                if (!el.isNumber()) {
                    mongoutils::str::stream ss;
                    ss << "Failed to parse: " << cmdObj.toString() << ". "
                       << "'limit' field must be numeric.";
                    return Status(ErrorCodes::FailedToParse, ss);
                }

                int limit = el.numberInt();
                if (limit < 0) {
                    return Status(ErrorCodes::BadValue, "limit value must be non-negative");
                }

                pq->_limit = limit;
            }
            else if (mongoutils::str::equals(fieldName, "batchSize")) {
                if (!el.isNumber()) {
                    mongoutils::str::stream ss;
                    ss << "Failed to parse: " << cmdObj.toString() << ". "
                       << "'batchSize' field must be numeric.";
                    return Status(ErrorCodes::FailedToParse, ss);
                }

                int batchSize = el.numberInt();
                if (batchSize <= 0) {
                    return Status(ErrorCodes::BadValue, "batchSize value must be positive");
                }

                pq->_batchSize = batchSize;
            }
            else if (mongoutils::str::equals(fieldName, "singleBatch")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_wantMore = !el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "comment")) {
                Status status = checkFieldType(el, String);
                if (!status.isOK()) {
                    return status;
                }

                pq->_comment = el.str();
            }
            else if (mongoutils::str::equals(fieldName, "maxScan")) {
                if (!el.isNumber()) {
                    mongoutils::str::stream ss;
                    ss << "Failed to parse: " << cmdObj.toString() << ". "
                       << "'maxScan' field must be numeric.";
                    return Status(ErrorCodes::FailedToParse, ss);
                }

                int maxScan = el.numberInt();
                if (maxScan < 0) {
                    return Status(ErrorCodes::BadValue, "maxScan value must be non-negative");
                }

                pq->_maxScan = maxScan;
            }
            else if (mongoutils::str::equals(fieldName, cmdOptionMaxTimeMS.c_str())) {
                StatusWith<int> maxTimeMS = parseMaxTimeMS(el);
                if (!maxTimeMS.isOK()) {
                    return maxTimeMS.getStatus();
                }

                pq->_maxTimeMS = maxTimeMS.getValue();
            }
            else if (mongoutils::str::equals(fieldName, "min")) {
                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                pq->_min = el.Obj().getOwned();
            }
            else if (mongoutils::str::equals(fieldName, "max")) {
                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                pq->_max = el.Obj().getOwned();
            }
            else if (mongoutils::str::equals(fieldName, "returnKey")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_returnKey = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "showRecordId")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_showRecordId = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "snapshot")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_snapshot = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "$readPreference")) {
                pq->_hasReadPref = true;
            }
            else if (mongoutils::str::equals(fieldName, "tailable")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_tailable = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "slaveOk")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_slaveOk = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "oplogReplay")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_oplogReplay = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "noCursorTimeout")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_noCursorTimeout = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "awaitData")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_awaitData = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "partial")) {
                Status status = checkFieldType(el, Bool);
                if (!status.isOK()) {
                    return status;
                }

                pq->_partial = el.boolean();
            }
            else if (mongoutils::str::equals(fieldName, "options")) {
                // 3.0.x versions of the shell may generate an explain of a find command with an
                // 'options' field. We accept this only if the 'options' field is empty so that
                // the shell's explain implementation is forwards compatible.
                //
                // TODO: Remove for 3.4.
                if (!pq->isExplain()) {
                    return Status(ErrorCodes::FailedToParse,
                                  "Field 'options' is only allowed for explain.");
                }

                Status status = checkFieldType(el, Object);
                if (!status.isOK()) {
                    return status;
                }

                BSONObj optionsObj = el.Obj();
                if (!optionsObj.isEmpty()) {
                    return Status(ErrorCodes::FailedToParse,
                                  str::stream() << "Failed to parse options: "
                                                << optionsObj.toString() << ". "
                                                << "You may need to update your shell or driver.");
                }
            }
            else {
                mongoutils::str::stream ss;
                ss << "Failed to parse: " << cmdObj.toString() << ". "
                   << "Unrecognized field '" << fieldName << "'.";
                return Status(ErrorCodes::FailedToParse, ss);
            }
        }

        // We might need to update the projection object with a $meta projection.
        if (pq->returnKey()) {
            pq->addReturnKeyMetaProj();
        }
        if (pq->showRecordId()) {
            pq->addShowRecordIdMetaProj();
        }

        Status validateStatus = pq->validate();
        if (!validateStatus.isOK()) {
            return validateStatus;
        }

        *out = pq.release();
        return Status::OK();
    }

    void LiteParsedQuery::addReturnKeyMetaProj() {
        BSONObjBuilder projBob;
        projBob.appendElements(_proj);
        // We use $$ because it's never going to show up in a user's projection.
        // The exact text doesn't matter.
        BSONObj indexKey = BSON("$$" <<
                                BSON("$meta" << LiteParsedQuery::metaIndexKey));
        projBob.append(indexKey.firstElement());
        _proj = projBob.obj();
    }

    void LiteParsedQuery::addShowRecordIdMetaProj() {
        BSONObjBuilder projBob;
        projBob.appendElements(_proj);
        BSONObj metaRecordId = BSON("$recordId" <<
                                    BSON("$meta" << LiteParsedQuery::metaRecordId));
        projBob.append(metaRecordId.firstElement());
        _proj = projBob.obj();
    }

    Status LiteParsedQuery::validate() const {
        // Min and Max objects must have the same fields.
        if (!_min.isEmpty() && !_max.isEmpty()) {
            if (!_min.isFieldNamePrefixOf(_max) ||
                (_min.nFields() != _max.nFields())) {
                return Status(ErrorCodes::BadValue, "min and max must have the same field names");
            }
        }

        // Can't combine a normal sort and a $meta projection on the same field.
        BSONObjIterator projIt(_proj);
        while (projIt.more()) {
            BSONElement projElt = projIt.next();
            if (isTextScoreMeta(projElt)) {
                BSONElement sortElt = _sort[projElt.fieldName()];
                if (!sortElt.eoo() && !isTextScoreMeta(sortElt)) {
                    return Status(ErrorCodes::BadValue,
                                  "can't have a non-$meta sort on a $meta projection");
                }
            }
        }

        // All fields with a $meta sort must have a corresponding $meta projection.
        BSONObjIterator sortIt(_sort);
        while (sortIt.more()) {
            BSONElement sortElt = sortIt.next();
            if (isTextScoreMeta(sortElt)) {
                BSONElement projElt = _proj[sortElt.fieldName()];
                if (projElt.eoo() || !isTextScoreMeta(projElt)) {
                    return Status(ErrorCodes::BadValue,
                                  "must have $meta projection for all $meta sort keys");
                }
            }
        }

        if (_snapshot) {
            if (!_sort.isEmpty()) {
                return Status(ErrorCodes::BadValue, "E12001 can't use sort with $snapshot");
            }
            if (!_hint.isEmpty()) {
                return Status(ErrorCodes::BadValue, "E12002 can't use hint with $snapshot");
            }
        }

        return Status::OK();
    }

    // static
    StatusWith<int> LiteParsedQuery::parseMaxTimeMSCommand(const BSONObj& cmdObj) {
        return parseMaxTimeMS(cmdObj[cmdOptionMaxTimeMS]);
    }

    // static
    StatusWith<int> LiteParsedQuery::parseMaxTimeMSQuery(const BSONObj& queryObj) {
        return parseMaxTimeMS(queryObj[queryOptionMaxTimeMS]);
    }

    // static
    StatusWith<int> LiteParsedQuery::parseMaxTimeMS(const BSONElement& maxTimeMSElt) {
        if (!maxTimeMSElt.eoo() && !maxTimeMSElt.isNumber()) {
            return StatusWith<int>(ErrorCodes::BadValue,
                                   (StringBuilder()
                                       << maxTimeMSElt.fieldNameStringData()
                                       << " must be a number").str());
        }
        long long maxTimeMSLongLong = maxTimeMSElt.safeNumberLong(); // returns 0 on EOO
        if (maxTimeMSLongLong < 0 || maxTimeMSLongLong > INT_MAX) {
            return StatusWith<int>(ErrorCodes::BadValue,
                                   (StringBuilder()
                                       << maxTimeMSElt.fieldNameStringData()
                                       << " is out of range").str());
        }
        double maxTimeMSDouble = maxTimeMSElt.numberDouble();
        if (maxTimeMSElt.type() == mongo::NumberDouble
            && floor(maxTimeMSDouble) != maxTimeMSDouble) {
            return StatusWith<int>(ErrorCodes::BadValue,
                                   (StringBuilder()
                                       << maxTimeMSElt.fieldNameStringData()
                                       << " has non-integral value").str());
        }
        return StatusWith<int>(static_cast<int>(maxTimeMSLongLong));
    }

    // static
    bool LiteParsedQuery::isTextScoreMeta(BSONElement elt) {
        // elt must be foo: {$meta: "textScore"}
        if (mongo::Object != elt.type()) {
            return false;
        }
        BSONObj metaObj = elt.Obj();
        BSONObjIterator metaIt(metaObj);
        // must have exactly 1 element
        if (!metaIt.more()) {
            return false;
        }
        BSONElement metaElt = metaIt.next();
        if (!mongoutils::str::equals("$meta", metaElt.fieldName())) {
            return false;
        }
        if (mongo::String != metaElt.type()) {
            return false;
        }
        if (LiteParsedQuery::metaTextScore != metaElt.valuestr()) {
            return false;
        }
        // must have exactly 1 element
        if (metaIt.more()) {
            return false;
        }
        return true;
    }

    // static
    bool LiteParsedQuery::isRecordIdMeta(BSONElement elt) {
        // elt must be foo: {$meta: "recordId"}
        if (mongo::Object != elt.type()) {
            return false;
        }
        BSONObj metaObj = elt.Obj();
        BSONObjIterator metaIt(metaObj);
        // must have exactly 1 element
        if (!metaIt.more()) {
            return false;
        }
        BSONElement metaElt = metaIt.next();
        if (!mongoutils::str::equals("$meta", metaElt.fieldName())) {
            return false;
        }
        if (mongo::String != metaElt.type()) {
            return false;
        }
        if (LiteParsedQuery::metaRecordId != metaElt.valuestr()) {
            return false;
        }
        // must have exactly 1 element
        if (metaIt.more()) {
            return false;
        }
        return true;
    }

    // static
    bool LiteParsedQuery::isValidSortOrder(const BSONObj& sortObj) {
        BSONObjIterator i(sortObj);
        while (i.more()) {
            BSONElement e = i.next();
            // fieldNameSize() includes NULL terminator. For empty field name,
            // we should be checking for 1 instead of 0.
            if (1 == e.fieldNameSize()) {
                return false;
            }
            if (isTextScoreMeta(e)) {
                continue;
            }
            long long n = e.safeNumberLong();
            if (!(e.isNumber() && (n == -1LL || n == 1LL))) {
                return false;
            }
        }
        return true;
    }

    // static
    bool LiteParsedQuery::isQueryIsolated(const BSONObj& query) {
        BSONObjIterator iter(query);
        while (iter.more()) {
            BSONElement elt = iter.next();
            if (str::equals(elt.fieldName(), "$isolated") && elt.trueValue())
                return true;
            if (str::equals(elt.fieldName(), "$atomic") && elt.trueValue())
                return true;
        }
        return false;
    }

    LiteParsedQuery::LiteParsedQuery() :
        _skip(0),
        _limit(0),
        _batchSize(101),
        _wantMore(true),
        _explain(false),
        _maxScan(0),
        _maxTimeMS(0),
        _returnKey(false),
        _showRecordId(false),
        _snapshot(false),
        _hasReadPref(false),
        _tailable(false),
        _slaveOk(false),
        _oplogReplay(false),
        _noCursorTimeout(false),
        _awaitData(false),
        _exhaust(false),
        _partial(false) { }

    //
    // Old LiteParsedQuery parsing code: SOON TO BE DEPRECATED.
    //

    // static
    Status LiteParsedQuery::make(const QueryMessage& qm, LiteParsedQuery** out) {
        auto_ptr<LiteParsedQuery> pq(new LiteParsedQuery());

        Status status = pq->init(qm.ns, qm.ntoskip, qm.ntoreturn, qm.queryOptions, qm.query,
                                 qm.fields, true);
        if (status.isOK()) { *out = pq.release(); }
        return status;
    }

    // static
    Status LiteParsedQuery::make(const string& ns, int ntoskip, int ntoreturn, int queryOptions,
                                 const BSONObj& query, const BSONObj& proj, const BSONObj& sort,
                                 const BSONObj& hint,
                                 const BSONObj& minObj, const BSONObj& maxObj,
                                 bool snapshot,
                                 bool explain,
                                 LiteParsedQuery** out) {
        auto_ptr<LiteParsedQuery> pq(new LiteParsedQuery());
        pq->_sort = sort.getOwned();
        pq->_hint = hint.getOwned();
        pq->_min = minObj.getOwned();
        pq->_max = maxObj.getOwned();
        pq->_snapshot = snapshot;
        pq->_explain = explain;

        Status status = pq->init(ns, ntoskip, ntoreturn, queryOptions, query, proj, false);
        if (status.isOK()) { *out = pq.release(); }
        return status;
    }

    Status LiteParsedQuery::init(const string& ns, int ntoskip, int ntoreturn, int queryOptions,
                                 const BSONObj& queryObj, const BSONObj& proj,
                                 bool fromQueryMessage) {
        _ns = ns;
        _skip = ntoskip;
        _limit = ntoreturn;
        _proj = proj.getOwned();

        // Initialize flags passed as 'queryOptions' bit vector.
        initFromInt(queryOptions);

        if (_skip < 0) {
            return Status(ErrorCodes::BadValue, "bad skip value in query");
        }

        if (_limit == std::numeric_limits<int>::min()) {
            // _limit is negative but can't be negated.
            return Status(ErrorCodes::BadValue, "bad limit value in query");
        }

        if (_limit < 0) {
            // _limit greater than zero is simply a hint on how many objects to send back per
            // "cursor batch".  A negative number indicates a hard limit.
            _wantMore = false;
            _limit = -_limit;
        }

        // We are constructing this LiteParsedQuery from a legacy OP_QUERY message, and therefore
        // cannot distinguish batchSize and limit. (They are a single field in OP_QUERY, but are
        // passed separately for the find command.) Just set both values to be the same.
        _batchSize = _limit;

        if (fromQueryMessage) {
            BSONElement queryField = queryObj["query"];
            if (!queryField.isABSONObj()) { queryField = queryObj["$query"]; }
            if (queryField.isABSONObj()) {
                _filter = queryField.embeddedObject().getOwned();
                Status status = initFullQuery(queryObj);
                if (!status.isOK()) { return status; }
            }
            else {
                // TODO: Does this ever happen?
                _filter = queryObj.getOwned();
            }
        }
        else {
            // This is the debugging code path.
            _filter = queryObj.getOwned();
        }

        _hasReadPref = queryObj.hasField("$readPreference");

        if (!isValidSortOrder(_sort)) {
            return Status(ErrorCodes::BadValue, "bad sort specification");
        }

        return validate();
    }

    Status LiteParsedQuery::initFullQuery(const BSONObj& top) {
        BSONObjIterator i(top);

        while (i.more()) {
            BSONElement e = i.next();
            const char* name = e.fieldName();

            if (0 == strcmp("$orderby", name) || 0 == strcmp("orderby", name)) {
                if (Object == e.type()) {
                    _sort = e.embeddedObject().getOwned();
                }
                else if (Array == e.type()) {
                    _sort = e.embeddedObject();

                    // TODO: Is this ever used?  I don't think so.
                    // Quote:
                    // This is for languages whose "objects" are not well ordered (JSON is well
                    // ordered).
                    // [ { a : ... } , { b : ... } ] -> { a : ..., b : ... }
                    // note: this is slow, but that is ok as order will have very few pieces
                    BSONObjBuilder b;
                    char p[2] = "0";

                    while (1) {
                        BSONObj j = _sort.getObjectField(p);
                        if (j.isEmpty()) { break; }
                        BSONElement e = j.firstElement();
                        if (e.eoo()) {
                            return Status(ErrorCodes::BadValue, "bad order array");
                        }
                        if (!e.isNumber()) {
                            return Status(ErrorCodes::BadValue, "bad order array [2]");
                        }
                        b.append(e);
                        (*p)++;
                        if (!(*p <= '9')) {
                            return Status(ErrorCodes::BadValue, "too many ordering elements");
                        }
                    }

                    _sort = b.obj();
                }
                else {
                    return Status(ErrorCodes::BadValue, "sort must be object or array");
                }
            }
            else if ('$' == *name) {
                name++;
                if (str::equals("explain", name)) {
                    // Won't throw.
                    _explain = e.trueValue();
                }
                else if (str::equals("snapshot", name)) {
                    // Won't throw.
                    _snapshot = e.trueValue();
                }
                else if (str::equals("min", name)) {
                    if (!e.isABSONObj()) {
                        return Status(ErrorCodes::BadValue, "$min must be a BSONObj");
                    }
                    _min = e.embeddedObject().getOwned();
                }
                else if (str::equals("max", name)) {
                    if (!e.isABSONObj()) {
                        return Status(ErrorCodes::BadValue, "$max must be a BSONObj");
                    }
                    _max = e.embeddedObject().getOwned();
                }
                else if (str::equals("hint", name)) {
                    if (e.isABSONObj()) {
                        _hint = e.embeddedObject().getOwned();
                    }
                    else if (String == e.type()) {
                        _hint = e.wrap();
                    }
                    else {
                        return Status(ErrorCodes::BadValue,
                                      "$hint must be either a string or nested object");
                    }
                }
                else if (str::equals("returnKey", name)) {
                    // Won't throw.
                    if (e.trueValue()) {
                        _returnKey = true;
                        addReturnKeyMetaProj();
                    }
                }
                else if (str::equals("maxScan", name)) {
                    // Won't throw.
                    _maxScan = e.numberInt();
                }
                else if (str::equals("showDiskLoc", name)) {
                    // Won't throw.
                    if (e.trueValue()) {
                        _showRecordId = true;
                        addShowRecordIdMetaProj();
                    }
                }
                else if (str::equals("maxTimeMS", name)) {
                    StatusWith<int> maxTimeMS = parseMaxTimeMS(e);
                    if (!maxTimeMS.isOK()) {
                        return maxTimeMS.getStatus();
                    }
                    _maxTimeMS = maxTimeMS.getValue();
                }
            }
        }

        return Status::OK();
    }

    int LiteParsedQuery::getOptions() const {
        int options = 0;
        if (_tailable) { options |= QueryOption_CursorTailable; }
        if (_slaveOk) { options |= QueryOption_SlaveOk; }
        if (_oplogReplay) { options |= QueryOption_OplogReplay; }
        if (_noCursorTimeout) { options |= QueryOption_NoCursorTimeout; }
        if (_awaitData) { options |= QueryOption_AwaitData; }
        if (_exhaust) { options |= QueryOption_Exhaust; }
        if (_partial) { options |= QueryOption_PartialResults; }
        return options;
    }

    void LiteParsedQuery::initFromInt(int options) {
        _tailable = (options & QueryOption_CursorTailable) != 0;
        _slaveOk = (options & QueryOption_SlaveOk) != 0;
        _oplogReplay = (options & QueryOption_OplogReplay) != 0;
        _noCursorTimeout = (options & QueryOption_NoCursorTimeout) != 0;
        _awaitData = (options & QueryOption_AwaitData) != 0;
        _exhaust = (options & QueryOption_Exhaust) != 0;
        _partial = (options & QueryOption_PartialResults) != 0;
    }

} // namespace mongo
