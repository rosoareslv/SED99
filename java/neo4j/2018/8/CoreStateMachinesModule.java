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
package org.neo4j.causalclustering.core.state.machines;

import java.io.File;
import java.time.Clock;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.function.Supplier;

import org.neo4j.causalclustering.catchup.storecopy.LocalDatabase;
import org.neo4j.causalclustering.core.consensus.LeaderLocator;
import org.neo4j.causalclustering.core.consensus.RaftMachine;
import org.neo4j.causalclustering.core.replication.RaftReplicator;
import org.neo4j.causalclustering.core.replication.Replicator;
import org.neo4j.causalclustering.core.state.machines.dummy.DummyMachine;
import org.neo4j.causalclustering.core.state.machines.id.CommandIndexTracker;
import org.neo4j.causalclustering.core.state.machines.id.IdAllocationState;
import org.neo4j.causalclustering.core.state.machines.id.IdReusabilityCondition;
import org.neo4j.causalclustering.core.state.machines.id.ReplicatedIdAllocationStateMachine;
import org.neo4j.causalclustering.core.state.machines.id.ReplicatedIdGeneratorFactory;
import org.neo4j.causalclustering.core.state.machines.id.ReplicatedIdRangeAcquirer;
import org.neo4j.causalclustering.core.state.machines.locks.LeaderOnlyLockManager;
import org.neo4j.causalclustering.core.state.machines.locks.ReplicatedLockTokenState;
import org.neo4j.causalclustering.core.state.machines.locks.ReplicatedLockTokenStateMachine;
import org.neo4j.causalclustering.core.state.machines.token.ReplicatedLabelTokenHolder;
import org.neo4j.causalclustering.core.state.machines.token.ReplicatedPropertyKeyTokenHolder;
import org.neo4j.causalclustering.core.state.machines.token.ReplicatedRelationshipTypeTokenHolder;
import org.neo4j.causalclustering.core.state.machines.token.ReplicatedTokenStateMachine;
import org.neo4j.causalclustering.core.state.machines.tx.RecoverConsensusLogIndex;
import org.neo4j.causalclustering.core.state.machines.tx.ReplicatedTransactionCommitProcess;
import org.neo4j.causalclustering.core.state.machines.tx.ReplicatedTransactionStateMachine;
import org.neo4j.causalclustering.core.state.storage.DurableStateStorage;
import org.neo4j.causalclustering.core.state.storage.StateStorage;
import org.neo4j.causalclustering.identity.MemberId;
import org.neo4j.graphdb.factory.EditionLocksFactories;
import org.neo4j.graphdb.factory.module.PlatformModule;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.pagecache.tracing.cursor.PageCursorTracerSupplier;
import org.neo4j.io.pagecache.tracing.cursor.context.VersionContextSupplier;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.impl.api.CommitProcessFactory;
import org.neo4j.kernel.impl.core.TokenHolder;
import org.neo4j.kernel.impl.core.TokenHolders;
import org.neo4j.kernel.impl.core.TokenRegistry;
import org.neo4j.kernel.impl.enterprise.id.EnterpriseIdTypeConfigurationProvider;
import org.neo4j.kernel.impl.locking.Locks;
import org.neo4j.kernel.impl.locking.LocksFactory;
import org.neo4j.kernel.impl.logging.LogService;
import org.neo4j.kernel.impl.store.id.IdGeneratorFactory;
import org.neo4j.kernel.impl.store.id.IdType;
import org.neo4j.kernel.impl.store.id.configuration.IdTypeConfigurationProvider;
import org.neo4j.kernel.impl.util.Dependencies;
import org.neo4j.kernel.lifecycle.LifeSupport;
import org.neo4j.logging.LogProvider;
import org.neo4j.storageengine.api.StorageEngine;

import static org.neo4j.causalclustering.core.CausalClusteringSettings.array_block_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.id_alloc_state_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.label_token_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.label_token_name_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.neostore_block_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.node_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.node_labels_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.property_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.property_key_token_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.property_key_token_name_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.relationship_group_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.relationship_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.relationship_type_token_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.relationship_type_token_name_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.replicated_lock_token_state_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.schema_id_allocation_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.state_machine_apply_max_batch_size;
import static org.neo4j.causalclustering.core.CausalClusteringSettings.string_block_id_allocation_size;
import static org.neo4j.graphdb.factory.EditionLocksFactories.createLockFactory;

public class CoreStateMachinesModule
{
    public static final String ID_ALLOCATION_NAME = "id-allocation";
    public static final String LOCK_TOKEN_NAME = "lock-token";

    public final IdGeneratorFactory idGeneratorFactory;
    public final IdTypeConfigurationProvider idTypeConfigurationProvider;
    public final TokenHolders tokenHolders;
    public final Supplier<Locks> locksSupplier;
    public final CommitProcessFactory commitProcessFactory;

    public final CoreStateMachines coreStateMachines;
    public final BooleanSupplier freeIdCondition;

    public CoreStateMachinesModule( MemberId myself, PlatformModule platformModule, File clusterStateDirectory,
            Config config, RaftReplicator replicator, RaftMachine raftMachine, Dependencies dependencies,
            LocalDatabase localDatabase )
    {
        StateStorage<IdAllocationState> idAllocationState;
        StateStorage<ReplicatedLockTokenState> lockTokenState;
        final LifeSupport life = platformModule.life;
        final FileSystemAbstraction fileSystem = platformModule.fileSystem;
        LogService logging = platformModule.logging;
        LogProvider logProvider = logging.getInternalLogProvider();

        lockTokenState = life.add(
                new DurableStateStorage<>( fileSystem, clusterStateDirectory, LOCK_TOKEN_NAME,
                        new ReplicatedLockTokenState.Marshal( new MemberId.Marshal() ),
                        config.get( replicated_lock_token_state_size ), logProvider ) );

        idAllocationState = life.add(
                new DurableStateStorage<>( fileSystem, clusterStateDirectory, ID_ALLOCATION_NAME,
                        new IdAllocationState.Marshal(),
                        config.get( id_alloc_state_size ), logProvider ) );

        ReplicatedIdAllocationStateMachine idAllocationStateMachine =
                new ReplicatedIdAllocationStateMachine( idAllocationState );

        Map<IdType,Integer> allocationSizes = getIdTypeAllocationSizeFromConfig( config );

        ReplicatedIdRangeAcquirer idRangeAcquirer =
                new ReplicatedIdRangeAcquirer( replicator, idAllocationStateMachine, allocationSizes, myself,
                        logProvider );

        idTypeConfigurationProvider = new EnterpriseIdTypeConfigurationProvider( config );
        CommandIndexTracker commandIndexTracker = new CommandIndexTracker();
        freeIdCondition = new IdReusabilityCondition( commandIndexTracker, raftMachine, myself );
        this.idGeneratorFactory =
                createIdGeneratorFactory( fileSystem, idRangeAcquirer, logProvider, idTypeConfigurationProvider );

        TokenRegistry relationshipTypeTokenRegistry = new TokenRegistry( TokenHolder.TYPE_RELATIONSHIP_TYPE );
        Supplier<StorageEngine> storageEngineSupplier = () -> localDatabase.dataSource().getDependencyResolver().resolveDependency( StorageEngine.class );
        ReplicatedRelationshipTypeTokenHolder relationshipTypeTokenHolder =
                new ReplicatedRelationshipTypeTokenHolder( relationshipTypeTokenRegistry, replicator,
                        this.idGeneratorFactory, storageEngineSupplier );

        TokenRegistry propertyKeyTokenRegistry = new TokenRegistry( TokenHolder.TYPE_PROPERTY_KEY );
        ReplicatedPropertyKeyTokenHolder propertyKeyTokenHolder =
                new ReplicatedPropertyKeyTokenHolder( propertyKeyTokenRegistry, replicator, this.idGeneratorFactory,
                        storageEngineSupplier );

        TokenRegistry labelTokenRegistry = new TokenRegistry( TokenHolder.TYPE_LABEL );
        ReplicatedLabelTokenHolder labelTokenHolder =
                new ReplicatedLabelTokenHolder( labelTokenRegistry, replicator, this.idGeneratorFactory,
                        storageEngineSupplier );

        ReplicatedLockTokenStateMachine replicatedLockTokenStateMachine =
                new ReplicatedLockTokenStateMachine( lockTokenState );

        VersionContextSupplier versionContextSupplier = platformModule.versionContextSupplier;
        ReplicatedTokenStateMachine labelTokenStateMachine =
                new ReplicatedTokenStateMachine( labelTokenRegistry, logProvider, versionContextSupplier );

        ReplicatedTokenStateMachine propertyKeyTokenStateMachine =
                new ReplicatedTokenStateMachine( propertyKeyTokenRegistry, logProvider, versionContextSupplier );

        ReplicatedTokenStateMachine relationshipTypeTokenStateMachine =
                new ReplicatedTokenStateMachine( relationshipTypeTokenRegistry, logProvider, versionContextSupplier );

        PageCursorTracerSupplier cursorTracerSupplier = platformModule.tracers.pageCursorTracerSupplier;
        ReplicatedTransactionStateMachine replicatedTxStateMachine =
                new ReplicatedTransactionStateMachine( commandIndexTracker, replicatedLockTokenStateMachine,
                        config.get( state_machine_apply_max_batch_size ), logProvider, cursorTracerSupplier,
                        versionContextSupplier );

        dependencies.satisfyDependencies( replicatedTxStateMachine );

        LocksFactory lockFactory = createLockFactory( config, logging );
        locksSupplier = () -> createLockManager( lockFactory, config, platformModule.clock, replicator, myself, raftMachine,
                replicatedLockTokenStateMachine );

        RecoverConsensusLogIndex consensusLogIndexRecovery = new RecoverConsensusLogIndex( localDatabase, logProvider );

        coreStateMachines = new CoreStateMachines( replicatedTxStateMachine, labelTokenStateMachine,
                relationshipTypeTokenStateMachine, propertyKeyTokenStateMachine, replicatedLockTokenStateMachine,
                idAllocationStateMachine, new DummyMachine(), localDatabase, consensusLogIndexRecovery );

        commitProcessFactory = ( appender, applier, ignored ) ->
        {
            localDatabase.registerCommitProcessDependencies( appender, applier );
            return new ReplicatedTransactionCommitProcess( replicator );
        };

        this.tokenHolders = new TokenHolders( propertyKeyTokenHolder, labelTokenHolder, relationshipTypeTokenHolder );
        dependencies.satisfyDependencies( tokenHolders );
    }

    private Map<IdType,Integer> getIdTypeAllocationSizeFromConfig( Config config )
    {
        Map<IdType,Integer> allocationSizes = new HashMap<>( IdType.values().length );
        allocationSizes.put( IdType.NODE, config.get( node_id_allocation_size ) );
        allocationSizes.put( IdType.RELATIONSHIP, config.get( relationship_id_allocation_size ) );
        allocationSizes.put( IdType.PROPERTY, config.get( property_id_allocation_size ) );
        allocationSizes.put( IdType.STRING_BLOCK, config.get( string_block_id_allocation_size ) );
        allocationSizes.put( IdType.ARRAY_BLOCK, config.get( array_block_id_allocation_size ) );
        allocationSizes.put( IdType.PROPERTY_KEY_TOKEN, config.get( property_key_token_id_allocation_size ) );
        allocationSizes.put( IdType.PROPERTY_KEY_TOKEN_NAME, config.get( property_key_token_name_id_allocation_size ) );
        allocationSizes.put( IdType.RELATIONSHIP_TYPE_TOKEN, config.get( relationship_type_token_id_allocation_size ) );
        allocationSizes.put( IdType.RELATIONSHIP_TYPE_TOKEN_NAME, config.get( relationship_type_token_name_id_allocation_size ) );
        allocationSizes.put( IdType.LABEL_TOKEN, config.get( label_token_id_allocation_size ) );
        allocationSizes.put( IdType.LABEL_TOKEN_NAME, config.get( label_token_name_id_allocation_size ) );
        allocationSizes.put( IdType.NEOSTORE_BLOCK, config.get( neostore_block_id_allocation_size ) );
        allocationSizes.put( IdType.SCHEMA, config.get( schema_id_allocation_size ) );
        allocationSizes.put( IdType.NODE_LABELS, config.get( node_labels_id_allocation_size ) );
        allocationSizes.put( IdType.RELATIONSHIP_GROUP, config.get( relationship_group_id_allocation_size ) );
        return allocationSizes;
    }

    private IdGeneratorFactory createIdGeneratorFactory( FileSystemAbstraction fileSystem,
            final ReplicatedIdRangeAcquirer idRangeAcquirer, final LogProvider logProvider,
            IdTypeConfigurationProvider idTypeConfigurationProvider )
    {
        return new ReplicatedIdGeneratorFactory( fileSystem, idRangeAcquirer,
                logProvider, idTypeConfigurationProvider );
    }

    private Locks createLockManager( LocksFactory locksFactory, Config config, Clock clock,
                                     Replicator replicator, MemberId myself, LeaderLocator leaderLocator,
                                     ReplicatedLockTokenStateMachine lockTokenStateMachine )
    {
        Locks localLocks = EditionLocksFactories.createLockManager( locksFactory, config, clock );
        return new LeaderOnlyLockManager( myself, replicator, leaderLocator, localLocks, lockTokenStateMachine );
    }
}
