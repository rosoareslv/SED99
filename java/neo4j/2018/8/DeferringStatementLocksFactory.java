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
package org.neo4j.kernel.impl.locking;

import org.neo4j.configuration.Description;
import org.neo4j.configuration.Internal;
import org.neo4j.configuration.LoadableConfig;
import org.neo4j.graphdb.config.Setting;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.configuration.Settings;

import static java.util.Objects.requireNonNull;
import static org.neo4j.kernel.configuration.Settings.setting;

/**
 * A {@link StatementLocksFactory} that created {@link DeferringStatementLocks} based on the given
 * {@link Locks} and {@link Config}.
 */
public class DeferringStatementLocksFactory implements StatementLocksFactory, LoadableConfig
{
    @Internal
    @Description( "Enable deferring of locks to commit time. This feature weakens the isolation level. " +
                  "It can result in both domain and storage level inconsistencies." )
    public static final Setting<Boolean> deferred_locks_enabled =
            setting( "unsupported.dbms.deferred_locks.enabled", Settings.BOOLEAN, Settings.FALSE );

    private Locks locks;
    private boolean deferredLocksEnabled;

    @Override
    public void initialize( Locks locks, Config config )
    {
        this.locks = requireNonNull( locks );
        this.deferredLocksEnabled = config.get( deferred_locks_enabled );
    }

    @Override
    public StatementLocks newInstance()
    {
        if ( locks == null )
        {
            throw new IllegalStateException( "Factory has not been initialized" );
        }

        Locks.Client client = locks.newClient();
        return deferredLocksEnabled ? new DeferringStatementLocks( client ) : new SimpleStatementLocks( client );
    }
}
