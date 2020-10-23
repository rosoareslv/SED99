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

import org.junit.Test;

import org.neo4j.kernel.impl.store.record.PropertyRecord;

import static org.junit.Assert.assertTrue;

public class PropertyCheckTypeTest
{
    @Test
    public void inUseRecordEquality()
    {
        PropertyRecord record1 = new PropertyRecord( 1 );
        record1.initialize( true, 1, 2 );
        record1.setSecondaryUnitId( 42 );

        PropertyRecord record2 = record1.clone();

        PropertyCheckType check = new PropertyCheckType();

        assertTrue( check.equal( record1, record2 ) );
    }

    @Test
    public void notInUseRecordEquality()
    {
        PropertyRecord record1 = new PropertyRecord( 1 );
        record1.initialize( false, 1, 2 );
        record1.setSecondaryUnitId( 42 );

        PropertyRecord record2 = new PropertyRecord( 1 );
        record2.initialize( false, 11, 22 );
        record2.setSecondaryUnitId( 24 );

        PropertyCheckType check = new PropertyCheckType();

        assertTrue( check.equal( record1, record2 ) );
    }
}
