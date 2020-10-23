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
package org.neo4j.kernel.ha.cluster;

import java.io.Externalizable;
import java.io.IOException;
import java.io.ObjectInput;
import java.io.ObjectOutput;

import org.neo4j.cluster.protocol.election.ElectionCredentials;

public final class DefaultElectionCredentials implements ElectionCredentials, Externalizable
{
    private int serverId;
    private long latestTxId;
    private boolean currentWinner;

    // For Externalizable
    public DefaultElectionCredentials()
    {}

    public DefaultElectionCredentials( int serverId, long latestTxId, boolean currentWinner )
    {
        this.serverId = serverId;
        this.latestTxId = latestTxId;
        this.currentWinner = currentWinner;
    }

    @Override
    public int compareTo( ElectionCredentials o )
    {
        DefaultElectionCredentials other = (DefaultElectionCredentials) o;
        if ( this.latestTxId == other.latestTxId )
        {
            // Smaller id means higher priority
            if ( this.currentWinner == other.currentWinner )
            {
                return Integer.compare( other.serverId, this.serverId );
            }
            else
            {
                return other.currentWinner ? -1 : 1;
            }
        }
        else
        {
            return Long.compare( this.latestTxId, other.latestTxId );
        }
    }

    @Override
    public boolean equals( Object obj )
    {
        if ( obj == null )
        {
            return false;
        }
        if ( !(obj instanceof DefaultElectionCredentials ) )
        {
            return false;
        }
        DefaultElectionCredentials other = (DefaultElectionCredentials) obj;
        return other.serverId == this.serverId &&
                other.latestTxId == this.latestTxId &&
                other.currentWinner == this.currentWinner;
    }

    @Override
    public int hashCode()
    {
        return (int) ( 17 * serverId + latestTxId );
    }

    @Override
    public void writeExternal( ObjectOutput out ) throws IOException
    {
        out.writeInt( serverId );
        out.writeLong( latestTxId );
        out.writeBoolean( currentWinner );
    }

    @Override
    public void readExternal( ObjectInput in ) throws IOException
    {
        serverId =  in.readInt();
        latestTxId = in.readLong();
        currentWinner = in.readBoolean();
    }

    @Override
    public String toString()
    {
        return "DefaultElectionCredentials[serverId=" + serverId +
                ", latestTxId=" + latestTxId +
                ", currentWinner=" + currentWinner + "]";
    }
}
