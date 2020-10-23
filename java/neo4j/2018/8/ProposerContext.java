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
package org.neo4j.cluster.protocol.atomicbroadcast.multipaxos;

import java.net.URI;
import java.util.List;

import org.neo4j.cluster.com.message.Message;
import org.neo4j.cluster.protocol.ConfigurationContext;
import org.neo4j.cluster.protocol.LoggingContext;
import org.neo4j.cluster.protocol.TimeoutsContext;
import org.neo4j.cluster.protocol.cluster.ClusterMessage;

/**
 * Context used by {@link ProposerState} state machine.
 */
public interface ProposerContext
    extends TimeoutsContext, LoggingContext, ConfigurationContext
{
    InstanceId newInstanceId( );

    PaxosInstance getPaxosInstance( InstanceId instanceId );

    void pendingValue( Message message );

    void bookInstance( InstanceId instanceId, Message message );

    int nrOfBookedInstances();

    boolean canBookInstance();

    Message getBookedInstance( InstanceId id );

    Message<ProposerMessage> unbookInstance( InstanceId id );

    void patchBookedInstances( ClusterMessage.ConfigurationChangeState value );

    int getMinimumQuorumSize( List<URI> acceptors );

    boolean hasPendingValues();

    Message popPendingValue();

    void leave();

    List<URI> getAcceptors();
}
