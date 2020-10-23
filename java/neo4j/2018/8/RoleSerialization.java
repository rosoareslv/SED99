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
package org.neo4j.server.security.enterprise.auth;

import java.util.SortedSet;
import java.util.TreeSet;

import org.neo4j.server.security.auth.FileRepositorySerializer;
import org.neo4j.server.security.auth.exception.FormatException;

import static java.lang.String.format;

/**
 * Serializes role authorization and authentication data to a format similar to unix passwd files.
 */
public class RoleSerialization extends FileRepositorySerializer<RoleRecord>
{
    private static final String roleSeparator = ":";
    private static final String userSeparator = ",";

    @Override
    protected String serialize( RoleRecord role )
    {
        return String.join( roleSeparator, role.name(), String.join( userSeparator, role.users() ) );
    }

    @Override
    protected RoleRecord deserializeRecord( String line, int lineNumber ) throws FormatException
    {
        String[] parts = line.split( roleSeparator, -1 );
        if ( parts.length != 2 )
        {
            throw new FormatException( format( "wrong number of line fields [line %d]", lineNumber ) );
        }
        return new RoleRecord.Builder()
                .withName( parts[0] )
                .withUsers( deserializeUsers( parts[1] ) )
                .build();
    }

    private SortedSet<String> deserializeUsers( String part )
    {
        String[] splits = part.split( userSeparator, -1 );

        SortedSet<String> users = new TreeSet<>();

        for ( String user : splits )
        {
            if ( !user.trim().isEmpty() )
            {
                users.add( user );
            }
        }

        return users;
    }
}
