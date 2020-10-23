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
package org.neo4j.causalclustering.core.state.machines.id;

import java.io.File;
import java.util.HashMap;
import java.util.Map;
import java.util.function.LongSupplier;

import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.kernel.impl.store.id.IdGenerator;
import org.neo4j.kernel.impl.store.id.IdGeneratorFactory;
import org.neo4j.kernel.impl.store.id.IdType;
import org.neo4j.kernel.impl.store.id.configuration.IdTypeConfiguration;
import org.neo4j.kernel.impl.store.id.configuration.IdTypeConfigurationProvider;
import org.neo4j.logging.LogProvider;

public class ReplicatedIdGeneratorFactory implements IdGeneratorFactory
{
    private final Map<IdType, ReplicatedIdGenerator> generators = new HashMap<>();
    private final FileSystemAbstraction fs;
    private final ReplicatedIdRangeAcquirer idRangeAcquirer;
    private final LogProvider logProvider;
    private IdTypeConfigurationProvider idTypeConfigurationProvider;

    public ReplicatedIdGeneratorFactory( FileSystemAbstraction fs, ReplicatedIdRangeAcquirer idRangeAcquirer,
            LogProvider logProvider, IdTypeConfigurationProvider idTypeConfigurationProvider )
    {
        this.fs = fs;
        this.idRangeAcquirer = idRangeAcquirer;
        this.logProvider = logProvider;
        this.idTypeConfigurationProvider = idTypeConfigurationProvider;
    }

    @Override
    public IdGenerator open( File file, IdType idType, LongSupplier highId, long maxId )
    {
        IdTypeConfiguration idTypeConfiguration = idTypeConfigurationProvider.getIdTypeConfiguration( idType );
        return openGenerator( file, idTypeConfiguration.getGrabSize(), idType, highId, maxId, idTypeConfiguration.allowAggressiveReuse() );
    }

    @Override
    public IdGenerator open( File fileName, int grabSize, IdType idType, LongSupplier highId, long maxId )
    {
        IdTypeConfiguration idTypeConfiguration = idTypeConfigurationProvider.getIdTypeConfiguration( idType );
        return openGenerator( fileName, grabSize, idType, highId, maxId, idTypeConfiguration.allowAggressiveReuse() );
    }

    private IdGenerator openGenerator( File file, int grabSize, IdType idType, LongSupplier highId, long maxId, boolean aggressiveReuse )
    {
        ReplicatedIdGenerator other = generators.get( idType );
        if ( other != null )
        {
            other.close();
        }
        ReplicatedIdGenerator replicatedIdGenerator =
                new ReplicatedIdGenerator( fs, file, idType, highId, idRangeAcquirer, logProvider, grabSize, aggressiveReuse );

        generators.put( idType, replicatedIdGenerator);
        return replicatedIdGenerator;
    }

    @Override
    public IdGenerator get( IdType idType )
    {
        return generators.get( idType );
    }

    @Override
    public void create( File fileName, long highId, boolean throwIfFileExists )
    {
        ReplicatedIdGenerator.createGenerator( fs, fileName, highId, throwIfFileExists );
    }
}
