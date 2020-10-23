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
package org.neo4j.causalclustering.readreplica;

import java.util.stream.Stream;

import org.neo4j.kernel.api.exceptions.ReadOnlyDbException;
import org.neo4j.kernel.impl.locking.ActiveLock;
import org.neo4j.kernel.impl.locking.Locks;
import org.neo4j.storageengine.api.lock.AcquireLockTimeoutException;
import org.neo4j.storageengine.api.lock.LockTracer;
import org.neo4j.storageengine.api.lock.ResourceType;

public class ReadReplicaLockManager implements Locks
{
    @Override
    public Locks.Client newClient()
    {
        return new Client();
    }

    @Override
    public void accept( Visitor visitor )
    {
    }

    @Override
    public void close()
    {
    }

    private class Client implements Locks.Client
    {
        @Override
        public void acquireShared( LockTracer tracer, ResourceType resourceType, long... resourceIds ) throws AcquireLockTimeoutException
        {
        }

        @Override
        public void acquireExclusive( LockTracer tracer, ResourceType resourceType, long... resourceIds ) throws AcquireLockTimeoutException
        {
            throw new RuntimeException( new ReadOnlyDbException() );
        }

        @Override
        public boolean tryExclusiveLock( ResourceType resourceType, long resourceId )
        {
            throw new RuntimeException( new ReadOnlyDbException() );
        }

        @Override
        public boolean trySharedLock( ResourceType resourceType, long resourceId )
        {
            return false;
        }

        @Override
        public boolean reEnterShared( ResourceType resourceType, long resourceId )
        {
            return false;
        }

        @Override
        public boolean reEnterExclusive( ResourceType resourceType, long resourceId )
        {
            throw new IllegalStateException( "Should never happen" );
        }

        @Override
        public void releaseShared( ResourceType resourceType, long... resourceIds )
        {
        }

        @Override
        public void releaseExclusive( ResourceType resourceType, long... resourceIds )
        {
            throw new IllegalStateException( "Should never happen" );
        }

        @Override
        public void prepare()
        {
        }

        @Override
        public void stop()
        {
        }

        @Override
        public void close()
        {
        }

        @Override
        public int getLockSessionId()
        {
            return 0;
        }

        @Override
        public Stream<ActiveLock> activeLocks()
        {
            return Stream.empty();
        }

        @Override
        public long activeLockCount()
        {
            return 0;
        }
    }
}
