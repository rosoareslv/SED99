/*
 * Copyright (c) 2002-2017 "Neo Technology,"
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
package org.neo4j.graphdb;

import java.io.File;
import java.util.Collections;
import java.util.List;
import java.util.stream.Stream;

import org.neo4j.graphdb.factory.GraphDatabaseBuilder;
import org.neo4j.graphdb.factory.GraphDatabaseSettings.LabelIndex;
import org.neo4j.kernel.api.impl.labelscan.LuceneLabelScanIndexBuilder;
import org.neo4j.test.rule.DatabaseRule;

import static java.util.stream.Collectors.toList;
import static org.junit.Assert.assertTrue;
import static org.neo4j.graphdb.factory.GraphDatabaseSettings.label_index;
import static org.neo4j.io.fs.FileUtils.deleteRecursively;

public class LuceneLabelScanStoreChaosIT extends LabelScanStoreChaosIT
{
    @Override
    protected DatabaseRule.RestartAction corruptTheLabelScanStoreIndex()
    {
        return ( fs, storeDirectory ) ->
        {
            int filesCorrupted = 0;
            List<File> partitionDirs = labelScanStoreIndexDirectories( storeDirectory );
            for ( File partitionDir : partitionDirs )
            {
                for ( File file : partitionDir.listFiles() )
                {
                    scrambleFile( file );
                    filesCorrupted++;
                }
            }
            assertTrue( "No files found to corrupt", filesCorrupted > 0 );
        };
    }

    @Override
    protected DatabaseRule.RestartAction deleteTheLabelScanStoreIndex()
    {
        return ( fs, storeDirectory ) ->
        {
            List<File> partitionDirs = labelScanStoreIndexDirectories( storeDirectory );
            for ( File dir : partitionDirs )
            {
                assertTrue( "We seem to want to delete the wrong directory here", dir.exists() );
                assertTrue( "No index files to delete", dir.listFiles().length > 0 );
                deleteRecursively( dir );
            }
        };
    }

    private List<File> labelScanStoreIndexDirectories( File storeDirectory )
    {
        File rootDir = new File( new File( new File( new File( storeDirectory, "schema" ), "label" ), "lucene" ),
                LuceneLabelScanIndexBuilder.DEFAULT_INDEX_IDENTIFIER );

        File[] partitionDirs = rootDir.listFiles( File::isDirectory );
        return (partitionDirs == null) ? Collections.emptyList() : Stream.of( partitionDirs ).collect( toList() );
    }

    @Override
    protected void addSpecificConfig( GraphDatabaseBuilder builder )
    {
        builder.setConfig( label_index, LabelIndex.LUCENE.name() );
    }
}
