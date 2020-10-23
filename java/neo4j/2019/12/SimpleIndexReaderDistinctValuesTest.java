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
package org.neo4j.kernel.api.impl.schema.reader;

import org.apache.commons.lang3.mutable.MutableInt;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import org.neo4j.configuration.Config;
import org.neo4j.internal.schema.IndexPrototype;
import org.neo4j.io.fs.EphemeralFileSystemAbstraction;
import org.neo4j.kernel.api.impl.schema.LuceneSchemaIndexBuilder;
import org.neo4j.kernel.api.impl.schema.SchemaIndex;
import org.neo4j.kernel.api.impl.schema.writer.LuceneIndexWriter;
import org.neo4j.kernel.api.index.IndexReader;
import org.neo4j.kernel.impl.index.schema.GatheringNodeValueClient;
import org.neo4j.storageengine.api.NodePropertyAccessor;
import org.neo4j.test.extension.Inject;
import org.neo4j.test.extension.RandomExtension;
import org.neo4j.test.extension.testdirectory.EphemeralTestDirectoryExtension;
import org.neo4j.test.rule.RandomRule;
import org.neo4j.test.rule.TestDirectory;
import org.neo4j.values.storable.Value;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.neo4j.internal.schema.SchemaDescriptor.forLabel;
import static org.neo4j.kernel.api.impl.schema.LuceneDocumentStructure.documentRepresentingProperties;
import static org.neo4j.values.storable.Values.stringValue;

@EphemeralTestDirectoryExtension
@ExtendWith( RandomExtension.class )
public class SimpleIndexReaderDistinctValuesTest
{
    @Inject
    public RandomRule random;
    @Inject
    EphemeralFileSystemAbstraction fs;
    @Inject
    TestDirectory directory;

    private SchemaIndex index;

    @BeforeEach
    void setup() throws IOException
    {
        index = LuceneSchemaIndexBuilder.create( IndexPrototype.forSchema( forLabel( 1, 1 ) ).withName( "index_42" ).materialise( 42 ), Config.defaults() )
                .withFileSystem( fs )
                .withIndexRootFolder( directory.homeDir() )
                .build();
        index.create();
        index.open();
    }

    @AfterEach
    void tearDown() throws IOException
    {
        index.close();
    }

    @Test
    void shouldGetDistinctStringValues() throws IOException
    {
        // given
        LuceneIndexWriter writer = index.getIndexWriter();
        Map<Value,MutableInt> expectedCounts = new HashMap<>();
        for ( int i = 0; i < 10_000; i++ )
        {
            Value value = stringValue( String.valueOf( random.nextInt( 1_000 ) ) );
            writer.addDocument( documentRepresentingProperties( i, value ) );
            expectedCounts.computeIfAbsent( value, v -> new MutableInt( 0 ) ).increment();
        }
        index.maybeRefreshBlocking();

        // when/then
        GatheringNodeValueClient client = new GatheringNodeValueClient();
        NodePropertyAccessor propertyAccessor = mock( NodePropertyAccessor.class );
        try ( IndexReader reader = index.getIndexReader() )
        {
            reader.distinctValues( client, propertyAccessor, true );
            while ( client.progressor.next() )
            {
                Value value = client.values[0];
                MutableInt expectedCount = expectedCounts.remove( value );
                assertNotNull( expectedCount );
                assertEquals( expectedCount.intValue(), client.reference );
            }
            assertTrue( expectedCounts.isEmpty() );
        }
        verifyNoMoreInteractions( propertyAccessor );
    }

    @Test
    void shouldCountDistinctValues() throws IOException
    {
        // given
        LuceneIndexWriter writer = index.getIndexWriter();
        int expectedCount = 10_000;
        for ( int i = 0; i < expectedCount; i++ )
        {
            Value value = random.nextTextValue();
            writer.addDocument( documentRepresentingProperties( i, value ) );
        }
        index.maybeRefreshBlocking();

        // when/then
        GatheringNodeValueClient client = new GatheringNodeValueClient();
        NodePropertyAccessor propertyAccessor = mock( NodePropertyAccessor.class );
        try ( IndexReader reader = index.getIndexReader() )
        {
            reader.distinctValues( client, propertyAccessor, true );
            int actualCount = 0;
            while ( client.progressor.next() )
            {
                actualCount += client.reference;
            }
            assertEquals( expectedCount, actualCount );
        }
        verifyNoMoreInteractions( propertyAccessor );
    }
}
