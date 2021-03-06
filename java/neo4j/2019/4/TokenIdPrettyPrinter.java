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
package org.neo4j.token.api;

import org.neo4j.common.TokenNameLookup;

public class TokenIdPrettyPrinter
{
    private TokenIdPrettyPrinter()
    {
    }

    public static String label( int id )
    {
        return id == TokenConstants.ANY_LABEL ? "" : (":label=" + id);
    }

    public static String propertyKey( int id )
    {
        return id == TokenConstants.ANY_PROPERTY_KEY ? "" : (":propertyKey=" + id);
    }

    public static String relationshipType( int id )
    {
        return id == TokenConstants.ANY_RELATIONSHIP_TYPE ? "" : ("[:type=" + id + "]");
    }

    public static String niceProperties( TokenNameLookup tokenNameLookup, int[] propertyIds )
    {
        return niceProperties( tokenNameLookup, propertyIds, "", false );
    }

    public static String niceProperties( TokenNameLookup tokenNameLookup, int[] propertyIds, String prefix,
            boolean useBrackets )
    {
        StringBuilder properties = new StringBuilder();
        if ( useBrackets )
        {
            properties.append( "(" );
        }
        for ( int i = 0; i < propertyIds.length; i++ )
        {
            if ( i > 0 )
            {
                properties.append( ", " );
            }
            properties.append( prefix ).append( tokenNameLookup.propertyKeyGetName( propertyIds[i] ) );
        }
        if ( useBrackets )
        {
            properties.append( ")" );
        }
        return properties.toString();
    }
}
