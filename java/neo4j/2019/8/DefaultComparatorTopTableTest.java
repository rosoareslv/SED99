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
package org.neo4j.cypher.internal;


import org.junit.jupiter.api.Test;

import java.util.Comparator;
import java.util.Iterator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

class DefaultComparatorTopTableTest
{
    private static final Long[] TEST_VALUES = new Long[]{7L, 4L, 5L, 0L, 3L, 4L, 8L, 6L, 1L, 9L, 2L};

    private static final long[] EXPECTED_VALUES = new long[]{0L, 1L, 2L, 3L, 4L, 4L, 5L, 6L, 7L, 8L, 9L};

    private static final Comparator<Long> comparator = Long::compare;

    @Test
    void shouldHandleAddingMoreValuesThanCapacity()
    {
        DefaultComparatorTopTable<Long> table = new DefaultComparatorTopTable<>( comparator, 7 );
        for ( Long i : TEST_VALUES )
        {
            table.add( i );
        }

        table.sort();

        Iterator<Long> iterator = table.iterator();

        for ( int i = 0; i < 7; i++ )
        {
            assertTrue( iterator.hasNext() );
            long value = iterator.next();
            assertEquals( EXPECTED_VALUES[i], value );
        }
        assertFalse( iterator.hasNext() );
    }

    @Test
    void shouldHandleWhenNotCompletelyFilledToCapacity()
    {
        DefaultComparatorTopTable<Long> table = new DefaultComparatorTopTable<>( comparator, 20 );
        for ( Long i : TEST_VALUES )
        {
            table.add( i );
        }

        table.sort();

        Iterator<Long> iterator = table.iterator();

        for ( int i = 0; i < TEST_VALUES.length; i++ )
        {
            assertTrue( iterator.hasNext() );
            long value = iterator.next();
            assertEquals( EXPECTED_VALUES[i], value );
        }
        assertFalse( iterator.hasNext() );
    }

    @Test
    void shouldHandleWhenEmpty()
    {
        DefaultComparatorTopTable<Long> table = new DefaultComparatorTopTable<>( comparator, 10 );

        table.sort();

        Iterator<Long> iterator = table.iterator();

        assertFalse( iterator.hasNext() );
    }

    @Test
    void shouldThrowOnInitializeToZeroCapacity()
    {
        assertThrows( IllegalArgumentException.class,
                () -> new DefaultComparatorTopTable<>( comparator, 0 ) );
    }

    @Test
    void shouldThrowOnInitializeToNegativeCapacity()
    {
        assertThrows( IllegalArgumentException.class,
                () -> new DefaultComparatorTopTable<>( comparator, -1 ) );
    }

    @Test
    void shouldThrowOnSortNotCalledBeforeIterator()
    {
        DefaultComparatorTopTable<Long> table = new DefaultComparatorTopTable<>( comparator, 5 );
        for ( Long i : TEST_VALUES )
        {
            table.add( i );
        }

        // We forgot to call sort() here...

        assertThrows( IllegalStateException.class, table::iterator );
    }
}
