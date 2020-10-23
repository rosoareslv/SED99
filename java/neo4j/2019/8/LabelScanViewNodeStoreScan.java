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
package org.neo4j.kernel.impl.transaction.state.storeview;

import java.util.function.IntPredicate;

import org.neo4j.internal.helpers.collection.Visitor;
import org.neo4j.internal.index.label.LabelScanStore;
import org.neo4j.lock.LockService;
import org.neo4j.storageengine.api.EntityUpdates;
import org.neo4j.storageengine.api.NodeLabelUpdate;
import org.neo4j.storageengine.api.StorageReader;

/**
 * Store scan view that will try to minimize amount of scanned nodes by using label scan store {@link LabelScanStore}
 * as a source of known labeled node ids.
 * @param <FAILURE> type of exception thrown on failure
 */
public class LabelScanViewNodeStoreScan<FAILURE extends Exception> extends StoreViewNodeStoreScan<FAILURE>
{
    private final LabelScanStore labelScanStore;

    public LabelScanViewNodeStoreScan( StorageReader storageReader, LockService locks,
            LabelScanStore labelScanStore, Visitor<NodeLabelUpdate,FAILURE> labelUpdateVisitor,
            Visitor<EntityUpdates,FAILURE> propertyUpdatesVisitor, int[] labelIds,
            IntPredicate propertyKeyIdFilter )
    {
        super( storageReader, locks, labelUpdateVisitor, propertyUpdatesVisitor, labelIds,
                propertyKeyIdFilter );
        this.labelScanStore = labelScanStore;
    }

    @Override
    public EntityIdIterator getEntityIdIterator()
    {
        return new LabelScanViewIdIterator<>( labelScanStore.newReader(), labelIds, entityCursor );
    }
}
