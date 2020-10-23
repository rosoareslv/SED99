/**
 *    Copyright (C) 2012 10gen Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/db/s/collection_metadata.h"

#include "mongo/bson/simple_bsonobj_comparator.h"
#include "mongo/bson/util/builder.h"
#include "mongo/s/catalog/type_chunk.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

CollectionMetadata::CollectionMetadata(std::shared_ptr<ChunkManager> cm, const ShardId& thisShardId)
    : _cm(std::move(cm)),
      _thisShardId(thisShardId),
      _shardVersion(_cm->getVersion(_thisShardId)),
      _chunksMap(SimpleBSONObjComparator::kInstance.makeBSONObjIndexedMap<BSONObj>()),
      _rangesMap(SimpleBSONObjComparator::kInstance.makeBSONObjIndexedMap<BSONObj>()) {

    invariant(_cm->getVersion().isSet());
    invariant(_cm->getVersion() >= _shardVersion);

    for (const auto& chunk : _cm->chunks()) {
        if (chunk->getShardId() != _thisShardId)
            continue;

        _chunksMap.emplace_hint(_chunksMap.end(), chunk->getMin(), chunk->getMax());
    }

    if (_chunksMap.empty()) {
        invariant(!_shardVersion.isSet());
        return;
    }
    invariant(_shardVersion.isSet());

    _buildRangesMap();
}

CollectionMetadata::~CollectionMetadata() = default;

void CollectionMetadata::_buildRangesMap() {
    _rangesMap.clear();

    // Load the chunk information, coalescing their ranges. The version for this shard would be
    // the highest version for any of the chunks.

    BSONObj min, max;

    for (const auto& entry : _chunksMap) {
        BSONObj const& currMin = entry.first;
        BSONObj const& currMax = entry.second;

        // Coalesce the chunk's bounds in ranges if they are adjacent chunks
        if (min.isEmpty()) {
            min = currMin;
            max = currMax;
            continue;
        }

        if (SimpleBSONObjComparator::kInstance.evaluate(max == currMin)) {
            max = currMax;
            continue;
        }

        _rangesMap.emplace_hint(_rangesMap.end(), min, max);

        min = currMin;
        max = currMax;
    }

    invariant(!min.isEmpty());
    invariant(!max.isEmpty());

    _rangesMap.emplace(min, max);
}

bool CollectionMetadata::keyBelongsToMe(const BSONObj& key) const {
    if (_rangesMap.empty()) {
        return false;
    }

    auto it = _rangesMap.upper_bound(key);
    if (it != _rangesMap.begin())
        it--;

    return rangeContains(it->first, it->second, key);
}

bool CollectionMetadata::getNextChunk(const BSONObj& lookupKey, ChunkType* chunk) const {
    RangeMap::const_iterator upperChunkIt = _chunksMap.upper_bound(lookupKey);
    RangeMap::const_iterator lowerChunkIt = upperChunkIt;

    if (upperChunkIt != _chunksMap.begin()) {
        --lowerChunkIt;
    } else {
        lowerChunkIt = _chunksMap.end();
    }

    if (lowerChunkIt != _chunksMap.end() && lowerChunkIt->second.woCompare(lookupKey) > 0) {
        chunk->setMin(lowerChunkIt->first);
        chunk->setMax(lowerChunkIt->second);
        return true;
    }

    if (upperChunkIt != _chunksMap.end()) {
        chunk->setMin(upperChunkIt->first);
        chunk->setMax(upperChunkIt->second);
        return true;
    }

    return false;
}

bool CollectionMetadata::getDifferentChunk(const BSONObj& chunkMinKey,
                                           ChunkType* differentChunk) const {
    RangeMap::const_iterator upperChunkIt = _chunksMap.end();
    RangeMap::const_iterator lowerChunkIt = _chunksMap.begin();

    while (lowerChunkIt != upperChunkIt) {
        if (lowerChunkIt->first.woCompare(chunkMinKey) != 0) {
            differentChunk->setMin(lowerChunkIt->first);
            differentChunk->setMax(lowerChunkIt->second);
            return true;
        }
        ++lowerChunkIt;
    }

    return false;
}

Status CollectionMetadata::checkChunkIsValid(const ChunkType& chunk) {
    ChunkType existingChunk;

    if (!getNextChunk(chunk.getMin(), &existingChunk)) {
        return {ErrorCodes::StaleShardVersion,
                str::stream() << "Chunk with bounds "
                              << ChunkRange(chunk.getMin(), chunk.getMax()).toString()
                              << " is not owned by this shard."};
    }

    if (existingChunk.getMin().woCompare(chunk.getMin()) ||
        existingChunk.getMax().woCompare(chunk.getMax())) {
        return {ErrorCodes::StaleShardVersion,
                str::stream() << "Unable to find chunk with the exact bounds "
                              << ChunkRange(chunk.getMin(), chunk.getMax()).toString()
                              << " at collection version "
                              << getCollVersion().toString()};
    }

    return Status::OK();
}

bool CollectionMetadata::rangeOverlapsChunk(ChunkRange const& range) {
    return rangeMapOverlaps(_rangesMap, range.getMin(), range.getMax());
}

void CollectionMetadata::toBSONBasic(BSONObjBuilder& bb) const {
    _cm->getVersion().addToBSON(bb, "collVersion");
    _shardVersion.addToBSON(bb, "shardVersion");
    bb.append("keyPattern", _cm->getShardKeyPattern().toBSON());
}

void CollectionMetadata::toBSONChunks(BSONArrayBuilder& bb) const {
    if (_chunksMap.empty())
        return;

    for (RangeMap::const_iterator it = _chunksMap.begin(); it != _chunksMap.end(); ++it) {
        BSONArrayBuilder chunkBB(bb.subarrayStart());
        chunkBB.append(it->first);
        chunkBB.append(it->second);
        chunkBB.done();
    }
}

std::string CollectionMetadata::toStringBasic() const {
    return str::stream() << "collection version: " << _cm->getVersion().toString()
                         << ", shard version: " << _shardVersion.toString();
}

boost::optional<ChunkRange> CollectionMetadata::getNextOrphanRange(
    RangeMap const& receivingChunks, BSONObj const& origLookupKey) const {
    const BSONObj maxKey = getMaxKey();
    BSONObj lookupKey = origLookupKey;

    while (lookupKey.woCompare(maxKey) < 0) {
        using Its = std::pair<RangeMap::const_iterator, RangeMap::const_iterator>;

        const auto patchLookupKey = [&](RangeMap const& map) -> boost::optional<Its> {
            auto lowerIt = map.end(), upperIt = map.end();

            if (!map.empty()) {
                upperIt = map.upper_bound(lookupKey);
                lowerIt = upperIt;
                if (upperIt != map.begin())
                    --lowerIt;
                else
                    lowerIt = map.end();
            }

            // If we overlap, continue after the overlap
            //
            // TODO: Could optimize slightly by finding next non-contiguous chunk
            if (lowerIt != map.end() && lowerIt->second.woCompare(lookupKey) > 0) {
                lookupKey = lowerIt->second;  // note side effect
                return boost::none;
            } else {
                return Its(lowerIt, upperIt);
            }
        };

        boost::optional<Its> chunksIts, pendingIts;
        if (!(chunksIts = patchLookupKey(_chunksMap)) ||
            !(pendingIts = patchLookupKey(receivingChunks))) {
            continue;
        }

        BSONObj rangeMin = getMinKey();
        BSONObj rangeMax = maxKey;

        const auto patchArgRange = [&rangeMin, &rangeMax](RangeMap const& map, Its const& its) {
            // We know that the lookup key is not covered by a chunk or pending range, and where the
            // previous chunk and pending chunks are.  Now we fill in the bounds as the closest
            // bounds of the surrounding ranges in both maps.
            const auto& lowerIt = its.first;
            const auto& upperIt = its.second;

            if (lowerIt != map.end() && lowerIt->second.woCompare(rangeMin) > 0) {
                rangeMin = lowerIt->second;
            }

            if (upperIt != map.end() && upperIt->first.woCompare(rangeMax) < 0) {
                rangeMax = upperIt->first;
            }
        };

        patchArgRange(_chunksMap, *chunksIts);
        patchArgRange(receivingChunks, *pendingIts);

        return ChunkRange(rangeMin.getOwned(), rangeMax.getOwned());
    }

    return boost::none;
}

}  // namespace mongo
