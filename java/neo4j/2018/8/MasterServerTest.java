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
package org.neo4j.kernel.ha.com.master;

import org.junit.Test;
import org.mockito.Mockito;

import org.neo4j.com.RequestContext;
import org.neo4j.com.Server;
import org.neo4j.com.TxChecksumVerifier;
import org.neo4j.com.monitor.RequestMonitor;
import org.neo4j.kernel.impl.transaction.log.ReadableClosablePositionAwareChannel;
import org.neo4j.kernel.impl.transaction.log.entry.LogEntryReader;
import org.neo4j.kernel.impl.transaction.log.entry.VersionAwareLogEntryReader;
import org.neo4j.kernel.monitoring.ByteCounterMonitor;
import org.neo4j.logging.LogProvider;

import static org.mockito.Mockito.mock;

public class MasterServerTest
{
    @Test
    public void shouldCleanExistentLockSessionOnFinishOffChannel()
    {
        Master master = mock( Master.class );
        ConversationManager conversationManager = mock( ConversationManager.class );
        LogEntryReader<ReadableClosablePositionAwareChannel> logEntryReader = new VersionAwareLogEntryReader<>();
        MasterServer masterServer = new MasterServer( master, mock( LogProvider.class ),
                mock(Server.Configuration.class ), mock( TxChecksumVerifier.class ),
                mock( ByteCounterMonitor.class ), mock( RequestMonitor.class ), conversationManager, logEntryReader );
        RequestContext requestContext = new RequestContext( 1L, 1, 1, 0, 0L );

        masterServer.stopConversation( requestContext );

        Mockito.verify( conversationManager ).stop( requestContext );
    }
}
