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
package org.neo4j.kernel.ha;

import org.junit.Rule;
import org.junit.Test;

import java.io.File;
import java.util.function.LongSupplier;

import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.io.pagecache.tracing.cursor.context.EmptyVersionContextSupplier;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.ha.transaction.OnDiskLastTxIdGetter;
import org.neo4j.kernel.impl.store.NeoStores;
import org.neo4j.kernel.impl.store.StoreFactory;
import org.neo4j.kernel.impl.store.id.DefaultIdGeneratorFactory;
import org.neo4j.kernel.impl.transaction.log.TransactionIdStore;
import org.neo4j.logging.NullLogProvider;
import org.neo4j.test.rule.PageCacheRule;
import org.neo4j.test.rule.fs.EphemeralFileSystemRule;

import static org.junit.Assert.assertEquals;

public class OnDiskLastTxIdGetterTest
{
    @Rule
    public PageCacheRule pageCacheRule = new PageCacheRule();
    @Rule
    public EphemeralFileSystemRule fs = new EphemeralFileSystemRule();

    @Test
    public void testGetLastTxIdNoFilePresent()
    {
        // This is a sign that we have some bad coupling on our hands.
        // We currently have to do this because of our lifecycle and construction ordering.
        OnDiskLastTxIdGetter getter = new OnDiskLastTxIdGetter( () -> 13L );
        assertEquals( 13L, getter.getLastTxId() );
    }

    @Test
    public void lastTransactionIdIsBaseTxIdWhileNeoStoresAreStopped()
    {
        final StoreFactory storeFactory = new StoreFactory( DatabaseLayout.of( new File( "store" ) ),
                Config.defaults(), new DefaultIdGeneratorFactory( fs.get() ), pageCacheRule.getPageCache( fs.get() ), fs.get(),
                NullLogProvider.getInstance(), EmptyVersionContextSupplier.EMPTY );
        final NeoStores neoStores = storeFactory.openAllNeoStores( true );
        neoStores.close();

        LongSupplier supplier = () -> neoStores.getMetaDataStore().getLastCommittedTransactionId();
        OnDiskLastTxIdGetter diskLastTxIdGetter = new OnDiskLastTxIdGetter( supplier );
        assertEquals( TransactionIdStore.BASE_TX_ID, diskLastTxIdGetter.getLastTxId() );
    }
}
