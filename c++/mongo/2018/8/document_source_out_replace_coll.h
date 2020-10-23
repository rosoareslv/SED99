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

#pragma once

#include "mongo/db/pipeline/document_source_out.h"
#include "mongo/util/destructor_guard.h"

namespace mongo {

/**
 * Version of $out which directs writes to a temporary collection, then renames the temp collection
 * to the target collection with the 'dropTarget' option set to true.
 */
class DocumentSourceOutReplaceColl final : public DocumentSourceOut {
public:
    using DocumentSourceOut::DocumentSourceOut;

    ~DocumentSourceOutReplaceColl() {
        DESTRUCTOR_GUARD(
            // Make sure we drop the temp collection if anything goes wrong. Errors are ignored
            // here because nothing can be done about them. Additionally, if this fails and the
            // collection is left behind, it will be cleaned up next time the server is started.
            if (_tempNs.size()) {
                pExpCtx->mongoProcessInterface->directClient()->dropCollection(_tempNs.ns());
            });
    }

    /**
     * Sets up a temp collection which contains the same indexes and options as the output
     * collection. All writes will be directed to the temp collection.
     */
    void initializeWriteNs() final;

    /**
     * Renames the temp collection to the output collection with the 'dropTarget' option set to
     * true.
     */
    void finalize() final;

    const NamespaceString& getWriteNs() const final {
        return _tempNs;
    };

private:
    // Holds on to the original collection options and index specs so we can check they didn't
    // change during computation.
    BSONObj _originalOutOptions;
    std::list<BSONObj> _originalIndexes;

    // The temporary namespace for the $out writes.
    NamespaceString _tempNs;
};

}  // namespace mongo
