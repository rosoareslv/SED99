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
package org.neo4j.causalclustering.catchup.storecopy;

import java.util.Objects;

public class FileHeader
{
    private final String fileName;
    private final int requiredAlignment;

    public FileHeader( String fileName )
    {
        // A required alignment of 1 basically means that any alignment will do.
        this( fileName, 1 );
    }

    public FileHeader( String fileName, int requiredAlignment )
    {
        this.fileName = fileName;
        this.requiredAlignment = requiredAlignment;
    }

    public String fileName()
    {
        return fileName;
    }

    public int requiredAlignment()
    {
        return requiredAlignment;
    }

    @Override
    public boolean equals( Object o )
    {
        if ( this == o )
        {
            return true;
        }
        if ( o == null || getClass() != o.getClass() )
        {
            return false;
        }
        FileHeader that = (FileHeader) o;
        return requiredAlignment == that.requiredAlignment && Objects.equals( fileName, that.fileName );
    }

    @Override
    public int hashCode()
    {
        return Objects.hash( fileName, requiredAlignment );
    }

    @Override
    public String toString()
    {
        return "FileHeader{" + "fileName='" + fileName + '\'' + ", requiredAlignment=" + requiredAlignment + '}';
    }
}
