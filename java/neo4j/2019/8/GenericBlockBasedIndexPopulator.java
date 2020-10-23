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
package org.neo4j.kernel.impl.index.schema;

import java.util.HashMap;
import java.util.Map;

import org.neo4j.gis.spatial.index.curves.SpaceFillingCurveConfiguration;
import org.neo4j.internal.schema.IndexDescriptor;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.kernel.api.index.IndexProvider;
import org.neo4j.kernel.impl.index.schema.config.IndexSpecificSpaceFillingCurveSettings;
import org.neo4j.values.storable.Value;

class GenericBlockBasedIndexPopulator extends BlockBasedIndexPopulator<GenericKey,NativeIndexValue>
{
    private final IndexSpecificSpaceFillingCurveSettings spatialSettings;
    private final SpaceFillingCurveConfiguration configuration;

    GenericBlockBasedIndexPopulator( PageCache pageCache, FileSystemAbstraction fs, IndexFiles indexFiles, IndexLayout<GenericKey,NativeIndexValue> layout,
            IndexProvider.Monitor monitor, IndexDescriptor descriptor, IndexSpecificSpaceFillingCurveSettings spatialSettings,
            SpaceFillingCurveConfiguration configuration, boolean archiveFailedIndex, ByteBufferFactory bufferFactory )
    {
        super( pageCache, fs, indexFiles, layout, monitor, descriptor, spatialSettings, archiveFailedIndex, bufferFactory );
        this.spatialSettings = spatialSettings;
        this.configuration = configuration;
    }

    @Override
    NativeIndexReader<GenericKey,NativeIndexValue> newReader()
    {
        return new GenericNativeIndexReader( tree, layout, descriptor, spatialSettings, configuration );
    }

    @Override
    public Map<String,Value> indexConfig()
    {
        Map<String,Value> map = new HashMap<>();
        spatialSettings.visitIndexSpecificSettings( new SpatialConfigVisitor( map ) );
        return map;
    }
}
