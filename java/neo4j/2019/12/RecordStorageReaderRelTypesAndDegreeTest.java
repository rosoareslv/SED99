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
import org.junit.jupiter.api.extension.ExtendWith;

import java.util.HashSet;
import java.util.Set;
import java.util.function.LongConsumer;

import org.neo4j.configuration.GraphDatabaseSettings;
import org.neo4j.exceptions.KernelException;
import org.neo4j.kernel.impl.store.NeoStores;
import org.neo4j.kernel.impl.store.RecordStore;
import org.neo4j.kernel.impl.store.record.AbstractBaseRecord;
import org.neo4j.kernel.impl.store.record.NodeRecord;
import org.neo4j.kernel.impl.store.record.RecordLoad;
import org.neo4j.kernel.impl.store.record.RelationshipGroupRecord;
import org.neo4j.kernel.impl.store.record.RelationshipRecord;
import org.neo4j.storageengine.api.RelationshipDirection;
import org.neo4j.storageengine.api.StorageNodeCursor;
import org.neo4j.storageengine.api.StorageRelationshipGroupCursor;
import org.neo4j.test.extension.Inject;
import org.neo4j.test.extension.RandomExtension;
import org.neo4j.test.rule.RandomRule;
import org.neo4j.test.rule.RecordStorageEngineRule;

import static java.util.Arrays.asList;
import static java.util.Collections.emptySet;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.neo4j.internal.helpers.collection.Iterators.asSet;
import static org.neo4j.internal.helpers.collection.MapUtil.map;
import static org.neo4j.internal.kernel.api.TokenRead.ANY_RELATIONSHIP_TYPE;
import static org.neo4j.internal.kernel.api.TokenRead.NO_TOKEN;
import static org.neo4j.internal.recordstorage.TestRelType.IN;
import static org.neo4j.internal.recordstorage.TestRelType.LOOP;
import static org.neo4j.internal.recordstorage.TestRelType.OUT;
import static org.neo4j.kernel.impl.store.record.Record.NO_NEXT_RELATIONSHIP;
import static org.neo4j.storageengine.api.RelationshipDirection.INCOMING;
import static org.neo4j.storageengine.api.RelationshipDirection.OUTGOING;

@ExtendWith( RandomExtension.class )
class RecordStorageReaderRelTypesAndDegreeTest extends RecordStorageReaderTestBase
{
    private static final int RELATIONSHIPS_COUNT = 20;

    @Inject
    private RandomRule random;

    @Override
    protected RecordStorageEngineRule.Builder modify( RecordStorageEngineRule.Builder builder )
    {
        return builder.setting( GraphDatabaseSettings.dense_node_threshold, RELATIONSHIPS_COUNT );
    }

    @Test
    void degreesForDenseNodeWithPartiallyDeletedRelGroupChain() throws Exception
    {
        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain();

        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( IN );
        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( OUT );
        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( LOOP );

        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT );
        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( OUT, LOOP );
        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( IN, LOOP );

        testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT, LOOP );
    }

    @Test
    void degreesForDenseNodeWithPartiallyDeletedRelChains() throws Exception
    {
        testDegreesForDenseNodeWithPartiallyDeletedRelChains( false, false, false );

        testDegreesForDenseNodeWithPartiallyDeletedRelChains( true, false, false );
        testDegreesForDenseNodeWithPartiallyDeletedRelChains( false, true, false );
        testDegreesForDenseNodeWithPartiallyDeletedRelChains( false, false, true );

        testDegreesForDenseNodeWithPartiallyDeletedRelChains( true, true, false );
        testDegreesForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
        testDegreesForDenseNodeWithPartiallyDeletedRelChains( true, false, true );

        testDegreesForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
    }

    @Test
    void degreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain() throws Exception
    {
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain();

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( IN );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( OUT );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( LOOP );

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( IN, LOOP );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( OUT, LOOP );

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT,
                LOOP );
    }

    @Test
    void degreeByDirectionForDenseNodeWithPartiallyDeletedRelChains() throws Exception
    {
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( false, false, false );

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( true, false, false );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( false, true, false );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( false, false, true );

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( true, true, false );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( true, false, true );

        testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
    }

    @Test
    void degreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain() throws Exception
    {
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain();

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( IN );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( OUT );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( LOOP );

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( OUT, LOOP );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( IN, LOOP );

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain( IN, OUT,
                LOOP );
    }

    @Test
    void degreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains() throws Exception
    {
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( false, false, false );

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( true, false, false );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( false, true, false );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( false, false, true );

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( true, true, false );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( true, false, true );

        testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( true, true, true );
    }

    private void testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelGroupChain( TestRelType... typesToDelete ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        for ( TestRelType type : typesToDelete )
        {
            markRelGroupNotInUse( nodeId, type );
            switch ( type )
            {
            case IN:
                inRelCount = 0;
                break;
            case OUT:
                outRelCount = 0;
                break;
            case LOOP:
                loopRelCount = 0;
                break;
            default:
                throw new IllegalArgumentException( "Unknown type: " + type );
            }
        }

        assertEquals( outRelCount + loopRelCount, degreeForDirection( cursor, OUTGOING ) );
        assertEquals( inRelCount + loopRelCount, degreeForDirection( cursor, INCOMING ) );
        assertEquals( inRelCount + outRelCount + loopRelCount, degreeForDirection( cursor, RelationshipDirection.LOOP) );
    }

    private void testDegreeByDirectionForDenseNodeWithPartiallyDeletedRelChains( boolean modifyInChain,
            boolean modifyOutChain, boolean modifyLoopChain ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        if ( modifyInChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, IN );
        }
        if ( modifyOutChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, OUT );
        }
        if ( modifyLoopChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, LOOP );
        }

        assertEquals( outRelCount + loopRelCount, degreeForDirection( cursor, OUTGOING ) );
        assertEquals( inRelCount + loopRelCount, degreeForDirection( cursor, INCOMING ) );
        assertEquals( inRelCount + outRelCount + loopRelCount, degreeForDirection( cursor, RelationshipDirection.LOOP) );
    }

    private int degreeForDirection( StorageNodeCursor cursor, RelationshipDirection direction )
    {
        return degreeForDirectionAndType( cursor, direction, ANY_RELATIONSHIP_TYPE );
    }

    private int degreeForDirectionAndType( StorageNodeCursor cursor, RelationshipDirection direction, int relType )
    {
        int degree = 0;
        try ( StorageRelationshipGroupCursor groups = storageReader.allocateRelationshipGroupCursor() )
        {
            groups.init( cursor.entityReference(), cursor.relationshipGroupReference(), cursor.isDense() );
            while ( groups.next() )
            {
                if ( relType == ANY_RELATIONSHIP_TYPE || relType == groups.type() )
                {
                    switch ( direction )
                    {
                    case OUTGOING:
                        degree += groups.outgoingCount() + groups.loopCount();
                        break;
                    case INCOMING:
                        degree += groups.incomingCount() + groups.loopCount();
                        break;
                    case LOOP:
                        degree += groups.outgoingCount() + groups.incomingCount() + groups.loopCount();
                        break;
                    default:
                        throw new IllegalArgumentException( direction.name() );
                    }
                }
            }
        }
        return degree;
    }

    private void testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelGroupChain(
            TestRelType... typesToDelete ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        for ( TestRelType type : typesToDelete )
        {
            markRelGroupNotInUse( nodeId, type );
            switch ( type )
            {
            case IN:
                inRelCount = 0;
                break;
            case OUT:
                outRelCount = 0;
                break;
            case LOOP:
                loopRelCount = 0;
                break;
            default:
                throw new IllegalArgumentException( "Unknown type: " + type );
            }
        }

        assertEquals( 0, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( IN ) ) );
        assertEquals( outRelCount, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( OUT ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( LOOP ) ) );

        assertEquals( 0, degreeForDirectionAndType( cursor, INCOMING, relTypeId( OUT ) ) );
        assertEquals( inRelCount, degreeForDirectionAndType( cursor, INCOMING, relTypeId( IN ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, INCOMING, relTypeId( LOOP ) ) );

        assertEquals( inRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( IN ) ) );
        assertEquals( outRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( OUT ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( LOOP ) ) );
    }

    private void testDegreeByDirectionAndTypeForDenseNodeWithPartiallyDeletedRelChains( boolean modifyInChain,
            boolean modifyOutChain, boolean modifyLoopChain ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        if ( modifyInChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, IN );
        }
        if ( modifyOutChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, OUT );
        }
        if ( modifyLoopChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, LOOP );
        }

        assertEquals( 0, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( IN ) ) );
        assertEquals( outRelCount, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( OUT ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, OUTGOING, relTypeId( LOOP ) ) );

        assertEquals( 0, degreeForDirectionAndType( cursor, INCOMING, relTypeId( OUT ) ) );
        assertEquals( inRelCount, degreeForDirectionAndType( cursor, INCOMING, relTypeId( IN ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, INCOMING, relTypeId( LOOP ) ) );

        assertEquals( inRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( IN ) ) );
        assertEquals( outRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( OUT ) ) );
        assertEquals( loopRelCount, degreeForDirectionAndType( cursor, RelationshipDirection.LOOP, relTypeId( LOOP ) ) );
    }

    @Test
    void relationshipTypesForDenseNodeWithPartiallyDeletedRelGroupChain() throws Exception
    {
        testRelationshipTypesForDenseNode( this::noNodeChange,
                asSet( IN, OUT, LOOP ) );

        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, IN ),
                asSet( OUT, LOOP ) );
        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, OUT ),
                asSet( IN, LOOP ) );
        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, LOOP ),
                asSet( IN, OUT ) );

        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, IN, OUT ),
                asSet( LOOP ) );
        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, IN, LOOP ),
                asSet( OUT ) );
        testRelationshipTypesForDenseNode( nodeId -> markRelGroupNotInUse( nodeId, OUT, LOOP ),
                asSet( IN ) );

        testRelationshipTypesForDenseNode(
                nodeId -> markRelGroupNotInUse( nodeId, IN, OUT, LOOP ),
                emptySet() );
    }

    @Test
    void relationshipTypesForDenseNodeWithPartiallyDeletedRelChains() throws Exception
    {
        testRelationshipTypesForDenseNode( this::markRandomRelsNotInUse,
                asSet( IN, OUT, LOOP ) );
    }

    private void testRelationshipTypesForDenseNode( LongConsumer nodeChanger, Set<TestRelType> expectedTypes ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        nodeChanger.accept( nodeId );

        StorageNodeCursor cursor = newCursor( nodeId );

        assertEquals( expectedTypes, relTypes( cursor ) );
    }

    private Set<TestRelType> relTypes( StorageNodeCursor cursor )
    {
        Set<TestRelType> types = new HashSet<>();
        try ( StorageRelationshipGroupCursor groups = storageReader.allocateRelationshipGroupCursor() )
        {
            groups.init( cursor.entityReference(), cursor.relationshipGroupReference(), cursor.isDense() );
            while ( groups.next() )
            {
                types.add( relTypeForId( groups.type() ) );
            }
        }
        return types;
    }

    private void testDegreesForDenseNodeWithPartiallyDeletedRelGroupChain( TestRelType... typesToDelete ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        for ( TestRelType type : typesToDelete )
        {
            markRelGroupNotInUse( nodeId, type );
            switch ( type )
            {
            case IN:
                inRelCount = 0;
                break;
            case OUT:
                outRelCount = 0;
                break;
            case LOOP:
                loopRelCount = 0;
                break;
            default:
                throw new IllegalArgumentException( "Unknown type: " + type );
            }
        }

        Set<TestDegreeItem> expectedDegrees = new HashSet<>();
        if ( outRelCount != 0 )
        {
            expectedDegrees.add( new TestDegreeItem( relTypeId( OUT ), outRelCount, 0 ) );
        }
        if ( inRelCount != 0 )
        {
            expectedDegrees.add( new TestDegreeItem( relTypeId( IN ), 0, inRelCount ) );
        }
        if ( loopRelCount != 0 )
        {
            expectedDegrees.add( new TestDegreeItem( relTypeId( LOOP ), loopRelCount, loopRelCount ) );
        }

        Set<TestDegreeItem> actualDegrees = degrees( cursor );

        assertEquals( expectedDegrees, actualDegrees );
    }

    private void testDegreesForDenseNodeWithPartiallyDeletedRelChains( boolean modifyInChain, boolean modifyOutChain,
            boolean modifyLoopChain ) throws Exception
    {
        int inRelCount = randomRelCount();
        int outRelCount = randomRelCount();
        int loopRelCount = randomRelCount();

        long nodeId = createNode( inRelCount, outRelCount, loopRelCount );
        StorageNodeCursor cursor = newCursor( nodeId );

        if ( modifyInChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, IN );
        }
        if ( modifyOutChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, OUT );
        }
        if ( modifyLoopChain )
        {
            markRandomRelsInGroupNotInUse( nodeId, LOOP );
        }

        Set<TestDegreeItem> expectedDegrees = new HashSet<>(
                asList( new TestDegreeItem( relTypeId( OUT ), outRelCount, 0 ),
                        new TestDegreeItem( relTypeId( IN ), 0, inRelCount ),
                        new TestDegreeItem( relTypeId( LOOP ), loopRelCount, loopRelCount ) ) );

        Set<TestDegreeItem> actualDegrees = degrees( cursor );

        assertEquals( expectedDegrees, actualDegrees );
    }

    private Set<TestDegreeItem> degrees( StorageNodeCursor nodeCursor )
    {
        Set<TestDegreeItem> degrees = new HashSet<>();
        try ( StorageRelationshipGroupCursor groups = storageReader.allocateRelationshipGroupCursor() )
        {
            groups.init( nodeCursor.entityReference(), nodeCursor.relationshipGroupReference(), nodeCursor.isDense() );
            while ( groups.next() )
            {
                degrees.add( new TestDegreeItem( groups.type(), groups.outgoingCount() + groups.loopCount(), groups.incomingCount() + groups.loopCount() ) );
            }
        }
        return degrees;
    }

    private StorageNodeCursor newCursor( long nodeId )
    {
        StorageNodeCursor nodeCursor = storageReader.allocateNodeCursor();
        nodeCursor.single( nodeId );
        assertTrue( nodeCursor.next() );
        return nodeCursor;
    }

    private void noNodeChange( long nodeId )
    {
    }

    private void markRandomRelsNotInUse( long nodeId )
    {
        for ( TestRelType type : TestRelType.values() )
        {
            markRandomRelsInGroupNotInUse( nodeId, type );
        }
    }

    private void markRandomRelsInGroupNotInUse( long nodeId, TestRelType type )
    {
        NodeRecord node = getNodeRecord( nodeId );
        assertTrue( node.isDense() );

        long relGroupId = node.getNextRel();
        while ( relGroupId != NO_NEXT_RELATIONSHIP.intValue() )
        {
            RelationshipGroupRecord relGroup = getRelGroupRecord( relGroupId );

            if ( type == relTypeForId( relGroup.getType() ) )
            {
                markRandomRelsInChainNotInUse( relGroup.getFirstOut() );
                markRandomRelsInChainNotInUse( relGroup.getFirstIn() );
                markRandomRelsInChainNotInUse( relGroup.getFirstLoop() );
                return;
            }

            relGroupId = relGroup.getNext();
        }

        throw new IllegalStateException( "No relationship group with type: " + type + " found" );
    }

    private void markRandomRelsInChainNotInUse( long relId )
    {
        if ( relId != NO_NEXT_RELATIONSHIP.intValue() )
        {
            RelationshipRecord record = getRelRecord( relId );

            boolean shouldBeMarked = random.nextBoolean();
            if ( shouldBeMarked )
            {
                record.setInUse( false );
                update( record );
            }

            markRandomRelsInChainNotInUse( record.getFirstNextRel() );
            boolean isLoopRelationship = record.getFirstNextRel() == record.getSecondNextRel();
            if ( !isLoopRelationship )
            {
                markRandomRelsInChainNotInUse( record.getSecondNextRel() );
            }
        }
    }

    private void markRelGroupNotInUse( long nodeId, TestRelType... types )
    {
        NodeRecord node = getNodeRecord( nodeId );
        assertTrue( node.isDense() );

        Set<TestRelType> typesToRemove = asSet( types );

        long relGroupId = node.getNextRel();
        while ( relGroupId != NO_NEXT_RELATIONSHIP.intValue() )
        {
            RelationshipGroupRecord relGroup = getRelGroupRecord( relGroupId );
            TestRelType type = relTypeForId( relGroup.getType() );

            if ( typesToRemove.contains( type ) )
            {
                relGroup.setInUse( false );
                update( relGroup );
            }

            relGroupId = relGroup.getNext();
        }
    }

    private int relTypeId( TestRelType type )
    {
        int id = relationshipTypeId( type );
        assertNotEquals( NO_TOKEN, id );
        return id;
    }

    private long createNode( int inRelCount, int outRelCount, int loopRelCount ) throws Exception
    {
        long nodeId = createNode( map() );
        for ( int i = 0; i < inRelCount; i++ )
        {
            createRelationship( createNode( map() ), nodeId, IN );
        }
        for ( int i = 0; i < outRelCount; i++ )
        {
            createRelationship( nodeId, createNode( map() ), OUT );
        }
        for ( int i = 0; i < loopRelCount; i++ )
        {
            createRelationship( nodeId, nodeId, LOOP );
        }
        return nodeId;
    }

    private TestRelType relTypeForId( int id )
    {
        try
        {
            return TestRelType.valueOf( relationshipType( id ) );
        }
        catch ( KernelException e )
        {
            throw new RuntimeException( e );
        }
    }

    private static <R extends AbstractBaseRecord> R getRecord( RecordStore<R> store, long id )
    {
        return store.getRecord( id, store.newRecord(), RecordLoad.FORCE );
    }

    private NodeRecord getNodeRecord( long id )
    {
        return getRecord( resolveNeoStores().getNodeStore(), id );
    }

    private RelationshipRecord getRelRecord( long id )
    {
        return getRecord( resolveNeoStores().getRelationshipStore(), id );
    }

    private RelationshipGroupRecord getRelGroupRecord( long id )
    {
        return getRecord( resolveNeoStores().getRelationshipGroupStore(), id );
    }

    private void update( RelationshipGroupRecord record )
    {
        resolveNeoStores().getRelationshipGroupStore().updateRecord( record );
    }

    private void update( RelationshipRecord record )
    {
        resolveNeoStores().getRelationshipStore().updateRecord( record );
    }

    private NeoStores resolveNeoStores()
    {
        return storageEngine.testAccessNeoStores();
    }

    private int randomRelCount()
    {
        return RELATIONSHIPS_COUNT + random.nextInt( 20 );
    }
}
