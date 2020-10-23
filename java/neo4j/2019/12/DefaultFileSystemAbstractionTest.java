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
package org.neo4j.io.fs;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.DisabledOnOs;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

import java.io.File;
import java.io.IOException;
import java.util.UUID;

import static java.lang.String.format;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.hamcrest.core.Is.is;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.neo4j.internal.helpers.Numbers.isPowerOfTwo;
import static org.neo4j.io.fs.DefaultFileSystemAbstraction.UNABLE_TO_CREATE_DIRECTORY_FORMAT;
import static org.neo4j.io.fs.FileSystemAbstraction.INVALID_FILE_DESCRIPTOR;

public class DefaultFileSystemAbstractionTest extends FileSystemAbstractionTest
{
    @Override
    protected FileSystemAbstraction buildFileSystemAbstraction()
    {
        return new DefaultFileSystemAbstraction();
    }

    @Test
    @DisabledOnOs( OS.WINDOWS )
    void retrieveFileDescriptor() throws IOException
    {
        File testFile = testDirectory.file( "testFile" );
        try ( StoreChannel storeChannel = fsa.write( testFile ) )
        {
            int fileDescriptor = fsa.getFileDescriptor( storeChannel );
            assertThat( fileDescriptor, greaterThan( 0 ) );
        }
    }

    @Test
    @EnabledOnOs( OS.WINDOWS )
    void retrieveWindowsFileDescriptor() throws IOException
    {
        File testFile = testDirectory.file( "testFile" );
        try ( StoreChannel storeChannel = fsa.write( testFile ) )
        {
            int fileDescriptor = fsa.getFileDescriptor( storeChannel );
            assertThat( fileDescriptor, equalTo( INVALID_FILE_DESCRIPTOR ) );
        }
    }

    @Test
    void retrieveFileDescriptorOnClosedChannel() throws IOException
    {
        File testFile = testDirectory.file( "testFile" );
        StoreChannel escapedChannel = null;
        try ( StoreChannel storeChannel = fsa.write( testFile ) )
        {
            escapedChannel = storeChannel;
        }
        int fileDescriptor = fsa.getFileDescriptor( escapedChannel );
        assertThat( fileDescriptor, equalTo( INVALID_FILE_DESCRIPTOR ) );
    }

    @Test
    void retrieveBlockSize() throws IOException
    {
        var testFile = testDirectory.createFile( "testBlock" );
        long blockSize = fsa.getBlockSize( testFile );
        assertTrue( isPowerOfTwo( blockSize ), "Observed block size: " + blockSize );
        assertThat( blockSize, greaterThanOrEqualTo( 512L ) );
    }

    @Test
    void shouldFailGracefullyWhenPathCannotBeCreated()
    {
        path = new File( testDirectory.homeDir(), String.valueOf( UUID.randomUUID() ) )
        {
            @Override
            public boolean mkdirs()
            {
                return false;
            }
        };

        IOException exception = assertThrows( IOException.class, () -> fsa.mkdirs( path ) );
        assertFalse( fsa.fileExists( path ) );
        String expectedMessage = format( UNABLE_TO_CREATE_DIRECTORY_FORMAT, path );
        assertThat( exception.getMessage(), is( expectedMessage ) );
    }
}
