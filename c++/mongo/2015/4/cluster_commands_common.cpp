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

#include "mongo/s/commands/cluster_commands_common.h"

namespace mongo {

    int getUniqueCodeFromCommandResults(const std::vector<Strategy::CommandResult>& results) {
        int commonErrCode = -1;
        for (std::vector<Strategy::CommandResult>::const_iterator it = results.begin();
             it != results.end();
             ++it) {

            // Only look at shards with errors.
            if (!it->result["ok"].trueValue()) {
                int errCode = it->result["code"].numberInt();

                if (commonErrCode == -1) {
                    commonErrCode = errCode;
                }
                else if (commonErrCode != errCode) {
                    // At least two shards with errors disagree on the error code
                    commonErrCode = 0;
                }
            }
        }

        // If no error encountered or shards with errors disagree on the error code, return 0
        if (commonErrCode == -1 || commonErrCode == 0) {
            return 0;
        }

        // Otherwise, shards with errors agree on the error code; return that code
        return commonErrCode;
    }

} // namespace mongo
