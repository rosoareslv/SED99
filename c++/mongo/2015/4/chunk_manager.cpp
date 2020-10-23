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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/chunk_manager.h"

#include <map>
#include <set>

#include "mongo/db/namespace_string.h"
#include "mongo/db/query/index_bounds_builder.h"
#include "mongo/db/query/query_planner.h"
#include "mongo/db/query/query_planner_common.h"
#include "mongo/s/catalog/catalog_cache.h"
#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/catalog/type_collection.h"
#include "mongo/s/chunk_diff.h"
#include "mongo/s/client/shard_connection.h"
#include "mongo/s/config.h"
#include "mongo/s/grid.h"
#include "mongo/util/log.h"
#include "mongo/util/timer.h"

namespace mongo {

    using boost::shared_ptr;

    using std::make_pair;
    using std::map;
    using std::max;
    using std::pair;
    using std::set;
    using std::string;
    using std::vector;

namespace {

    /**
     * This is an adapter so we can use config diffs - mongos and mongod do them slightly
     * differently
     *
     * The mongos adapter here tracks all shards, and stores ranges by (max, Chunk) in the map.
     */
    class CMConfigDiffTracker : public ConfigDiffTracker<ChunkPtr, std::string> {
    public:
        CMConfigDiffTracker( ChunkManager* manager ) : _manager( manager ) { }

        virtual bool isTracked( const BSONObj& chunkDoc ) const {
            // Mongos tracks all shards
            return true;
        }

        virtual BSONObj minFrom( const ChunkPtr& val ) const {
            return val.get()->getMin();
        }

        virtual bool isMinKeyIndexed() const { return false; }

        virtual pair<BSONObj,ChunkPtr> rangeFor( const BSONObj& chunkDoc, const BSONObj& min, const BSONObj& max ) const {
            ChunkPtr c( new Chunk( _manager, chunkDoc ) );
            return make_pair( max, c );
        }

        virtual string shardFor(const string& hostName) const {
            Shard shard = Shard::make(hostName);
            return shard.getName();
        }

    private:
        ChunkManager* _manager;
    };


    bool allOfType(BSONType type, const BSONObj& o) {
        BSONObjIterator it(o);
        while(it.more()) {
            if (it.next().type() != type) {
                return false;
            }
        }
        return true;
    }

} // namespace

    AtomicUInt32 ChunkManager::NextSequenceNumber(1U);

    ChunkManager::ChunkManager( const string& ns, const ShardKeyPattern& pattern , bool unique ) :
        _ns( ns ),
        _keyPattern( pattern.getKeyPattern() ),
        _unique( unique ),
        _chunkRanges(),
        _sequenceNumber(NextSequenceNumber.addAndFetch(1))
    {
        //
        // Sets up a chunk manager from new data
        //
    }

    ChunkManager::ChunkManager(const CollectionType& coll)
        : _ns(coll.getNs()),
          _keyPattern(coll.getKeyPattern()),
          _unique(coll.getUnique()),
          _chunkRanges(),
          // The shard versioning mechanism hinges on keeping track of the number of times we
          // reload ChunkManagers. Increasing this number here will prompt checkShardVersion to
          // refresh the connection-level versions to the most up to date value.
          _sequenceNumber(NextSequenceNumber.addAndFetch(1)) {

        _version = ChunkVersion::fromBSON(coll.toBSON());
    }

    void ChunkManager::loadExistingRanges( const string& config, const ChunkManager* oldManager ) {
        int tries = 3;
        while (tries--) {
            ChunkMap chunkMap;
            set<Shard> shards;
            ShardVersionMap shardVersions;
            Timer t;

            bool success = _load(config, chunkMap, shards, &shardVersions, oldManager);

            if( success ){
                {
                    int ms = t.millis();
                    log() << "ChunkManager: time to load chunks for " << _ns << ": " << ms << "ms"
                        << " sequenceNumber: " << _sequenceNumber
                        << " version: " << _version.toString()
                        << " based on: " <<
                        (oldManager ? oldManager->getVersion().toString() : "(empty)");;
                }

                // TODO: Merge into diff code above, so we validate in one place
                if (_isValid(chunkMap)) {
                    // These variables are const for thread-safety. Since the
                    // constructor can only be called from one thread, we don't have
                    // to worry about that here.
                    const_cast<ChunkMap&>(_chunkMap).swap(chunkMap);
                    const_cast<set<Shard>&>(_shards).swap(shards);
                    const_cast<ShardVersionMap&>(_shardVersions).swap(shardVersions);
                    const_cast<ChunkRangeManager&>(_chunkRanges).reloadAll(_chunkMap);

                    return;
                }
            }

            if (_chunkMap.size() < 10) {
                _printChunks();
            }
            
            warning() << "ChunkManager loaded an invalid config for " << _ns
                      << ", trying again";

            sleepmillis(10 * (3-tries));
        }

        // this will abort construction so we should never have a reference to an invalid config
        msgasserted(13282, "Couldn't load a valid config for " + _ns + " after 3 attempts. Please try again.");
    }

    bool ChunkManager::_load(const string& config,
                             ChunkMap& chunkMap,
                             set<Shard>& shards,
                             ShardVersionMap* shardVersions,
                             const ChunkManager* oldManager)
    {

        // Reset the max version, but not the epoch, when we aren't loading from the oldManager
        _version = ChunkVersion( 0, 0, _version.epoch() );

        // If we have a previous version of the ChunkManager to work from, use that info to reduce
        // our config query
        if( oldManager && oldManager->getVersion().isSet() ){

            // Get the old max version
            _version = oldManager->getVersion();
            // Load a copy of the old versions
            *shardVersions = oldManager->_shardVersions;

            // Load a copy of the chunk map, replacing the chunk manager with our own
            const ChunkMap& oldChunkMap = oldManager->getChunkMap();

            // Could be v.expensive
            // TODO: If chunks were immutable and didn't reference the manager, we could do more
            // interesting things here
            for( ChunkMap::const_iterator it = oldChunkMap.begin(); it != oldChunkMap.end(); it++ ){

                ChunkPtr oldC = it->second;
                ChunkPtr c( new Chunk( this, oldC->getMin(),
                                             oldC->getMax(),
                                             oldC->getShard(),
                                             oldC->getLastmod() ) );

                c->setBytesWritten( oldC->getBytesWritten() );

                chunkMap.insert( make_pair( oldC->getMax(), c ) );
            }

            LOG(2) << "loading chunk manager for collection " << _ns
                   << " using old chunk manager w/ version " << _version.toString()
                   << " and " << oldChunkMap.size() << " chunks";
        }

        // Attach a diff tracker for the versioned chunk data
        CMConfigDiffTracker differ( this );
        differ.attach( _ns, chunkMap, _version, *shardVersions );

        // Diff tracker should *always* find at least one chunk if collection exists
        int diffsApplied = differ.calculateConfigDiff(config);
        if( diffsApplied > 0 ){

            LOG(2) << "loaded " << diffsApplied << " chunks into new chunk manager for " << _ns
                   << " with version " << _version;

            // Add all existing shards we find to the shards set
            for (ShardVersionMap::iterator it = shardVersions->begin();
                 it != shardVersions->end();
                ) {
                if (Shard::findIfExists(it->first).ok()) {
                    shards.insert(it->first);
                    ++it;
                }
                else {
                    shardVersions->erase(it++);
                }
            }

            return true;
        }
        else if( diffsApplied == 0 ){

            // No chunks were found for the ns
            warning() << "no chunks found when reloading " << _ns
                      << ", previous version was " << _version;

            // Set all our data to empty
            chunkMap.clear();
            shardVersions->clear();
            _version = ChunkVersion( 0, 0, OID() );

            return true;
        }
        else { // diffsApplied < 0

            bool allInconsistent = differ.numValidDiffs() == 0;

            if( allInconsistent ){
                // All versions are different, this can be normal
                warning() << "major change in chunk information found when reloading "
                          << _ns << ", previous version was " << _version;
            }
            else {
                // Inconsistent load halfway through (due to yielding cursor during load)
                // should be rare
                warning() << "inconsistent chunks found when reloading "
                          << _ns << ", previous version was " << _version
                          << ", this should be rare";
            }

            // Set all our data to empty to be extra safe
            chunkMap.clear();
            shardVersions->clear();
            _version = ChunkVersion( 0, 0, OID() );

            return allInconsistent;
        }

    }

    ChunkManagerPtr ChunkManager::reload(bool force) const {
        const NamespaceString nss(_ns);
        auto status = grid.catalogCache()->getDatabase(nss.db().toString());
        shared_ptr<DBConfig> config = uassertStatusOK(status);

        return config->getChunkManager(getns(), force);
    }

    bool ChunkManager::_isValid(const ChunkMap& chunkMap) {
#define ENSURE(x) do { if(!(x)) { log() << "ChunkManager::_isValid failed: " #x; return false; } } while(0)

        if (chunkMap.empty())
            return true;

        // Check endpoints
        ENSURE(allOfType(MinKey, chunkMap.begin()->second->getMin()));
        ENSURE(allOfType(MaxKey, boost::prior(chunkMap.end())->second->getMax()));

        // Make sure there are no gaps or overlaps
        for (ChunkMap::const_iterator it=boost::next(chunkMap.begin()), end=chunkMap.end(); it != end; ++it) {
            ChunkMap::const_iterator last = boost::prior(it);

            if (!(it->second->getMin() == last->second->getMax())) {
                PRINT(last->second->toString());
                PRINT(it->second->toString());
                PRINT(it->second->getMin());
                PRINT(last->second->getMax());
            }
            ENSURE(it->second->getMin() == last->second->getMax());
        }

        return true;

#undef ENSURE
    }

    void ChunkManager::_printChunks() const {
        for (ChunkMap::const_iterator it=_chunkMap.begin(), end=_chunkMap.end(); it != end; ++it) {
            log() << *it->second ;
        }
    }

    void ChunkManager::calcInitSplitsAndShards( const Shard& primary,
                                                const vector<BSONObj>* initPoints,
                                                const vector<Shard>* initShards,
                                                vector<BSONObj>* splitPoints,
                                                vector<Shard>* shards ) const
    {
        verify( _chunkMap.size() == 0 );

        unsigned long long numObjects = 0;
        Chunk c(this, _keyPattern.getKeyPattern().globalMin(),
                      _keyPattern.getKeyPattern().globalMax(), primary);

        if ( !initPoints || !initPoints->size() ) {
            // discover split points
            {
                // get stats to see if there is any data
                ScopedDbConnection shardConn(primary.getConnString());

                numObjects = shardConn->count( getns() );
                shardConn.done();
            }

            if ( numObjects > 0 )
                c.pickSplitVector( *splitPoints , Chunk::MaxChunkSize );

            // since docs alread exists, must use primary shard
            shards->push_back( primary );
        } else {
            // make sure points are unique and ordered
            set<BSONObj> orderedPts;
            for ( unsigned i = 0; i < initPoints->size(); ++i ) {
                BSONObj pt = (*initPoints)[i];
                orderedPts.insert( pt );
            }
            for ( set<BSONObj>::iterator it = orderedPts.begin(); it != orderedPts.end(); ++it ) {
                splitPoints->push_back( *it );
            }

            if ( !initShards || !initShards->size() ) {
                // If not specified, only use the primary shard (note that it's not safe for mongos
                // to put initial chunks on other shards without the primary mongod knowing).
                shards->push_back( primary );
            } else {
                std::copy( initShards->begin() , initShards->end() , std::back_inserter(*shards) );
            }
        }
    }

    void ChunkManager::createFirstChunks( const string& config,
                                          const Shard& primary,
                                          const vector<BSONObj>* initPoints,
                                          const vector<Shard>* initShards )
    {
        // TODO distlock?
        // TODO: Race condition if we shard the collection and insert data while we split across
        // the non-primary shard.

        vector<BSONObj> splitPoints;
        vector<Shard> shards;

        calcInitSplitsAndShards( primary, initPoints, initShards,
                                 &splitPoints, &shards );

        // this is the first chunk; start the versioning from scratch
        ChunkVersion version;
        version.incEpoch();
        version.incMajor();
        
        log() << "going to create " << splitPoints.size() + 1 << " chunk(s) for: " << _ns
              << " using new epoch " << version.epoch() ;

        for ( unsigned i=0; i<=splitPoints.size(); i++ ) {
            BSONObj min = i == 0 ? _keyPattern.getKeyPattern().globalMin() : splitPoints[i-1];
            BSONObj max = i < splitPoints.size() ?
                splitPoints[i] : _keyPattern.getKeyPattern().globalMax();

            Chunk temp( this , min , max , shards[ i % shards.size() ], version );

            BSONObjBuilder chunkBuilder;
            temp.serialize(chunkBuilder);

            BSONObj chunkObj = chunkBuilder.obj();

            Status result = grid.catalogManager()->update(ChunkType::ConfigNS,
                                                          BSON(ChunkType::name(temp.genID())),
                                                          chunkObj,
                                                          true,
                                                          false,
                                                          NULL);

            version.incMinor();

            if (!result.isOK()) {
                string ss = str::stream() << "creating first chunks failed. result: "
                                          << result.reason();
                error() << ss;
                msgasserted(15903, ss);
            }
        }

        _version = ChunkVersion(0, 0, version.epoch());
    }

    ChunkPtr ChunkManager::findIntersectingChunk( const BSONObj& shardKey ) const {
        {
            BSONObj chunkMin;
            ChunkPtr chunk;
            {
                ChunkMap::const_iterator it = _chunkMap.upper_bound( shardKey );
                if (it != _chunkMap.end()) {
                    chunkMin = it->first;
                    chunk = it->second;
                }
            }

            if ( chunk ) {
                if ( chunk->containsKey( shardKey ) ){
                    return chunk;
                }

                PRINT(chunkMin);
                PRINT(*chunk);
                PRINT( shardKey );

                reload();
                massert(13141, "Chunk map pointed to incorrect chunk", false);
            }
        }

        msgasserted( 8070 ,
                     str::stream() << "couldn't find a chunk intersecting: " << shardKey
                                   << " for ns: " << _ns
                                   << " at version: " << _version.toString()
                                   << ", number of chunks: " << _chunkMap.size() );
    }

    void ChunkManager::getShardsForQuery( set<Shard>& shards , const BSONObj& query ) const {
        CanonicalQuery* canonicalQuery = NULL;
        Status status = CanonicalQuery::canonicalize(
                            _ns,
                            query,
                            &canonicalQuery,
                            WhereCallbackNoop());
                            
        boost::scoped_ptr<CanonicalQuery> canonicalQueryPtr(canonicalQuery);
        
        uassert(status.code(), status.reason(), status.isOK());

        // Query validation
        if (QueryPlannerCommon::hasNode(canonicalQuery->root(), MatchExpression::GEO_NEAR)) {
            uassert(13501, "use geoNear command rather than $near query", false);
        }

        // Transforms query into bounds for each field in the shard key
        // for example :
        //   Key { a: 1, b: 1 },
        //   Query { a : { $gte : 1, $lt : 2 },
        //            b : { $gte : 3, $lt : 4 } }
        //   => Bounds { a : [1, 2), b : [3, 4) }
        IndexBounds bounds = getIndexBoundsForQuery(_keyPattern.toBSON(), canonicalQuery);

        // Transforms bounds for each shard key field into full shard key ranges
        // for example :
        //   Key { a : 1, b : 1 }
        //   Bounds { a : [1, 2), b : [3, 4) }
        //   => Ranges { a : 1, b : 3 } => { a : 2, b : 4 }
        BoundList ranges = _keyPattern.flattenBounds(bounds);

        for (BoundList::const_iterator it = ranges.begin(); it != ranges.end();
            ++it) {

            getShardsForRange(shards, it->first /*min*/, it->second /*max*/);

            // once we know we need to visit all shards no need to keep looping
            if( shards.size() == _shards.size() ) break;
        }

        // SERVER-4914 Some clients of getShardsForQuery() assume at least one shard will be
        // returned.  For now, we satisfy that assumption by adding a shard with no matches rather
        // than return an empty set of shards.
        if ( shards.empty() ) {
            massert( 16068, "no chunk ranges available", !_chunkRanges.ranges().empty() );
            shards.insert( _chunkRanges.ranges().begin()->second->getShard() );
        }
    }

    void ChunkManager::getShardsForRange( set<Shard>& shards,
                                          const BSONObj& min,
                                          const BSONObj& max ) const {

        ChunkRangeMap::const_iterator it = _chunkRanges.upper_bound(min);
        ChunkRangeMap::const_iterator end = _chunkRanges.upper_bound(max);

        massert( 13507 , str::stream() << "no chunks found between bounds " << min << " and " << max , it != _chunkRanges.ranges().end() );

        if( end != _chunkRanges.ranges().end() ) ++end;

        for( ; it != end; ++it ){
            shards.insert(it->second->getShard());

            // once we know we need to visit all shards no need to keep looping
            if (shards.size() == _shards.size()) break;
        }
    }

    void ChunkManager::getAllShards( set<Shard>& all ) const {
        all.insert(_shards.begin(), _shards.end());
    }

    IndexBounds ChunkManager::getIndexBoundsForQuery(const BSONObj& key, const CanonicalQuery* canonicalQuery) {
        // $text is not allowed in planning since we don't have text index on mongos.
        //
        // TODO: Treat $text query as a no-op in planning. So with shard key {a: 1},
        //       the query { a: 2, $text: { ... } } will only target to {a: 2}.
        if (QueryPlannerCommon::hasNode(canonicalQuery->root(), MatchExpression::TEXT)) {
            IndexBounds bounds;
            IndexBoundsBuilder::allValuesBounds(key, &bounds); // [minKey, maxKey]
            return bounds;
        }

        // Consider shard key as an index
        string accessMethod = IndexNames::findPluginName(key);
        dassert(accessMethod == IndexNames::BTREE || accessMethod == IndexNames::HASHED);

        // Use query framework to generate index bounds
        QueryPlannerParams plannerParams;
        // Must use "shard key" index
        plannerParams.options = QueryPlannerParams::NO_TABLE_SCAN;
        IndexEntry indexEntry(key, accessMethod, false /* multiKey */, false /* sparse */,
                              false /* unique */, "shardkey", BSONObj());
        plannerParams.indices.push_back(indexEntry);

        OwnedPointerVector<QuerySolution> solutions;
        Status status = QueryPlanner::plan(*canonicalQuery, plannerParams, &solutions.mutableVector());
        uassert(status.code(), status.reason(), status.isOK());

        IndexBounds bounds;

        for (vector<QuerySolution*>::const_iterator it = solutions.begin();
                bounds.size() == 0 && it != solutions.end(); it++) {
            // Try next solution if we failed to generate index bounds, i.e. bounds.size() == 0
            bounds = collapseQuerySolution((*it)->root.get());
        }

        if (bounds.size() == 0) {
            // We cannot plan the query without collection scan, so target to all shards.
            IndexBoundsBuilder::allValuesBounds(key, &bounds); // [minKey, maxKey]
        }
        return bounds;
    }

    IndexBounds ChunkManager::collapseQuerySolution( const QuerySolutionNode* node ) {
        if (node->children.size() == 0) {
            invariant(node->getType() == STAGE_IXSCAN);

            const IndexScanNode* ixNode = static_cast<const IndexScanNode*>( node );
            return ixNode->bounds;
        }

        if (node->children.size() == 1) {
            // e.g. FETCH -> IXSCAN
            return collapseQuerySolution( node->children.front() );
        }

        // children.size() > 1, assert it's OR / SORT_MERGE.
        if ( node->getType() != STAGE_OR && node->getType() != STAGE_SORT_MERGE ) {
            // Unexpected node. We should never reach here.
            error() << "could not generate index bounds on query solution tree: " << node->toString();
            dassert(false); // We'd like to know this error in testing.

            // Bail out with all shards in production, since this isn't a fatal error.
            return IndexBounds();
        }

        IndexBounds bounds;
        for ( vector<QuerySolutionNode*>::const_iterator it = node->children.begin();
                it != node->children.end(); it++ )
        {
            // The first branch under OR
            if ( it == node->children.begin() ) {
                invariant(bounds.size() == 0);
                bounds = collapseQuerySolution( *it );
                if (bounds.size() == 0) { // Got unexpected node in query solution tree
                    return IndexBounds();
                }
                continue;
            }

            IndexBounds childBounds = collapseQuerySolution( *it );
            if (childBounds.size() == 0) { // Got unexpected node in query solution tree
                return IndexBounds();
            }

            invariant(childBounds.size() == bounds.size());
            for ( size_t i = 0; i < bounds.size(); i++ ) {
                bounds.fields[i].intervals.insert( bounds.fields[i].intervals.end(),
                                                   childBounds.fields[i].intervals.begin(),
                                                   childBounds.fields[i].intervals.end() );
            }
        }

        for ( size_t i = 0; i < bounds.size(); i++ ) {
            IndexBoundsBuilder::unionize( &bounds.fields[i] );
        }

        return bounds;
    }

    bool ChunkManager::compatibleWith(const ChunkManager& other, const string& shardName) const {
        // Return true if the shard version is the same in the two chunk managers
        // TODO: This doesn't need to be so strong, just major vs
        return other.getVersion(shardName).equals(getVersion(shardName));
    }

    ChunkVersion ChunkManager::getVersion(const std::string& shardName) const {
        ShardVersionMap::const_iterator i = _shardVersions.find(shardName);
        if ( i == _shardVersions.end() ) {
            // Shards without explicitly tracked shard versions (meaning they have
            // no chunks) always have a version of (0, 0, epoch).  Note this is
            // *different* from the dropped chunk version of (0, 0, OID(000...)).
            // See s/chunk_version.h.
            return ChunkVersion( 0, 0, _version.epoch() );
        }
        return i->second;
    }

    ChunkVersion ChunkManager::getVersion() const {
        return _version;
    }

    string ChunkManager::toString() const {
        StringBuilder sb;
        sb << "ChunkManager: " << _ns << " key:" << _keyPattern.toString() << '\n';

        for (ChunkMap::const_iterator i = _chunkMap.begin(); i != _chunkMap.end(); ++i) {
            sb << "\t" << i->second->toString() << '\n';
        }

        return sb.str();
    }


    ChunkRange::ChunkRange(ChunkMap::const_iterator begin, const ChunkMap::const_iterator end)
        : _manager(begin->second->getManager()),
          _shard(begin->second->getShard()),
          _min(begin->second->getMin()),
          _max(boost::prior(end)->second->getMax()) {

        invariant(begin != end);

        DEV while (begin != end) {
            dassert(begin->second->getManager() == _manager);
            dassert(begin->second->getShard() == _shard);
            ++begin;
        }
    }

    ChunkRange::ChunkRange(const ChunkRange& min, const ChunkRange& max)
        : _manager(min.getManager()),
          _shard(min.getShard()),
          _min(min.getMin()),
          _max(max.getMax()) {

        invariant(min.getShard() == max.getShard());
        invariant(min.getManager() == max.getManager());
        invariant(min.getMax() == max.getMin());
    }

    string ChunkRange::toString() const {
        StringBuilder sb;
        sb << "ChunkRange(min=" << _min << ", max=" << _max
                                << ", shard=" << _shard.toString() << ")";

        return sb.str();
    }


    void ChunkRangeManager::assertValid() const {
        if (_ranges.empty())
            return;

        try {
            // No Nulls
            for (ChunkRangeMap::const_iterator it=_ranges.begin(), end=_ranges.end(); it != end; ++it) {
                verify(it->second);
            }

            // Check endpoints
            verify(allOfType(MinKey, _ranges.begin()->second->getMin()));
            verify(allOfType(MaxKey, boost::prior(_ranges.end())->second->getMax()));

            // Make sure there are no gaps or overlaps
            for (ChunkRangeMap::const_iterator it=boost::next(_ranges.begin()), end=_ranges.end(); it != end; ++it) {
                ChunkRangeMap::const_iterator last = boost::prior(it);
                verify(it->second->getMin() == last->second->getMax());
            }

            // Check Map keys
            for (ChunkRangeMap::const_iterator it=_ranges.begin(), end=_ranges.end(); it != end; ++it) {
                verify(it->first == it->second->getMax());
            }

            // Make sure we match the original chunks
            const ChunkMap chunks = _ranges.begin()->second->getManager()->_chunkMap;
            for ( ChunkMap::const_iterator i=chunks.begin(); i!=chunks.end(); ++i ) {
                const ChunkPtr chunk = i->second;

                ChunkRangeMap::const_iterator min = _ranges.upper_bound(chunk->getMin());
                ChunkRangeMap::const_iterator max = _ranges.lower_bound(chunk->getMax());

                verify(min != _ranges.end());
                verify(max != _ranges.end());
                verify(min == max);
                verify(min->second->getShard() == chunk->getShard());
                verify(min->second->containsKey( chunk->getMin() ));
                verify(min->second->containsKey( chunk->getMax() ) || (min->second->getMax() == chunk->getMax()));
            }

        }
        catch (...) {
            error() << "\t invalid ChunkRangeMap! printing ranges:";

            for (ChunkRangeMap::const_iterator it = _ranges.begin(), end = _ranges.end(); it != end; ++it) {
                log() << it->first << ": " << it->second->toString();
            }

            throw;
        }
    }

    void ChunkRangeManager::reloadAll(const ChunkMap& chunks) {
        _ranges.clear();
        _insertRange(chunks.begin(), chunks.end());

        DEV assertValid();
    }

    void ChunkRangeManager::_insertRange(ChunkMap::const_iterator begin, const ChunkMap::const_iterator end) {
        while (begin != end) {
            ChunkMap::const_iterator first = begin;
            Shard shard = first->second->getShard();
            while (begin != end && (begin->second->getShard() == shard))
                ++begin;

            shared_ptr<ChunkRange> cr (new ChunkRange(first, begin));
            _ranges[cr->getMax()] = cr;
        }
    }

    int ChunkManager::getCurrentDesiredChunkSize() const {
        // split faster in early chunks helps spread out an initial load better
        const int minChunkSize = 1 << 20;  // 1 MBytes

        int splitThreshold = Chunk::MaxChunkSize;

        int nc = numChunks();

        if ( nc <= 1 ) {
            return 1024;
        }
        else if ( nc < 3 ) {
            return minChunkSize / 2;
        }
        else if ( nc < 10 ) {
            splitThreshold = max( splitThreshold / 4 , minChunkSize );
        }
        else if ( nc < 20 ) {
            splitThreshold = max( splitThreshold / 2 , minChunkSize );
        }

        return splitThreshold;
    }

} // namespace mongo
