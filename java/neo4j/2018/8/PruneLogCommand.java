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
package org.neo4j.causalclustering.core.consensus.outcome;

import java.io.IOException;
import java.util.Objects;

import org.neo4j.causalclustering.core.consensus.log.cache.InFlightCache;
import org.neo4j.causalclustering.core.consensus.log.RaftLog;
import org.neo4j.logging.Log;

public class PruneLogCommand implements RaftLogCommand
{
    private final long pruneIndex;

    public PruneLogCommand( long pruneIndex )
    {
        this.pruneIndex = pruneIndex;
    }

    @Override
    public void dispatch( Handler handler )
    {
        handler.prune( pruneIndex );
    }

    @Override
    public void applyTo( RaftLog raftLog, Log log ) throws IOException
    {
        raftLog.prune( pruneIndex );
    }

    @Override
    public void applyTo( InFlightCache inFlightCache, Log log )
    {
        // only the actual log prunes
    }

    @Override
    public boolean equals( Object o )
    {
        if ( this == o )
        {
            return true;
        }
        if ( o == null || getClass() != o.getClass() )
        {
            return false;
        }
        PruneLogCommand that = (PruneLogCommand) o;
        return pruneIndex == that.pruneIndex;
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( pruneIndex );
    }

    @Override
    public String toString()
    {
        return "PruneLogCommand{" +
               "pruneIndex=" + pruneIndex +
               '}';
    }
}
