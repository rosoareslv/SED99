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
package org.neo4j.cluster;

import java.io.Externalizable;
import java.io.IOException;
import java.io.ObjectInput;
import java.io.ObjectOutput;
import java.net.URI;

import static org.neo4j.helpers.Uris.parameter;

/**
 * Represents the concept of the cluster wide unique id of an instance. The
 * main requirement is total order over the instances of the class, currently
 * implemented by an encapsulated integer.
 * It is also expected to be serializable, as it's transmitted over the wire
 * as part of messages between instances.
 */
public class InstanceId implements Externalizable, Comparable<InstanceId>
{
    public static final InstanceId NONE = new InstanceId( Integer.MIN_VALUE );

    private int serverId;

    public InstanceId()
    {}

    public InstanceId( int serverId )
    {
        this.serverId = serverId;
    }

    @Override
    public int compareTo( InstanceId o )
    {
        return Integer.compare( serverId, o.serverId );
    }

    @Override
    public int hashCode()
    {
        return serverId;
    }

    @Override
    public String toString()
    {
        return Integer.toString( serverId );
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

        InstanceId instanceId1 = (InstanceId) o;

        return serverId == instanceId1.serverId;
    }

    @Override
    public void writeExternal( ObjectOutput out ) throws IOException
    {
        out.writeInt( serverId );
    }

    @Override
    public void readExternal( ObjectInput in ) throws IOException
    {
        serverId = in.readInt();
    }

    public int toIntegerIndex()
    {
        return serverId;
    }

    public String instanceNameFromURI( URI member )
    {
        String name = member == null ? null : parameter( "memberName" ).apply( member );
        return name == null ? toString() : name;
    }
}
