/**
 *    Copyright (C) 2016 MongoDB, Inc.
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

#pragma once

#include "mongo/platform/basic.h"

#include <boost/intrusive_ptr.hpp>
#include <memory>

#include "mongo/bson/bsonelement.h"
#include "mongo/db/pipeline/document_source_single_document_transformation.h"
#include "mongo/db/pipeline/field_path.h"

namespace mongo {

class BSONObj;
class Document;
class ExpressionContext;

namespace parsed_aggregation_projection {

/**
 * This class ensures that the specification was valid: that none of the paths specified conflict
 * with one another, that there is at least one field, etc. Here "projection" includes both
 * $project specifications and $addFields specifications.
 */
class ProjectionSpecValidator {
public:
    /**
     * Throws if the specification is not valid for a projection. The stageName is used to provide a
     * more helpful error message.
     */
    static void uassertValid(const BSONObj& spec, StringData stageName);

private:
    ProjectionSpecValidator(const BSONObj& spec) : _rawObj(spec) {}

    /**
     * Uses '_seenPaths' to see if 'path' conflicts with any paths that have already been specified.
     *
     * For example, a user is not allowed to specify {'a': 1, 'a.b': 1}, or some similar conflicting
     * paths.
     */
    void ensurePathDoesNotConflictOrThrow(StringData path);

    /**
     * Throws if an invalid projection specification is detected.
     */
    void validate();

    /**
     * Parses a single BSONElement. 'pathToElem' should include the field name of 'elem'.
     *
     * Delegates to parseSubObject() if 'elem' is an object. Otherwise adds the full path to 'elem'
     * to '_seenPaths'.
     *
     * Calls ensurePathDoesNotConflictOrThrow with the path to this element, throws on conflicting
     * path specifications.
     */
    void parseElement(const BSONElement& elem, const FieldPath& pathToElem);

    /**
     * Traverses 'thisLevelSpec', parsing each element in turn.
     *
     * Throws if any paths conflict with each other or existing paths, 'thisLevelSpec' contains a
     * dotted path, or if 'thisLevelSpec' represents an invalid expression.
     */
    void parseNestedObject(const BSONObj& thisLevelSpec, const FieldPath& prefix);

    // The original object. Used to generate more helpful error messages.
    const BSONObj& _rawObj;

    // Tracks which paths we've seen to ensure no two paths conflict with each other.
    // Can be a vector since we iterate through it.
    std::vector<std::string> _seenPaths;
};

/**
 * A ParsedAggregationProjection is responsible for parsing and executing a $project. It
 * represents either an inclusion or exclusion projection. This is the common interface between the
 * two types of projections.
 */
class ParsedAggregationProjection
    : public DocumentSourceSingleDocumentTransformation::TransformerInterface {
public:
    /**
     * Main entry point for a ParsedAggregationProjection.
     *
     * Throws a AssertionException if 'spec' is an invalid projection specification.
     */
    static std::unique_ptr<ParsedAggregationProjection> create(
        const boost::intrusive_ptr<ExpressionContext>& expCtx, const BSONObj& spec);

    virtual ~ParsedAggregationProjection() = default;

    /**
     * Parse the user-specified BSON object 'spec'. By the time this is called, 'spec' has already
     * been verified to not have any conflicting path specifications, and not to mix and match
     * inclusions and exclusions. 'variablesParseState' is used by any contained expressions to
     * track which variables are defined so that they can later be referenced at execution time.
     */
    virtual void parse(const BSONObj& spec) = 0;

    /**
     * Optimize any expressions contained within this projection.
     */
    virtual void optimize() {}

    /**
     * Add any dependencies needed by this projection or any sub-expressions to 'deps'.
     */
    virtual DocumentSource::GetDepsReturn addDependencies(DepsTracker* deps) const {
        return DocumentSource::NOT_SUPPORTED;
    }

    /**
     * Apply the projection transformation.
     */
    Document applyTransformation(const Document& input) {
        return applyProjection(input);
    }

protected:
    ParsedAggregationProjection(const boost::intrusive_ptr<ExpressionContext>& expCtx)
        : _expCtx(expCtx){};

    /**
     * Apply the projection to 'input'.
     */
    virtual Document applyProjection(const Document& input) const = 0;

    boost::intrusive_ptr<ExpressionContext> _expCtx;
};
}  // namespace parsed_aggregation_projection
}  // namespace mongo
