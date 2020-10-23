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

import java.io.IOException;
import java.util.Optional;

import org.neo4j.causalclustering.catchup.CatchupResult;
import org.neo4j.causalclustering.catchup.storecopy.StoreCopyFailedException;
import org.neo4j.causalclustering.catchup.storecopy.StoreFiles;
import org.neo4j.causalclustering.catchup.storecopy.StoreIdDownloadFailedException;
import org.neo4j.causalclustering.identity.StoreId;
import org.neo4j.helpers.AdvertisedSocketAddress;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.impl.util.OptionalHostnamePort;
import org.neo4j.kernel.lifecycle.LifecycleAdapter;
import org.neo4j.logging.Log;
import org.neo4j.logging.LogProvider;

import static java.lang.String.format;

class CausalClusteringBackupStrategy extends LifecycleAdapter implements BackupStrategy
{
    private final BackupDelegator backupDelegator;
    private final AddressResolver addressResolver;
    private final Log log;
    private final StoreFiles storeFiles;

    CausalClusteringBackupStrategy( BackupDelegator backupDelegator, AddressResolver addressResolver, LogProvider logProvider, StoreFiles storeFiles )
    {
        this.backupDelegator = backupDelegator;
        this.addressResolver = addressResolver;
        this.log = logProvider.getLog( CausalClusteringBackupStrategy.class );
        this.storeFiles = storeFiles;
    }

    @Override
    public Fallible<BackupStageOutcome> performFullBackup( DatabaseLayout targetDatabaseLayout, Config config,
                                                           OptionalHostnamePort userProvidedAddress )
    {
        AdvertisedSocketAddress fromAddress = addressResolver.resolveCorrectCCAddress( config, userProvidedAddress );
        log.info( "Resolved address for catchup protocol is " + fromAddress );
        StoreId storeId;
        try
        {
            storeId = backupDelegator.fetchStoreId( fromAddress );
            log.info( "Remote store id is " + storeId );
        }
        catch ( StoreIdDownloadFailedException e )
        {
            return new Fallible<>( BackupStageOutcome.WRONG_PROTOCOL, e );
        }

        Optional<StoreId> expectedStoreId = readLocalStoreId( targetDatabaseLayout );
        if ( expectedStoreId.isPresent() )
        {
            return new Fallible<>( BackupStageOutcome.FAILURE, new StoreIdDownloadFailedException(
                    format( "Cannot perform a full backup onto preexisting backup. Remote store id was %s but local is %s", storeId, expectedStoreId ) ) );
        }

        try
        {
            backupDelegator.copy( fromAddress, storeId, targetDatabaseLayout );
            return new Fallible<>( BackupStageOutcome.SUCCESS, null );
        }
        catch ( StoreCopyFailedException e )
        {
            return new Fallible<>( BackupStageOutcome.FAILURE, e );
        }
    }

    @Override
    public Fallible<BackupStageOutcome> performIncrementalBackup( DatabaseLayout databaseLayout, Config config,
                                                                  OptionalHostnamePort userProvidedAddress )
    {
        AdvertisedSocketAddress fromAddress = addressResolver.resolveCorrectCCAddress( config, userProvidedAddress );
        log.info( "Resolved address for catchup protocol is " + fromAddress );
        StoreId storeId;
        try
        {
            storeId = backupDelegator.fetchStoreId( fromAddress );
            log.info( "Remote store id is " + storeId );
        }
        catch ( StoreIdDownloadFailedException e )
        {
            return new Fallible<>( BackupStageOutcome.WRONG_PROTOCOL, e );
        }
        Optional<StoreId> expectedStoreId = readLocalStoreId( databaseLayout );
        if ( !expectedStoreId.isPresent() || !expectedStoreId.get().equals( storeId ) )
        {
            return new Fallible<>( BackupStageOutcome.FAILURE,
                    new StoreIdDownloadFailedException( format( "Remote store id was %s but local is %s", storeId, expectedStoreId ) ) );
        }
        return catchup( fromAddress, storeId, databaseLayout );
    }

    @Override
    public void start() throws Throwable
    {
        super.start();
        backupDelegator.start();
    }

    @Override
    public void stop() throws Throwable
    {
        backupDelegator.stop();
        super.stop();
    }

    private Optional<StoreId> readLocalStoreId( DatabaseLayout databaseLayout )
    {
        try
        {
            return Optional.of( storeFiles.readStoreId( databaseLayout ) );
        }
        catch ( IOException e )
        {
            return Optional.empty();
        }
    }

    private Fallible<BackupStageOutcome> catchup( AdvertisedSocketAddress fromAddress, StoreId storeId, DatabaseLayout databaseLayout )
    {
        CatchupResult catchupResult;
        try
        {
            catchupResult = backupDelegator.tryCatchingUp( fromAddress, storeId, databaseLayout );
        }
        catch ( StoreCopyFailedException e )
        {
            return new Fallible<>( BackupStageOutcome.FAILURE, e );
        }
        if ( catchupResult == CatchupResult.SUCCESS_END_OF_STREAM )
        {
            return new Fallible<>( BackupStageOutcome.SUCCESS, null );
        }
        return new Fallible<>( BackupStageOutcome.FAILURE,
                new StoreCopyFailedException( "End state of catchup was not a successful end of stream" ) );
    }
}
