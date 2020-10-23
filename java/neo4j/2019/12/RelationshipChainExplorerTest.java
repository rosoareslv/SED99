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
package org.neo4j.consistency.repair;

import org.apache.commons.lang3.StringUtils;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.Map;

import org.neo4j.configuration.Config;
import org.neo4j.configuration.GraphDatabaseSettings;
import org.neo4j.dbms.api.DatabaseManagementService;
import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Node;
import org.neo4j.graphdb.RelationshipType;
import org.neo4j.graphdb.Transaction;
import org.neo4j.graphdb.config.Setting;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.kernel.impl.store.RecordStore;
import org.neo4j.kernel.impl.store.StoreAccess;
import org.neo4j.kernel.impl.store.record.RelationshipRecord;
import org.neo4j.test.TestDatabaseManagementServiceBuilder;
import org.neo4j.test.extension.Inject;
import org.neo4j.test.extension.Neo4jLayoutExtension;
import org.neo4j.test.extension.pagecache.PageCacheExtension;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.neo4j.configuration.GraphDatabaseSettings.DEFAULT_DATABASE_NAME;
import static org.neo4j.kernel.impl.store.record.RecordLoad.FORCE;
import static org.neo4j.kernel.impl.store.record.RecordLoad.NORMAL;

@PageCacheExtension
@Neo4jLayoutExtension
public class RelationshipChainExplorerTest
{
    private static final int degreeTwoNodes = 10;

    @Inject
    private FileSystemAbstraction fileSystem;
    @Inject
    private PageCache pageCache;
    @Inject
    private DatabaseLayout databaseLayout;

    private StoreAccess store;

    @BeforeEach
    void setupStoreAccess()
    {
        store = createStoreWithOneHighDegreeNodeAndSeveralDegreeTwoNodes( degreeTwoNodes );
    }

    @AfterEach
    void tearDownStoreAccess()
    {
        store.close();
    }

    @Test
    void shouldLoadAllConnectedRelationshipRecordsAndTheirFullChainsOfRelationshipRecords()
    {
        // given
        RecordStore<RelationshipRecord> relationshipStore = store.getRelationshipStore();

        // when
        int relationshipIdInMiddleOfChain = 10;
        RecordSet<RelationshipRecord> records = new RelationshipChainExplorer( relationshipStore )
                .exploreRelationshipRecordChainsToDepthTwo(
                        relationshipStore.getRecord( relationshipIdInMiddleOfChain, relationshipStore.newRecord(), NORMAL ) );

        // then
        assertEquals( degreeTwoNodes * 2, records.size() );
    }

    @Test
    void shouldCopeWithAChainThatReferencesNotInUseZeroValueRecords()
    {
        // given
        RecordStore<RelationshipRecord> relationshipStore = store.getRelationshipStore();
        breakTheChain( relationshipStore );

        // when
        int relationshipIdInMiddleOfChain = 10;
        RecordSet<RelationshipRecord> records = new RelationshipChainExplorer( relationshipStore )
                .exploreRelationshipRecordChainsToDepthTwo(
                        relationshipStore.getRecord( relationshipIdInMiddleOfChain, relationshipStore.newRecord(), NORMAL ) );

        // then
        int recordsInaccessibleBecauseOfBrokenChain = 3;
        assertEquals( degreeTwoNodes * 2 - recordsInaccessibleBecauseOfBrokenChain, records.size() );
    }

    private static void breakTheChain( RecordStore<RelationshipRecord> relationshipStore )
    {
        RelationshipRecord record = relationshipStore.getRecord( 10, relationshipStore.newRecord(), NORMAL );
        long relationshipTowardsEndOfChain = record.getFirstNode();
        while ( record.inUse() && !record.isFirstInFirstChain() )
        {
            record = relationshipStore.getRecord( relationshipTowardsEndOfChain, relationshipStore.newRecord(), FORCE );
            relationshipTowardsEndOfChain = record.getFirstPrevRel();
        }

        relationshipStore.updateRecord( new RelationshipRecord( relationshipTowardsEndOfChain, 0, 0, 0 ) );
    }

    enum TestRelationshipType implements RelationshipType
    {
        CONNECTED
    }

    private StoreAccess createStoreWithOneHighDegreeNodeAndSeveralDegreeTwoNodes( int nDegreeTwoNodes )
    {
        DatabaseManagementService managementService = new TestDatabaseManagementServiceBuilder( databaseLayout ).setConfig( getConfig() ).build();
        GraphDatabaseService database = managementService.database( DEFAULT_DATABASE_NAME );

        try ( Transaction transaction = database.beginTx() )
        {
            Node denseNode = transaction.createNode();
            for ( int i = 0; i < nDegreeTwoNodes; i++ )
            {
                Node degreeTwoNode = transaction.createNode();
                Node leafNode = transaction.createNode();
                if ( i % 2 == 0 )
                {
                    denseNode.createRelationshipTo( degreeTwoNode, TestRelationshipType.CONNECTED );
                }
                else
                {
                    degreeTwoNode.createRelationshipTo( denseNode, TestRelationshipType.CONNECTED );
                }
                degreeTwoNode.createRelationshipTo( leafNode, TestRelationshipType.CONNECTED );
            }
            transaction.commit();
        }
        managementService.shutdown();
        StoreAccess storeAccess = new StoreAccess( fileSystem, pageCache, databaseLayout, Config.defaults() );
        return storeAccess.initialize();
    }

    protected Map<Setting<?>,Object> getConfig()
    {
        return Map.of( GraphDatabaseSettings.record_format, getRecordFormatName() );
    }

    protected String getRecordFormatName()
    {
        return StringUtils.EMPTY;
    }
}
