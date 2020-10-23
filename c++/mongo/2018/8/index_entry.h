/**
 *    Copyright (C) 2013 10gen Inc.
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

#include <string>

#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/index_names.h"
#include "mongo/db/jsobj.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

class CollatorInterface;
class MatchExpression;

/**
 * This name sucks, but every name involving 'index' is used somewhere.
 */
struct IndexEntry {

    /**
     * This struct is used to uniquely identify an index. The index "Identifier" has two
     * components: catalog name, and "disambiguator". The catalog name is just the name of the
     * index in the catalog. The disambiguator is used by the planner when multiple IndexEntries
     * may refer to the same underlying index in the catalog. This can only happen with $**
     * indices. Otherwise, the disambiguator should be empty.
     *
     * Has the same comparison and equality semantics as std::pair<string, string>.
     *
     */
    struct Identifier {
        explicit Identifier(std::string aCatalogName) : catalogName(std::move(aCatalogName)) {}

        Identifier(std::string aCatalogName, std::string nameDisambiguator)
            : catalogName(std::move(aCatalogName)), disambiguator(std::move(nameDisambiguator)) {}

        bool operator==(const Identifier& other) const {
            return other.catalogName == catalogName && other.disambiguator == disambiguator;
        }

        bool operator!=(const Identifier& other) const {
            return !(*this == other);
        }

        bool operator<(const Identifier& other) const {
            const auto cmpRes = catalogName.compare(other.catalogName);
            if (cmpRes != 0) {
                return cmpRes < 0;
            }
            return disambiguator < other.disambiguator;
        }

        std::string toString() const {
            return "(" + catalogName + ", " + disambiguator + ")";
        }

        // The name of the index in the catalog.
        std::string catalogName;

        // A string used for disambiguating multiple IndexEntries with the same catalogName (such
        // as in the case with an allPaths index).
        std::string disambiguator;
    };

    /**
     * Use this constructor if you're making an IndexEntry from the catalog.
     */
    IndexEntry(const BSONObj& kp,
               const std::string& accessMethod,
               bool mk,
               const MultikeyPaths& mkp,
               bool sp,
               bool unq,
               Identifier ident,
               const MatchExpression* fe,
               const BSONObj& io,
               const CollatorInterface* ci)
        : keyPattern(kp),
          multikey(mk),
          multikeyPaths(mkp),
          sparse(sp),
          unique(unq),
          identifier(std::move(ident)),
          filterExpr(fe),
          infoObj(io),
          collator(ci) {
        type = IndexNames::nameToType(accessMethod);
    }

    /**
     * For testing purposes only.
     */
    IndexEntry(const BSONObj& kp,
               bool mk,
               bool sp,
               bool unq,
               Identifier ident,
               const MatchExpression* fe,
               const BSONObj& io)
        : keyPattern(kp),
          multikey(mk),
          sparse(sp),
          unique(unq),
          identifier(std::move(ident)),
          filterExpr(fe),
          infoObj(io) {
        type = IndexNames::nameToType(IndexNames::findPluginName(keyPattern));
    }

    /**
     * For testing purposes only.
     */
    IndexEntry(const BSONObj& kp, const std::string& indexName = "test_foo")
        : keyPattern(kp),
          multikey(false),
          sparse(false),
          unique(false),
          identifier(indexName),
          filterExpr(nullptr),
          infoObj(BSONObj()) {
        type = IndexNames::nameToType(IndexNames::findPluginName(keyPattern));
    }

    /**
     * Returns true if 'indexedField' has any multikey components. For example, returns true if this
     * index has a multikey component "a", and 'indexedField' is "a.b". Illegal to call unless
     * 'indexedField' is present in this index's key pattern.
     *
     * For indexes created on older versions we may not have path-level multikey information. In
     * these cases we only have a single boolean to track whether any path in the index is multikey.
     * If this is the case we defensively return true for any path.
     */
    bool pathHasMultikeyComponent(StringData indexedField) const;

    bool operator==(const IndexEntry& rhs) const {
        // Indexes are logically equal when names are equal.
        return this->identifier == rhs.identifier;
    }

    std::string toString() const;

    BSONObj keyPattern;

    bool multikey;

    // If non-empty, 'multikeyPaths' is a vector with size equal to the number of elements in the
    // index key pattern. Each element in the vector is an ordered set of positions (starting at 0)
    // into the corresponding indexed field that represent what prefixes of the indexed field cause
    // the index to be multikey.
    MultikeyPaths multikeyPaths;

    bool sparse;

    bool unique;

    Identifier identifier;

    const MatchExpression* filterExpr;

    // Geo indices have extra parameters.  We need those available to plan correctly.
    BSONObj infoObj;

    // What type of index is this?  (What access method can we use on the index described
    // by the keyPattern?)
    IndexType type;

    // Null if this index orders strings according to the simple binary compare. If non-null,
    // represents the collator used to generate index keys for indexed strings.
    const CollatorInterface* collator = nullptr;
};

std::ostream& operator<<(std::ostream& stream, const IndexEntry::Identifier& ident);
StringBuilder& operator<<(StringBuilder& builder, const IndexEntry::Identifier& ident);
}  // namespace mongo
