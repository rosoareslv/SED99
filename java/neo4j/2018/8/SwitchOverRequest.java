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
package org.neo4j.causalclustering.protocol.handshake;

import java.util.List;
import java.util.Objects;

import org.neo4j.helpers.collection.Pair;

public class SwitchOverRequest implements ServerMessage
{
    private final String protocolName;
    private final Integer version;
    private final List<Pair<String,String>> modifierProtocols;

    public SwitchOverRequest( String applicationProtocolName, int applicationProtocolVersion, List<Pair<String,String>> modifierProtocols )
    {
        this.protocolName = applicationProtocolName;
        this.version = applicationProtocolVersion;
        this.modifierProtocols = modifierProtocols;
    }

    @Override
    public void dispatch( ServerMessageHandler handler )
    {
        handler.handle( this );
    }

    public String protocolName()
    {
        return protocolName;
    }

    public List<Pair<String,String>> modifierProtocols()
    {
        return modifierProtocols;
    }

    public int version()
    {
        return version;
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
        SwitchOverRequest that = (SwitchOverRequest) o;
        return Objects.equals( version, that.version ) &&
                Objects.equals( protocolName, that.protocolName ) &&
                Objects.equals( modifierProtocols, that.modifierProtocols );
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( protocolName, version, modifierProtocols );
    }

    @Override
    public String toString()
    {
        return "SwitchOverRequest{" + "protocolName='" + protocolName + '\'' + ", version=" + version + ", modifierProtocols=" + modifierProtocols + '}';
    }
}
