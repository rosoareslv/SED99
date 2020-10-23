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

import java.util.function.Supplier;

import org.neo4j.com.monitor.RequestMonitor;
import org.neo4j.kernel.ha.cluster.member.ClusterMember;
import org.neo4j.kernel.impl.transaction.log.ReadableClosablePositionAwareChannel;
import org.neo4j.kernel.impl.transaction.log.entry.LogEntryReader;
import org.neo4j.kernel.lifecycle.LifeSupport;
import org.neo4j.kernel.monitoring.ByteCounterMonitor;
import org.neo4j.kernel.monitoring.Monitors;
import org.neo4j.logging.LogProvider;
import org.neo4j.storageengine.api.StoreId;

public class DefaultSlaveFactory implements SlaveFactory
{
    private final LogProvider logProvider;
    private final Monitors monitors;
    private final int chunkSize;
    private StoreId storeId;
    private final Supplier<LogEntryReader<ReadableClosablePositionAwareChannel>> entryReader;

    public DefaultSlaveFactory( LogProvider logProvider, Monitors monitors, int chunkSize,
            Supplier<LogEntryReader<ReadableClosablePositionAwareChannel>> logEntryReader )
    {
        this.logProvider = logProvider;
        this.monitors = monitors;
        this.chunkSize = chunkSize;
        this.entryReader = logEntryReader;
    }

    @Override
    public Slave newSlave( LifeSupport life, ClusterMember clusterMember, String originHostNameOrIp, int originPort )
    {
        return life.add( new SlaveClient( clusterMember.getInstanceId(), clusterMember.getHAUri().getHost(),
                clusterMember.getHAUri().getPort(), originHostNameOrIp, logProvider, storeId,
                2, // and that's 1 too many, because we push from the master from one thread only anyway
                chunkSize, monitors.newMonitor( ByteCounterMonitor.class, SlaveClient.class.getName() ),
                monitors.newMonitor( RequestMonitor.class, SlaveClient.class.getName() ), entryReader.get() ) );
    }

    @Override
    public void setStoreId( StoreId storeId )
    {
        this.storeId = storeId;
    }
}
