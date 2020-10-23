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
package org.neo4j.tools.txlog.checktypes;

import org.neo4j.kernel.impl.store.record.AbstractBaseRecord;
import org.neo4j.kernel.impl.transaction.command.Command;

public class CheckTypes
{
    public static final NodeCheckType NODE = new NodeCheckType();
    public static final PropertyCheckType PROPERTY = new PropertyCheckType();
    public static final RelationshipCheckType RELATIONSHIP = new RelationshipCheckType();
    public static final RelationshipGroupCheckType RELATIONSHIP_GROUP = new RelationshipGroupCheckType();
    public static final NeoStoreCheckType NEO_STORE = new NeoStoreCheckType();

    @SuppressWarnings( "unchecked" )
    public static final CheckType<? extends Command, ? extends AbstractBaseRecord>[] CHECK_TYPES =
            new CheckType[]{NODE, PROPERTY, RELATIONSHIP, RELATIONSHIP_GROUP, NEO_STORE};

    private CheckTypes()
    {
    }

    public static <C extends Command,R extends AbstractBaseRecord> CheckType<C,R> fromName( String name )
    {
        for ( CheckType<?,?> checkType : CHECK_TYPES )
        {
            if ( checkType.name().equals( name ) )
            {
                //noinspection unchecked
                return (CheckType<C,R>) checkType;
            }
        }
        throw new IllegalArgumentException( "Unknown check named " + name );
    }
}
