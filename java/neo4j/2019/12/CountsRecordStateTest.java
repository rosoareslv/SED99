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
package org.neo4j.internal.recordstorage;

import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Set;

import org.neo4j.internal.helpers.collection.Iterables;
import org.neo4j.storageengine.api.CountsDelta;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.neo4j.internal.helpers.collection.Iterators.asSet;

class CountsRecordStateTest
{
    @Test
    void shouldReportDifferencesBetweenDifferentStates()
    {
        // given
        CountsRecordState oracle = new CountsRecordState();
        CountsRecordState victim = new CountsRecordState();
        oracle.incrementNodeCount( 17, 5 );
        victim.incrementNodeCount( 17, 3 );
        oracle.incrementNodeCount( 12, 9 );
        victim.incrementNodeCount( 12, 9 );
        oracle.incrementRelationshipCount( 1, 2, 3, 19 );
        victim.incrementRelationshipCount( 1, 2, 3, 22 );
        oracle.incrementRelationshipCount( 1, 4, 3, 25 );
        victim.incrementRelationshipCount( 1, 4, 3, 25 );

        // when
        Set<CountsRecordState.Difference> differences = Iterables.asSet( oracle.verify( victim ) );

        // then
        assertEquals( differences, asSet(
                new CountsRecordState.Difference( CountsDelta.nodeKey( 17 ), 5, 3 ),
                new CountsRecordState.Difference( CountsDelta.relationshipKey( 1, 2, 3 ), 19, 22 )
        ) );
    }

    @Test
    void shouldNotReportAnythingForEqualStates()
    {
        // given
        CountsRecordState oracle = new CountsRecordState();
        CountsRecordState victim = new CountsRecordState();
        oracle.incrementNodeCount( 17, 5 );
        victim.incrementNodeCount( 17, 5 );
        oracle.incrementNodeCount( 12, 9 );
        victim.incrementNodeCount( 12, 9 );
        oracle.incrementRelationshipCount( 1, 4, 3, 25 );
        victim.incrementRelationshipCount( 1, 4, 3, 25 );

        // when
        List<CountsRecordState.Difference> differences = oracle.verify( victim );

        // then
        assertTrue( differences.isEmpty(), differences.toString() );
    }
}
