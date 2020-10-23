/*
 * Copyright (c) 2002-2019 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j.
 *
 * Neo4j is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package org.neo4j.upgrade;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.RuleChain;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameters;

import java.io.File;
import java.io.IOException;
import java.util.Collection;
import java.util.Collections;

import org.neo4j.common.ProgressReporter;
import org.neo4j.configuration.Config;
import org.neo4j.configuration.GraphDatabaseSettings;
import org.neo4j.consistency.checking.full.ConsistencyCheckIncompleteException;
import org.neo4j.dbms.api.DatabaseManagementService;
import org.neo4j.exceptions.KernelException;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.io.layout.Neo4jLayout;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.kernel.api.index.IndexProvider;
import org.neo4j.kernel.impl.store.format.standard.StandardV3_4;
import org.neo4j.kernel.impl.storemigration.IdGeneratorMigrator;
import org.neo4j.kernel.impl.storemigration.LegacyTransactionLogsLocator;
import org.neo4j.kernel.impl.storemigration.MigrationTestUtils;
import org.neo4j.kernel.impl.storemigration.RecordStorageMigrator;
import org.neo4j.kernel.impl.storemigration.RecordStoreVersionCheck;
import org.neo4j.kernel.impl.storemigration.StoreUpgrader;
import org.neo4j.kernel.impl.transaction.log.entry.VersionAwareLogEntryReader;
import org.neo4j.kernel.impl.transaction.log.files.LogFiles;
import org.neo4j.kernel.impl.transaction.log.files.LogFilesBuilder;
import org.neo4j.kernel.recovery.LogTailScanner;
import org.neo4j.logging.NullLogProvider;
import org.neo4j.logging.internal.LogService;
import org.neo4j.logging.internal.NullLogService;
import org.neo4j.monitoring.Monitors;
import org.neo4j.scheduler.JobScheduler;
import org.neo4j.storageengine.api.StorageEngineFactory;
import org.neo4j.storageengine.api.StoreVersionCheck;
import org.neo4j.storageengine.migration.MigrationProgressMonitor;
import org.neo4j.storageengine.migration.SchemaIndexMigrator;
import org.neo4j.storageengine.migration.StoreMigrationParticipant;
import org.neo4j.test.TestDatabaseManagementServiceBuilder;
import org.neo4j.test.rule.PageCacheRule;
import org.neo4j.test.rule.TestDirectory;
import org.neo4j.test.rule.fs.DefaultFileSystemRule;
import org.neo4j.test.scheduler.ThreadPoolJobScheduler;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.neo4j.configuration.GraphDatabaseSettings.DEFAULT_DATABASE_NAME;
import static org.neo4j.configuration.GraphDatabaseSettings.allow_upgrade;
import static org.neo4j.consistency.store.StoreAssertions.assertConsistentStore;
import static org.neo4j.kernel.impl.storemigration.MigrationTestUtils.checkNeoStoreHasDefaultFormatVersion;

@RunWith( Parameterized.class )
public class StoreUpgraderInterruptionTestIT
{
    private final TestDirectory directory = TestDirectory.testDirectory();
    private final DefaultFileSystemRule fileSystemRule = new DefaultFileSystemRule();
    private final PageCacheRule pageCacheRule = new PageCacheRule();

    @Rule
    public RuleChain ruleChain = RuleChain.outerRule( directory )
                                          .around( fileSystemRule ).around( pageCacheRule );

    @Parameterized.Parameter
    public String version;
    private static final Config CONFIG = Config.defaults( GraphDatabaseSettings.pagecache_memory, "8m" );

    @Parameters( name = "{0}" )
    public static Collection<String> versions()
    {
        return Collections.singletonList( StandardV3_4.STORE_VERSION );
    }

    private final FileSystemAbstraction fs = fileSystemRule.get();
    private JobScheduler jobScheduler;
    private Neo4jLayout neo4jLayout;
    private DatabaseLayout workingDatabaseLayout;
    private File prepareDirectory;
    private LegacyTransactionLogsLocator legacyTransactionLogsLocator;

    @Before
    public void setUpLabelScanStore()
    {
        jobScheduler = new ThreadPoolJobScheduler();
        neo4jLayout = Neo4jLayout.of( directory.homeDir() );
        workingDatabaseLayout = neo4jLayout.databaseLayout( DEFAULT_DATABASE_NAME );
        prepareDirectory = directory.directory( "prepare" );
        legacyTransactionLogsLocator = new LegacyTransactionLogsLocator( Config.defaults(), workingDatabaseLayout );
    }

    @After
    public void tearDown() throws Exception
    {
        jobScheduler.close();
    }

    @Test
    public void shouldSucceedWithUpgradeAfterPreviousAttemptDiedDuringMigration()
            throws IOException, ConsistencyCheckIncompleteException
    {
        MigrationTestUtils.prepareSampleLegacyDatabase( version, fs, workingDatabaseLayout.databaseDirectory(), prepareDirectory );
        PageCache pageCache = pageCacheRule.getPageCache( fs );
        RecordStoreVersionCheck versionCheck = new RecordStoreVersionCheck( fs, pageCache, workingDatabaseLayout, NullLogProvider.getInstance(),
                Config.defaults() );
        MigrationProgressMonitor progressMonitor = MigrationProgressMonitor.SILENT;
        LogService logService = NullLogService.getInstance();
        RecordStorageMigrator failingStoreMigrator = new RecordStorageMigrator( fs, pageCache, CONFIG, logService, jobScheduler )
        {
            @Override
            public void migrate( DatabaseLayout directoryLayout, DatabaseLayout migrationLayout,
                    ProgressReporter progressReporter,
                    String versionToMigrateFrom, String versionToMigrateTo ) throws IOException, KernelException
            {
                super.migrate( directoryLayout, migrationLayout, progressReporter, versionToMigrateFrom,
                        versionToMigrateTo );
                throw new RuntimeException( "This upgrade is failing" );
            }
        };

        try
        {
            newUpgrader( versionCheck, progressMonitor, createIndexMigrator(), failingStoreMigrator )
                    .migrateIfNeeded( workingDatabaseLayout );
            fail( "Should throw exception" );
        }
        catch ( RuntimeException e )
        {
            assertEquals( "This upgrade is failing", e.getMessage() );
        }

        RecordStorageMigrator migrator = new RecordStorageMigrator( fs, pageCache, CONFIG, logService, jobScheduler );
        IdGeneratorMigrator idMigrator = new IdGeneratorMigrator( fs, pageCache, CONFIG );
        SchemaIndexMigrator indexMigrator = createIndexMigrator();
        newUpgrader( versionCheck, progressMonitor, indexMigrator, migrator, idMigrator ).migrateIfNeeded( workingDatabaseLayout );

        assertTrue( checkNeoStoreHasDefaultFormatVersion( versionCheck ) );

        // Since consistency checker is in read only mode we need to start/stop db to generate label scan store.
        startStopDatabase( neo4jLayout.homeDirectory() );
        assertConsistentStore( workingDatabaseLayout );
    }

    private SchemaIndexMigrator createIndexMigrator()
    {
        return new SchemaIndexMigrator( "upgrade test indexes", fs, IndexProvider.EMPTY.directoryStructure(), StorageEngineFactory.selectStorageEngine() );
    }

    @Test
    public void shouldSucceedWithUpgradeAfterPreviousAttemptDiedDuringMovingFiles()
            throws IOException, ConsistencyCheckIncompleteException
    {
        MigrationTestUtils.prepareSampleLegacyDatabase( version, fs, workingDatabaseLayout.databaseDirectory(), prepareDirectory );
        PageCache pageCache = pageCacheRule.getPageCache( fs );
        RecordStoreVersionCheck versionCheck = new RecordStoreVersionCheck( fs, pageCache, workingDatabaseLayout, NullLogProvider.getInstance(),
                Config.defaults() );
        MigrationProgressMonitor progressMonitor = MigrationProgressMonitor.SILENT;
        LogService logService = NullLogService.getInstance();
        RecordStorageMigrator failingStoreMigrator = new RecordStorageMigrator( fs, pageCache, CONFIG, logService, jobScheduler )
        {
            @Override
            public void moveMigratedFiles( DatabaseLayout migrationLayout, DatabaseLayout directoryLayout, String versionToUpgradeFrom,
                    String versionToMigrateTo ) throws IOException
            {
                super.moveMigratedFiles( migrationLayout, directoryLayout, versionToUpgradeFrom, versionToMigrateTo );
                throw new RuntimeException( "This upgrade is failing" );
            }
        };
        IdGeneratorMigrator idMigrator = new IdGeneratorMigrator( fs, pageCache, CONFIG );

        try
        {
            newUpgrader( versionCheck, progressMonitor, createIndexMigrator(), failingStoreMigrator, idMigrator )
                    .migrateIfNeeded( workingDatabaseLayout );
            fail( "Should throw exception" );
        }
        catch ( RuntimeException e )
        {
            assertEquals( "This upgrade is failing", e.getMessage() );
        }

        assertTrue( checkNeoStoreHasDefaultFormatVersion( versionCheck ) );

        RecordStorageMigrator migrator = new RecordStorageMigrator( fs, pageCache, CONFIG, logService, jobScheduler );
        newUpgrader( versionCheck, progressMonitor, createIndexMigrator(), migrator, idMigrator ).migrateIfNeeded( workingDatabaseLayout );

        assertTrue( checkNeoStoreHasDefaultFormatVersion( versionCheck ) );

        pageCache.close();

        // Since consistency checker is in read only mode we need to start/stop db to generate label scan store.
        startStopDatabase( neo4jLayout.homeDirectory() );
        assertConsistentStore( workingDatabaseLayout );
    }

    private StoreUpgrader newUpgrader( StoreVersionCheck versionCheck, MigrationProgressMonitor progressMonitor, StoreMigrationParticipant... participants )
            throws IOException
    {
        Config allowUpgrade = Config.defaults( allow_upgrade, true );

        LogFiles logFiles = LogFilesBuilder.logFilesBasedOnlyBuilder( workingDatabaseLayout.databaseDirectory(), fs ).build();
        LogTailScanner logTailScanner = new LogTailScanner( logFiles, new VersionAwareLogEntryReader(), new Monitors() );
        StoreUpgrader upgrader = new StoreUpgrader( versionCheck, progressMonitor, allowUpgrade, fs, NullLogProvider.getInstance(), logTailScanner,
                legacyTransactionLogsLocator );
        for ( StoreMigrationParticipant participant : participants )
        {
            upgrader.addParticipant( participant );
        }
        return upgrader;
    }

    private static void startStopDatabase( File storeDir )
    {
        DatabaseManagementService managementService = new TestDatabaseManagementServiceBuilder( storeDir ).setConfig( allow_upgrade, true ).build();
        managementService.database( DEFAULT_DATABASE_NAME );
        managementService.shutdown();
    }
}
