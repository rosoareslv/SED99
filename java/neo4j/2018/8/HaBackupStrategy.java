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
package org.neo4j.backup.impl;

import org.neo4j.com.ComException;
import org.neo4j.helpers.HostnamePort;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.impl.store.MismatchingStoreIdException;
import org.neo4j.kernel.impl.util.OptionalHostnamePort;
import org.neo4j.kernel.lifecycle.LifecycleAdapter;
import org.neo4j.logging.Log;
import org.neo4j.logging.LogProvider;

class HaBackupStrategy extends LifecycleAdapter implements BackupStrategy
{
    private final BackupProtocolService backupProtocolService;
    private final AddressResolver addressResolver;
    private final long timeout;
    private final Log log;

    HaBackupStrategy( BackupProtocolService backupProtocolService, AddressResolver addressResolver, LogProvider logProvider, long timeout )
    {
        this.backupProtocolService = backupProtocolService;
        this.addressResolver = addressResolver;
        this.timeout = timeout;
        this.log = logProvider.getLog( HaBackupStrategy.class );
    }

    @Override
    public Fallible<BackupStageOutcome> performIncrementalBackup( DatabaseLayout targetDatabaseLayout, Config config, OptionalHostnamePort fromAddress )
    {
        HostnamePort resolvedAddress = addressResolver.resolveCorrectHAAddress( config, fromAddress );
        log.info( "Resolved address for backup protocol is " + resolvedAddress );
        try
        {
            String host = resolvedAddress.getHost();
            int port = resolvedAddress.getPort();
            backupProtocolService.doIncrementalBackup(
                    host, port, targetDatabaseLayout, ConsistencyCheck.NONE, timeout, config );
            return new Fallible<>( BackupStageOutcome.SUCCESS, null );
        }
        catch ( MismatchingStoreIdException e )
        {
            return new Fallible<>( BackupStageOutcome.UNRECOVERABLE_FAILURE, e );
        }
        catch ( RuntimeException e )
        {
            return new Fallible<>( BackupStageOutcome.FAILURE, e );
        }
    }

    @Override
    public Fallible<BackupStageOutcome> performFullBackup( DatabaseLayout targetDatabaseLayout, Config config,
                                                           OptionalHostnamePort userProvidedAddress )
    {
        HostnamePort fromAddress = addressResolver.resolveCorrectHAAddress( config, userProvidedAddress );
        log.info( "Resolved address for backup protocol is " + fromAddress );
        ConsistencyCheck consistencyCheck = ConsistencyCheck.NONE;
        boolean forensics = false;
        try
        {
            String host = fromAddress.getHost();
            int port = fromAddress.getPort();
            backupProtocolService.doFullBackup(
                    host, port, targetDatabaseLayout, consistencyCheck, config, timeout, forensics );
            return new Fallible<>( BackupStageOutcome.SUCCESS, null );
        }
        catch ( ComException e )
        {
            return new Fallible<>( BackupStageOutcome.WRONG_PROTOCOL, e );
        }
    }
}
