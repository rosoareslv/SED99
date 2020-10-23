/*
 * Copyright (c) 2002-2018 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j Enterprise Edition. The included source
 * code can be redistributed and/or modified under the terms of the
 * GNU AFFERO GENERAL PUBLIC LICENSE Version 3
 * (http://www.fsf.org/licensing/licenses/agpl-3.0.html) with the
 * Commons Clause, as found in the associated LICENSE.txt file.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * Neo4j object code can be licensed independently from the source
 * under separate terms from the AGPL. Inquiries can be directed to:
 * licensing@neo4j.com
 *
 * More information is also available at:
 * https://neo4j.com/licensing/
 */
package org.neo4j.causalclustering.core.consensus.log.cache;

import org.neo4j.causalclustering.core.consensus.log.RaftLogEntry;

/**
 * A cache for in-flight entries which also tracks the size of the cache.
 */
public interface InFlightCache
{
    /**
     * Enables the cache.
     */
    void enable();

    /**
     * Put item into the cache.
     *
     * @param logIndex the index of the log entry.
     * @param entry the Raft log entry.
     */
    void put( long logIndex, RaftLogEntry entry );

    /**
     * Get item from the cache.
     *
     * @param logIndex the index of the log entry.
     * @return the log entry.
     */
    RaftLogEntry get( long logIndex );

    /**
     * Disposes of a range of elements from the tail of the consecutive cache.
     *
     * @param fromIndex the index to start from (inclusive).
     */
    void truncate( long fromIndex );

    /**
     * Prunes items from the cache.
     *
     * @param upToIndex the last index to prune (inclusive).
     */
    void prune( long upToIndex );

    /**
     * @return the amount of data in the cache.
     */
    long totalBytes();

    /**
     * @return the number of log entries in the cache.
     */
    int elementCount();
}
