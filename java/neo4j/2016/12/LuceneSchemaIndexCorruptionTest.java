/*
 * Copyright (c) 2002-2016 "Neo Technology,"
 * Network Engine for Objects in Lund AB [http://neotechnology.com]
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
package org.neo4j.kernel.api.impl.schema;

import org.apache.lucene.index.CorruptIndexException;
import org.hamcrest.CoreMatchers;
import org.junit.Rule;
import org.junit.Test;

import java.io.EOFException;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.UncheckedIOException;

import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.kernel.api.impl.index.storage.DirectoryFactory;
import org.neo4j.kernel.api.impl.index.storage.IndexStorageFactory;
import org.neo4j.kernel.api.impl.index.storage.PartitionedIndexStorage;
import org.neo4j.kernel.api.index.InternalIndexState;
import org.neo4j.test.rule.TestDirectory;
import org.neo4j.test.rule.fs.EphemeralFileSystemRule;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.impl.factory.OperationalMode;
import org.neo4j.logging.AssertableLogProvider;

import static java.util.Collections.singletonList;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.sameInstance;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.neo4j.logging.AssertableLogProvider.inLog;

public class LuceneSchemaIndexCorruptionTest
{
    @Rule
    public final TestDirectory testDirectory = TestDirectory.testDirectory();
    @Rule
    public final EphemeralFileSystemRule fs = new EphemeralFileSystemRule();
    private final AssertableLogProvider logProvider = new AssertableLogProvider();

    @Test
    public void shouldRequestIndexPopulationIfTheIndexIsCorrupt() throws Exception
    {
        // Given
        long faultyIndexId = 1;
        CorruptIndexException error = new CorruptIndexException( "It's broken.", "" );

        LuceneSchemaIndexProvider provider = newFaultySchemaIndexProvider( faultyIndexId, error );

        // When
        InternalIndexState initialState = provider.getInitialState( faultyIndexId );

        // Then
        assertThat( initialState, equalTo(InternalIndexState.POPULATING) );
        logProvider.assertAtLeastOnce( loggedException( error ) );
    }

    @Test
    public void shouldRequestIndexPopulationFailingWithFileNotFoundException() throws Exception
    {
        // Given
        long faultyIndexId = 1;
        FileNotFoundException error = new FileNotFoundException( "/some/path/somewhere" );

        LuceneSchemaIndexProvider provider = newFaultySchemaIndexProvider( faultyIndexId, error );

        // When
        InternalIndexState initialState = provider.getInitialState( faultyIndexId );

        // Then
        assertThat( initialState, equalTo(InternalIndexState.POPULATING) );
        logProvider.assertAtLeastOnce( loggedException( error ) );
    }

    @Test
    public void shouldRequestIndexPopulationWhenFailingWithEOFException() throws Exception
    {
        // Given
        long faultyIndexId = 1;
        EOFException error = new EOFException( "/some/path/somewhere" );

        LuceneSchemaIndexProvider provider = newFaultySchemaIndexProvider( faultyIndexId, error );

        // When
        InternalIndexState initialState = provider.getInitialState( faultyIndexId );

        // Then
        assertThat( initialState, equalTo(InternalIndexState.POPULATING) );
        logProvider.assertAtLeastOnce( loggedException( error ) );
    }

    private LuceneSchemaIndexProvider newFaultySchemaIndexProvider( long faultyIndexId, Exception error )
    {
        DirectoryFactory directoryFactory = mock( DirectoryFactory.class );
        File indexRootFolder = testDirectory.graphDbDir();
        FaultyIndexStorageFactory storageFactory = new FaultyIndexStorageFactory( faultyIndexId, error,
                directoryFactory, indexRootFolder );
        return new LuceneSchemaIndexProvider( fs.get(), directoryFactory, indexRootFolder, logProvider,
                Config.defaults(), OperationalMode.single )
        {
            @Override
            protected IndexStorageFactory buildIndexStorageFactory( FileSystemAbstraction fileSystem,
                                                                    DirectoryFactory directoryFactory,
                                                                    File schemaIndexStoreFolder )
            {
                return storageFactory;
            }
        };
    }

    private class FaultyIndexStorageFactory extends IndexStorageFactory
    {
        final long faultyIndexId;
        final Exception error;

        FaultyIndexStorageFactory( long faultyIndexId, Exception error, DirectoryFactory directoryFactory,
                                   File indexRootFolder )
        {
            super( directoryFactory, fs.get(), indexRootFolder );
            this.faultyIndexId = faultyIndexId;
            this.error = error;
        }

        @Override
        public PartitionedIndexStorage indexStorageOf( long indexId, boolean archiveFailed )
        {
            return indexId == faultyIndexId ? newFaultyPartitionedIndexStorage() : super.indexStorageOf( indexId, archiveFailed );
        }

        PartitionedIndexStorage newFaultyPartitionedIndexStorage()
        {
            try
            {
                PartitionedIndexStorage storage = mock( PartitionedIndexStorage.class );
                when( storage.listFolders() ).thenReturn( singletonList( new File( "/some/path/somewhere/1" ) ) );
                when( storage.openDirectory( any() ) ).thenThrow( error );
                return storage;
            }
            catch ( IOException e )
            {
                throw new UncheckedIOException( e );
            }
        }
    }

    private static AssertableLogProvider.LogMatcher loggedException( Throwable exception )
    {
        return inLog( CoreMatchers.any( String.class ) )
                .error( CoreMatchers.any( String.class ), sameInstance( exception ) );
    }
}
