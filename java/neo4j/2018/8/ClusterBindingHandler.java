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
package org.neo4j.causalclustering.core;

import java.util.Objects;

import org.neo4j.causalclustering.core.consensus.RaftMessages;
import org.neo4j.causalclustering.identity.ClusterId;
import org.neo4j.causalclustering.messaging.ComposableMessageHandler;
import org.neo4j.causalclustering.messaging.LifecycleMessageHandler;
import org.neo4j.logging.Log;
import org.neo4j.logging.LogProvider;

public class ClusterBindingHandler implements LifecycleMessageHandler<RaftMessages.ReceivedInstantClusterIdAwareMessage<?>>
{
    private final LifecycleMessageHandler<RaftMessages.ReceivedInstantClusterIdAwareMessage<?>> delegateHandler;
    private volatile ClusterId boundClusterId;
    private final Log log;

    public ClusterBindingHandler( LifecycleMessageHandler<RaftMessages.ReceivedInstantClusterIdAwareMessage<?>> delegateHandler, LogProvider logProvider )
    {
        this.delegateHandler = delegateHandler;
        log = logProvider.getLog( getClass() );
    }

    public static ComposableMessageHandler composable( LogProvider logProvider )
    {
        return delegate -> new ClusterBindingHandler( delegate, logProvider );
    }

    @Override
    public void start( ClusterId clusterId ) throws Throwable
    {
        this.boundClusterId = clusterId;
        delegateHandler.start( clusterId );
    }

    @Override
    public void stop() throws Throwable
    {
        this.boundClusterId = null;
        delegateHandler.stop();
    }

    @Override
    public void handle( RaftMessages.ReceivedInstantClusterIdAwareMessage<?> message )
    {
        if ( Objects.isNull( boundClusterId ) )
        {
            log.debug( "Message handling has been stopped, dropping the message: %s", message.message() );
        }
        else if ( !Objects.equals( boundClusterId, message.clusterId() ) )
        {
            log.info( "Discarding message[%s] owing to mismatched clusterId. Expected: %s, Encountered: %s",
                    message.message(), boundClusterId, message.clusterId() );
        }
        else
        {
            delegateHandler.handle( message );
        }
    }
}
