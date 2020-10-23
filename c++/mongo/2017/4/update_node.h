/**
 * Copyright (C) 2017 MongoDB Inc.
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

namespace mongo {

class CollatorInterface;

/**
 * Update modifier expressions are stored as a prefix tree of UpdateNodes, where two modifiers that
 * share a field path prefix share a path prefix in the tree. The prefix tree is used to enforce
 * that no update modifier's field path is a prefix of (or equal to) another update modifier's field
 * path. The root of the UpdateNode tree is always an UpdateObjectNode. The leaves are always
 * UpdateLeafNodes.
 *
 * Example: {$set: {'a.b': 5, c: 6}, $inc: {'a.c': 1}}
 *
 *                      UpdateObjectNode
 *                         a /    \ c
 *            UpdateObjectNode    SetNode: _val = 6
 *               b /    \ c
 * SetNode: _val = 5    IncNode: _val = 1
 */
class UpdateNode {
public:
    enum class Type { Object, Leaf };

    explicit UpdateNode(Type type) : type(type) {}
    virtual ~UpdateNode() = default;

    /**
     * Set the collation on the node and all descendants. This is a noop if no leaf nodes require a
     * collator. If setCollator() is called, it is required that the current collator of all leaf
     * nodes is the simple collator (nullptr). The collator must outlive the modifier interface.
     * This is used to override the collation after obtaining a collection lock if the update did
     * not specify a collation and the collection has a non-simple default collation.
     */
    virtual void setCollator(const CollatorInterface* collator) = 0;

    const Type type;
};

}  // namespace mongo
