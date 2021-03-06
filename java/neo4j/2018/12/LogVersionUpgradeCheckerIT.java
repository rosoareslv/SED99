/*
 * Copyright (c) 2002-2018 "Neo4j,"
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
package org.neo4j.kernel.impl.transaction.log;

import org.junit.jupiter.api.Test;

import java.io.IOException;

import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Transaction;
import org.neo4j.graphdb.factory.GraphDatabaseSettings;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.storageengine.migration.UpgradeNotAllowedException;
import org.neo4j.kernel.impl.transaction.log.entry.LogEntryVersion;
import org.neo4j.kernel.impl.transaction.log.entry.VersionAwareLogEntryReader;
import org.neo4j.kernel.impl.transaction.log.files.LogFiles;
import org.neo4j.kernel.impl.transaction.log.files.LogFilesBuilder;
import org.neo4j.kernel.lifecycle.Lifespan;
import org.neo4j.kernel.monitoring.Monitors;
import org.neo4j.kernel.recovery.LogTailScanner;
import org.neo4j.test.TestGraphDatabaseFactory;
import org.neo4j.test.extension.Inject;
import org.neo4j.test.extension.pagecache.PageCacheExtension;
import org.neo4j.test.matchers.NestedThrowableMatcher;
import org.neo4j.test.rule.TestDirectory;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.neo4j.graphdb.Label.label;
import static org.neo4j.kernel.impl.transaction.log.entry.LogEntryByteCodes.CHECK_POINT;

@PageCacheExtension
class LogVersionUpgradeCheckerIT
{
    @Inject
    private TestDirectory testDirectory;
    @Inject
    private FileSystemAbstraction fileSystem;
    @Inject
    private PageCache pageCache;

    @Test
    void startAsNormalWhenUpgradeIsNotAllowed()
    {
        createGraphDbAndKillIt();

        // Try to start with upgrading disabled
        final GraphDatabaseService db = new TestGraphDatabaseFactory()
                .setFileSystem( fileSystem )
                .newImpermanentDatabaseBuilder( testDirectory.databaseDir() )
                .setConfig( GraphDatabaseSettings.allow_upgrade, "false" )
                .newGraphDatabase();
        db.shutdown();
    }

    @Test
    void failToStartFromOlderTransactionLogsIfNotAllowed() throws Exception
    {
        createStoreWithLogEntryVersion( LogEntryVersion.V2_3 );

        Exception exception = assertThrows( Exception.class, () ->
        {
            // Try to start with upgrading disabled
            final GraphDatabaseService db =
                    new TestGraphDatabaseFactory().setFileSystem( fileSystem ).newImpermanentDatabaseBuilder( testDirectory.databaseDir() ).setConfig(
                            GraphDatabaseSettings.allow_upgrade, "false" ).newGraphDatabase();
            db.shutdown();
        } );
        assertThat( exception, new NestedThrowableMatcher( UpgradeNotAllowedException.class ) );
    }

    @Test
    void startFromOlderTransactionLogsIfAllowed() throws Exception
    {
        createStoreWithLogEntryVersion( LogEntryVersion.V2_3 );

        // Try to start with upgrading enabled
        final GraphDatabaseService db = new TestGraphDatabaseFactory()
                .setFileSystem( fileSystem )
                .newImpermanentDatabaseBuilder( testDirectory.databaseDir() )
                .setConfig( GraphDatabaseSettings.allow_upgrade, "true" )
                .newGraphDatabase();
        db.shutdown();
    }

    private void createGraphDbAndKillIt()
    {
        final GraphDatabaseService db = new TestGraphDatabaseFactory()
                .setFileSystem( fileSystem )
                .newImpermanentDatabaseBuilder( testDirectory.databaseDir() )
                .newGraphDatabase();

        try ( Transaction tx = db.beginTx() )
        {
            db.createNode( label( "FOO" ) );
            db.createNode( label( "BAR" ) );
            tx.success();
        }

        db.shutdown();
    }

    private void createStoreWithLogEntryVersion( LogEntryVersion logEntryVersion ) throws Exception
    {
        createGraphDbAndKillIt();
        appendCheckpoint( logEntryVersion );
    }

    private void appendCheckpoint( LogEntryVersion logVersion ) throws IOException
    {
        VersionAwareLogEntryReader<ReadableClosablePositionAwareChannel> logEntryReader = new VersionAwareLogEntryReader<>();
        LogFiles logFiles =
                LogFilesBuilder.activeFilesBuilder( testDirectory.databaseLayout(), fileSystem, pageCache ).withLogEntryReader( logEntryReader ).build();
        LogTailScanner tailScanner = new LogTailScanner( logFiles, logEntryReader, new Monitors() );
        LogTailScanner.LogTailInformation tailInformation = tailScanner.getTailInformation();

        try ( Lifespan lifespan = new Lifespan( logFiles ) )
        {
            FlushablePositionAwareChannel channel = logFiles.getLogFile().getWriter();

            LogPosition logPosition = tailInformation.lastCheckPoint.getLogPosition();

            // Fake record
            channel.put( logVersion.byteCode() )
                    .put( CHECK_POINT )
                    .putLong( logPosition.getLogVersion() )
                    .putLong( logPosition.getByteOffset() );

            channel.prepareForFlush().flush();
        }
    }
}
