/*
 * Copyright (c) 2002-2019 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j.
 *
 * Neo4j is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package org.neo4j.kernel.api.index;

import org.neo4j.graphdb.Resource;
import org.neo4j.internal.schema.IndexOrder;
import org.neo4j.internal.kernel.api.IndexQuery;
import org.neo4j.internal.kernel.api.QueryContext;
import org.neo4j.internal.kernel.api.exceptions.schema.IndexNotApplicableKernelException;
import org.neo4j.storageengine.api.NodePropertyAccessor;
import org.neo4j.values.storable.Value;

/**
 * Reader for an index. Must honor repeatable reads, which means that if a lookup is executed multiple times the
 * same result set must be returned.
 */
public interface IndexReader extends Resource
{
    /**
     * @param nodeId node id to match.
     * @param propertyKeyIds the property key ids that correspond to each of the property values.
     * @param propertyValues property values to match.
     * @return number of index entries for the given {@code nodeId} and {@code propertyValues}.
     */
    long countIndexedNodes( long nodeId, int[] propertyKeyIds, Value... propertyValues );

    IndexSampler createSampler();

    /**
     * Queries the index for the given {@link IndexQuery} predicates.
     * @param client the client which will control the progression though query results.
     * @param needsValues if the index should fetch property values together with node ids for index queries
     * @param query the query so serve.
     */
    void query( QueryContext context, IndexProgressor.EntityValueClient client, IndexOrder indexOrder, boolean needsValues, IndexQuery... query )
            throws IndexNotApplicableKernelException;

    /**
     * @param predicates query to determine whether or not index has full value precision for.
     * @return whether or not this reader will only return 100% matching results from
     * {@link #query(QueryContext, IndexProgressor.EntityValueClient, IndexOrder, boolean, IndexQuery...)}.
     * If {@code false} is returned this means that the caller of
     * {@link #query(QueryContext, IndexProgressor.EntityValueClient, IndexOrder, boolean, IndexQuery...)} will have to
     * do additional filtering, double-checking of actual property values, externally.
     */
    boolean hasFullValuePrecision( IndexQuery... predicates );

    /**
     * Initializes {@code client} to be able to progress through all distinct values in this index. {@link IndexProgressor.EntityValueClient}
     * is used because it has a perfect method signature, even if the {@code reference} argument will instead be used
     * as number of index entries for the specific indexed value.
     *
     * {@code needsValues} decides whether or not values will be materialized and given to the client.
     * The use-case for setting this to {@code false} is to have a more efficient counting of distinct values in an index,
     * regardless of the actual values.
     * @param client {@link IndexProgressor.EntityValueClient} to get initialized with this progression.
     * @param propertyAccessor used for distinguishing between lossy indexed values.
     * @param needsValues whether or not values should be loaded.
     */
    void distinctValues( IndexProgressor.EntityValueClient client, NodePropertyAccessor propertyAccessor, boolean needsValues );

    IndexReader EMPTY = new IndexReader()
    {
        // Used for checking index correctness
        @Override
        public long countIndexedNodes( long nodeId, int[] propertyKeyIds, Value... propertyValues )
        {
            return 0;
        }

        @Override
        public IndexSampler createSampler()
        {
            return IndexSampler.EMPTY;
        }

        @Override
        public void query( QueryContext context, IndexProgressor.EntityValueClient client, IndexOrder indexOrder, boolean needsValues, IndexQuery... query )
        {
            // do nothing
        }

        @Override
        public void close()
        {
        }

        @Override
        public boolean hasFullValuePrecision( IndexQuery... predicates )
        {
            return true;
        }

        @Override
        public void distinctValues( IndexProgressor.EntityValueClient client, NodePropertyAccessor propertyAccessor, boolean needsValues )
        {
            // do nothing
        }
    };

    class Adaptor implements IndexReader
    {
        @Override
        public long countIndexedNodes( long nodeId, int[] propertyKeyIds, Value... propertyValues )
        {
            return 0;
        }

        @Override
        public IndexSampler createSampler()
        {
            return null;
        }

        @Override
        public void query( QueryContext context, IndexProgressor.EntityValueClient client, IndexOrder indexOrder, boolean needsValues, IndexQuery... query )
        {
        }

        @Override
        public boolean hasFullValuePrecision( IndexQuery... predicates )
        {
            return false;
        }

        @Override
        public void distinctValues( IndexProgressor.EntityValueClient client, NodePropertyAccessor propertyAccessor, boolean needsValues )
        {
        }

        @Override
        public void close()
        {
        }
    }
}
