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

import org.neo4j.internal.kernel.api.IndexQuery;
import org.neo4j.internal.kernel.api.LabelSet;
import org.neo4j.internal.schema.IndexDescriptor;
import org.neo4j.internal.schema.IndexOrder;
import org.neo4j.values.storable.Value;

/**
 * The index progressor is a cursor like class, which allows controlled progression through the entries of an index.
 * In contrast to a cursor, the progressor does not hold value state, but rather attempts to write the next entry to a
 * Client. The client can them accept the entry, in which case next() returns, or reject it, in which case the
 * progression continues until an acceptable entry is found or the progression is done.
 *
 * A Progressor is expected to feed a single client, which is setup for example in the constructor. The typical
 * interaction goes something like
 *
 *   -- query(client) -> INDEX
 *                       progressor = new Progressor( client )
 *                       client.initialize( progressor, ... )
 *
 *   -- next() --> client
 *                 client ---- next() --> progressor
 *                        <-- accept() --
 *                                 :false
 *                        <-- accept() --
 *                                 :false
 *                        <-- accept() --
 *                                  :true
 *                 client <--------------
 *   <-----------
 */
public interface IndexProgressor extends AutoCloseable
{
    /**
     * Progress through the index until the next accepted entry. Entries are feed to a Client, which
     * is setup in an implementation specific way.
     *
     * @return true if an accepted entry was found, false otherwise
     */
    boolean next();

    /**
     * Close the progressor and all attached resources. Idempotent.
     */
    @Override
    void close();

    /**
     * Client which accepts nodes and some of their property values.
     */
    interface EntityValueClient
    {
        /**
         * Setup the client for progressing using the supplied progressor. The values feed in accept map to the
         * propertyIds provided here. Called by index implementation.
         * @param descriptor The descriptor
         * @param progressor The progressor
         * @param query The query of this progression
         * @param indexOrder The required order the index should return nodeids in
         * @param needsValues if the index should fetch property values together with node ids for index queries
         * @param indexIncludesTransactionState {@code true} if the index takes transaction state into account such that the entities delivered through
         * {@link #acceptEntity(long, float, Value...)} have already been filtered through, and merged with, the transaction state. If this is {@code true},
         * then the client does not need to do its own transaction state filtering. This is the case for the fulltext schema indexes, for instance.
         * Otherwise if this parameter is {@code false}, then the client needs to filter and merge the transaction state in on their own.
         */
        void initialize( IndexDescriptor descriptor, IndexProgressor progressor,
                         IndexQuery[] query, IndexOrder indexOrder, boolean needsValues, boolean indexIncludesTransactionState );

        /**
         * Accept the node id and values of a candidate index entry. Return true if the entry is
         * accepted, false otherwise.
         * @param reference the node id of the candidate index entry
         * @param score a score figure for the quality of the match, for indexes where this makes sense, otherwise {@link Float#NaN}.
         * @param values the values of the candidate index entry
         * @return true if the entry is accepted, false otherwise
         */
        boolean acceptEntity( long reference, float score, Value... values );

        boolean needsValues();
    }

    /**
     * Client which accepts nodes and some of their labels.
     */
    interface NodeLabelClient
    {
        /**
         * Accept the node id and (some) labels of a candidate index entry. Return true if the entry
         * is accepted, false otherwise.
         * @param reference the node id of the candidate index entry
         * @param labels some labels of the candidate index entry
         * @return true if the entry is accepted, false otherwise
         */
        boolean acceptNode( long reference, LabelSet labels );
    }

    IndexProgressor EMPTY = new IndexProgressor()
    {
        @Override
        public boolean next()
        {
            return false;
        }

        @Override
        public void close()
        {   // no-op
        }
    };
}
