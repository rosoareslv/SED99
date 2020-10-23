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

#pragma once

#include <memory>
#include <vector>

#include "mongo/base/clonable_ptr.h"
#include "mongo/db/catalog/index_catalog_entry.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/record_id.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage/record_store.h"

namespace mongo {
class Client;
class Collection;

class IndexDescriptor;
struct InsertDeleteOptions;

struct BsonRecord {
    RecordId id;
    Timestamp ts;
    const BSONObj* docPtr;
};

enum class IndexBuildMethod {
    /**
     * Use a collection scan to dump all keys into an external sorter. During this process,
     * concurrent client writes are accepted, and their generated keys are written into an
     * interceptor. On completion, this interceptor is drained and used to verify uniqueness
     * constraints on the index.
     *
     * This is the default for all index builds.
     */
    kHybrid,
    /**
     * Perform a collection scan by writing each document's generated key directly into the index.
     * Accept writes in the background into the index as well.
     */
    kBackground,
    /**
     * Perform a collection scan to dump all keys into the exteral sorter, then into the index.
     * During this process, callers guarantee that no writes will be accepted on this collection.
     */
    kForeground,
};

/**
 * The IndexCatalog is owned by the Collection and is responsible for the lookup and lifetimes of
 * the indexes in a collection. Every collection has exactly one instance of this class.
 *
 * Callers are expected to have acquired the necessary locks while accessing this interface.
 *
 * To inspect the contents of this IndexCatalog, callers may obtain an iterator from
 * getIndexIterator().
 *
 * Index building functionality is supported by the IndexBuildBlockInterface interface. However, it
 * is recommended to use the higher level MultiIndexBlock interface.
 *
 * Due to the resource-intensive nature of the index building process, this interface also provides
 * information on which indexes are available for queries through the following functions:
 *     int numIndexesTotal();
 *     int numIndexesReady();
 *     int numIndexesInProgress();
 */
class IndexCatalog {
public:
    class IndexIterator {
    public:
        virtual ~IndexIterator() = default;
        bool more();
        const IndexCatalogEntry* next();

    protected:
        /**
         * Advance the underlying iterator and returns the next index entry. Returns nullptr when
         * the iterator is exhausted.
         */
        virtual const IndexCatalogEntry* _advance() = 0;

    private:
        bool _start = true;
        const IndexCatalogEntry* _prev = nullptr;
        const IndexCatalogEntry* _next = nullptr;
    };

    class ReadyIndexesIterator : public IndexIterator {
    public:
        ReadyIndexesIterator(OperationContext* const opCtx,
                             IndexCatalogEntryContainer::const_iterator beginIterator,
                             IndexCatalogEntryContainer::const_iterator endIterator);

    private:
        const IndexCatalogEntry* _advance() override;

        OperationContext* const _opCtx;
        IndexCatalogEntryContainer::const_iterator _iterator;
        IndexCatalogEntryContainer::const_iterator _endIterator;
    };

    class AllIndexesIterator : public IndexIterator {
    public:
        /**
         * `ownedContainer` is a container whose lifetime the begin and end iterators depend
         * on. If the caller will keep control of the container for the entire iterator lifetime,
         * it should pass in a null value.
         */
        AllIndexesIterator(OperationContext* const opCtx,
                           std::unique_ptr<std::vector<IndexCatalogEntry*>> ownedContainer);

    private:
        const IndexCatalogEntry* _advance() override;

        OperationContext* const _opCtx;
        std::vector<IndexCatalogEntry*>::const_iterator _iterator;
        std::vector<IndexCatalogEntry*>::const_iterator _endIterator;
        std::unique_ptr<std::vector<IndexCatalogEntry*>> _ownedContainer;
    };

    /**
     * Interface for building a single index from an index spec and persisting its state to disk.
     */
    class IndexBuildBlockInterface {
    public:
        virtual ~IndexBuildBlockInterface() = default;

        /**
         * Must be called before the object is destructed if init() has been called.
         * Cleans up the temporary tables that are created for an index build.
         */
        virtual void deleteTemporaryTables(OperationContext* opCtx) = 0;

        /**
         * Initializes a new entry for the index in the IndexCatalog.
         *
         * On success, holds pointer to newly created IndexCatalogEntry that can be accessed using
         * getEntry(). IndexCatalog will still own the entry.
         *
         * Must be called from within a `WriteUnitOfWork`
         */
        virtual Status init(OperationContext* opCtx, Collection* collection) = 0;

        /**
         * Marks the state of the index as 'ready' and commits the index to disk.
         *
         * Must be called from within a `WriteUnitOfWork`
         */
        virtual void success(OperationContext* opCtx, Collection* collection) = 0;

        /**
         * Aborts the index build and removes any on-disk state where applicable.
         *
         * Must be called from within a `WriteUnitOfWork`
         */
        virtual void fail(OperationContext* opCtx, const Collection* collection) = 0;

        /**
         * Returns the IndexCatalogEntry that was created in init().
         *
         * This entry is owned by the IndexCatalog.
         */
        virtual IndexCatalogEntry* getEntry() = 0;

        /**
         * Returns the name of the index managed by this index builder.
         */
        virtual const std::string& getIndexName() const = 0;

        /**
         * Returns the index spec used to build this index.
         */
        virtual const BSONObj& getSpec() const = 0;
    };

    IndexCatalog() = default;
    virtual ~IndexCatalog() = default;

    inline IndexCatalog(IndexCatalog&&) = delete;
    inline IndexCatalog& operator=(IndexCatalog&&) = delete;

    // Must be called before used.
    virtual Status init(OperationContext* const opCtx) = 0;

    virtual bool ok() const = 0;

    // ---- accessors -----

    virtual bool haveAnyIndexes() const = 0;

    virtual bool haveAnyIndexesInProgress() const = 0;

    virtual int numIndexesTotal(OperationContext* const opCtx) const = 0;

    virtual int numIndexesReady(OperationContext* const opCtx) const = 0;

    virtual int numIndexesInProgress(OperationContext* const opCtx) const = 0;

    virtual bool haveIdIndex(OperationContext* const opCtx) const = 0;

    /**
     * Returns the spec for the id index to create by default for this collection.
     */
    virtual BSONObj getDefaultIdIndexSpec() const = 0;

    virtual const IndexDescriptor* findIdIndex(OperationContext* const opCtx) const = 0;

    /**
     * Find index by name.  The index name uniquely identifies an index.
     *
     * @return null if cannot find
     */
    virtual const IndexDescriptor* findIndexByName(
        OperationContext* const opCtx,
        const StringData name,
        const bool includeUnfinishedIndexes = false) const = 0;

    /**
     * Find index by matching key pattern and collation spec.  The key pattern and collation spec
     * uniquely identify an index.
     *
     * Collation is specified as a normalized collation spec as returned by
     * CollationInterface::getSpec.  An empty object indicates the simple collation.
     *
     * @return null if cannot find index, otherwise the index with a matching key pattern and
     * collation.
     */
    virtual const IndexDescriptor* findIndexByKeyPatternAndCollationSpec(
        OperationContext* const opCtx,
        const BSONObj& key,
        const BSONObj& collationSpec,
        const bool includeUnfinishedIndexes = false) const = 0;

    /**
     * Find indexes with a matching key pattern, putting them into the vector 'matches'.  The key
     * pattern alone does not uniquely identify an index.
     *
     * Consider using 'findIndexByName' if expecting to match one index.
     */
    virtual void findIndexesByKeyPattern(
        OperationContext* const opCtx,
        const BSONObj& key,
        const bool includeUnfinishedIndexes,
        std::vector<const IndexDescriptor*>* const matches) const = 0;

    /**
     * Returns an index suitable for shard key range scans.
     *
     * This index:
     * - must be prefixed by 'shardKey', and
     * - must not be a partial index.
     * - must have the simple collation.
     *
     * If the parameter 'requireSingleKey' is true, then this index additionally must not be
     * multi-key.
     *
     * If no such index exists, returns NULL.
     */
    virtual const IndexDescriptor* findShardKeyPrefixedIndex(OperationContext* const opCtx,
                                                             const BSONObj& shardKey,
                                                             const bool requireSingleKey) const = 0;

    virtual void findIndexByType(OperationContext* const opCtx,
                                 const std::string& type,
                                 std::vector<const IndexDescriptor*>& matches,
                                 const bool includeUnfinishedIndexes = false) const = 0;

    /**
     * Reload the index definition for 'oldDesc' from the CollectionCatalogEntry.  'oldDesc'
     * must be a ready index that is already registered with the index catalog.  Returns an
     * unowned pointer to the descriptor for the new index definition.
     *
     * Use this method to notify the IndexCatalog that the spec for this index has changed.
     *
     * It is invalid to dereference 'oldDesc' after calling this method.
     *
     * The caller must hold the collection X lock and ensure no index builds are in progress
     * on the collection.
     */
    virtual const IndexDescriptor* refreshEntry(OperationContext* const opCtx,
                                                const IndexDescriptor* const oldDesc) = 0;

    /**
     * Returns a pointer to the index catalog entry associated with 'desc'. Throws if there is no
     * such index. Never returns nullptr.
     */
    virtual const IndexCatalogEntry* getEntry(const IndexDescriptor* const desc) const = 0;

    /**
     * Returns a pointer to the index catalog entry associated with 'desc', where the caller assumes
     * shared ownership of the entry. Returns null if the entry does not exist.
     */
    virtual std::shared_ptr<const IndexCatalogEntry> getEntryShared(
        const IndexDescriptor*) const = 0;

    /**
     * Returns a vector of shared pointers to all index entries. Excludes unfinished indexes.
     */
    virtual std::vector<std::shared_ptr<const IndexCatalogEntry>> getAllReadyEntriesShared()
        const = 0;

    /**
     * Returns a not-ok Status if there are any unfinished index builds. No new indexes should
     * be built when in this state.
     */
    virtual Status checkUnfinished() const = 0;

    /**
     * Returns an iterator for the index descriptors in this IndexCatalog.
     */
    virtual std::unique_ptr<IndexIterator> getIndexIterator(
        OperationContext* const opCtx, const bool includeUnfinishedIndexes) const = 0;

    // ---- index set modifiers ------

    /**
     * Call this only on an empty collection from inside a WriteUnitOfWork. Index creation on an
     * empty collection can be rolled back as part of a larger WUOW. Returns the full specification
     * of the created index, as it is stored in this index catalog.
     */
    virtual StatusWith<BSONObj> createIndexOnEmptyCollection(OperationContext* const opCtx,
                                                             const BSONObj spec) = 0;

    /**
     * Checks the spec 'original' to make sure nothing is incorrectly set and cleans up any legacy
     * fields. Lastly, checks whether the spec conflicts with ready and in-progress indexes.
     *
     * Returns an error Status or the cleaned up version of the non-conflicting spec. Returns
     * IndexAlreadyExists if the index either already exists or is already being built.
     */
    virtual StatusWith<BSONObj> prepareSpecForCreate(OperationContext* const opCtx,
                                                     const BSONObj& original) const = 0;

    /**
     * Returns a copy of 'indexSpecsToBuild' that does not contain index specifications that already
     * exist or are already being built. If this is not done, an index build using
     * 'indexSpecsToBuild' may fail with error code IndexAlreadyExists. If {buildIndexes:false} is
     * set in the replica set config, also filters non-_id index specs out of the results.
     *
     * Additionally verifies the specs are valid and corrects any legacy fields. Throws on any spec
     * validation errors or conflicts other than IndexAlreadyExists, which indicates that the index
     * spec either already exists or is already being built and is what this function filters out.
     */
    virtual std::vector<BSONObj> removeExistingIndexes(
        OperationContext* const opCtx, const std::vector<BSONObj>& indexSpecsToBuild) const = 0;

    /**
     * Filters out ready and in-progress indexes that already exist and returns the remaining
     * indexes. Additionally filters out non-_id indexes if the replica set member config has
     * {buildIndexes:false} set.
     *
     * Does no correctness verification of the provided specs, nor modifications for legacy reasons.
     *
     * This should only be used when we are confident in the specs, such as when specs are received
     * via replica set cloning or chunk migrations.
     */
    virtual std::vector<BSONObj> removeExistingIndexesNoChecks(
        OperationContext* const opCtx, const std::vector<BSONObj>& indexSpecsToBuild) const = 0;

    /**
     * Drops all indexes in the index catalog, optionally dropping the id index depending on the
     * 'includingIdIndex' parameter value. If 'onDropFn' is provided, it will be called before each
     * index is dropped to allow timestamping each individual drop.
     */
    virtual void dropAllIndexes(OperationContext* opCtx,
                                bool includingIdIndex,
                                stdx::function<void(const IndexDescriptor*)> onDropFn) = 0;
    virtual void dropAllIndexes(OperationContext* opCtx, bool includingIdIndex) = 0;

    /**
     * Drops the index.
     *
     * The caller must hold the collection X lock and ensure no index builds are in progress on the
     * collection.
     */
    virtual Status dropIndex(OperationContext* const opCtx, const IndexDescriptor* const desc) = 0;

    /**
     * Drops all incomplete indexes and returns specs. After this, the indexes can be rebuilt.
     */
    virtual std::vector<BSONObj> getAndClearUnfinishedIndexes(OperationContext* const opCtx) = 0;

    // ---- modify single index

    /**
     * Returns true if the index 'idx' is multikey, and returns false otherwise.
     */
    virtual bool isMultikey(OperationContext* const opCtx, const IndexDescriptor* const idx) = 0;

    /**
     * Returns the path components that cause the index 'idx' to be multikey if the index supports
     * path-level multikey tracking, and returns an empty vector if path-level multikey tracking
     * isn't supported.
     *
     * If the index supports path-level multikey tracking but isn't multikey, then this function
     * returns a vector with size equal to the number of elements in the index key pattern where
     * each element in the vector is an empty set.
     */
    virtual MultikeyPaths getMultikeyPaths(OperationContext* const opCtx,
                                           const IndexDescriptor* const idx) = 0;

    /**
     * Sets the index 'desc' to be multikey with the provided 'multikeyPaths'.
     *
     * See IndexCatalogEntry::setMultikey().
     */
    virtual void setMultikeyPaths(OperationContext* const opCtx,
                                  const IndexDescriptor* const desc,
                                  const MultikeyPaths& multikeyPaths) = 0;

    // ----- data modifiers ------

    /**
     * When 'keysInsertedOut' is not null, it will be set to the number of index keys inserted by
     * this operation.
     *
     * This method may throw.
     */
    virtual Status indexRecords(OperationContext* const opCtx,
                                const std::vector<BsonRecord>& bsonRecords,
                                int64_t* const keysInsertedOut) = 0;

    /**
     * Both 'keysInsertedOut' and 'keysDeletedOut' are required and will be set to the number of
     * index keys inserted and deleted by this operation, respectively.
     *
     * This method may throw.
     */
    virtual Status updateRecord(OperationContext* const opCtx,
                                const BSONObj& oldDoc,
                                const BSONObj& newDoc,
                                const RecordId& recordId,
                                int64_t* const keysInsertedOut,
                                int64_t* const keysDeletedOut) = 0;

    /**
     * When 'keysDeletedOut' is not null, it will be set to the number of index keys removed by
     * this operation.
     */
    virtual void unindexRecord(OperationContext* const opCtx,
                               const BSONObj& obj,
                               const RecordId& loc,
                               const bool noWarn,
                               int64_t* const keysDeletedOut) = 0;

    /*
     * Attempt compaction on all ready indexes to regain disk space, if the storage engine's index
     * supports compaction in-place.
     */
    virtual Status compactIndexes(OperationContext* opCtx) = 0;

    virtual std::string getAccessMethodName(const BSONObj& keyPattern) = 0;

    /**
     * Creates an instance of IndexBuildBlockInterface for building an index with the provided index
     * spex and OperationContext.
     */
    virtual std::unique_ptr<IndexBuildBlockInterface> createIndexBuildBlock(
        OperationContext* opCtx, const BSONObj& spec, IndexBuildMethod method) = 0;

    // public helpers

    /**
     * Returns length of longest index name.
     * This includes unfinished indexes.
     */
    virtual std::string::size_type getLongestIndexNameLength(OperationContext* opCtx) const = 0;

    /**
     * Detects and normalizes _id index key pattern if found.
     */
    virtual BSONObj fixIndexKey(const BSONObj& key) const = 0;

    /**
     * Fills out 'options' in order to indicate whether to allow dups or relax
     * index constraints, as needed by replication.
     */
    virtual void prepareInsertDeleteOptions(OperationContext* opCtx,
                                            const IndexDescriptor* desc,
                                            InsertDeleteOptions* options) const = 0;

    virtual void setNs(NamespaceString ns) = 0;

    virtual void indexBuildSuccess(OperationContext* opCtx, IndexCatalogEntry* index) = 0;
};
}  // namespace mongo
