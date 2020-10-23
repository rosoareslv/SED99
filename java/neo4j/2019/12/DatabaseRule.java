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
package org.neo4j.test.rule;

import java.util.function.Function;
import java.util.function.LongFunction;

import org.neo4j.collection.Dependencies;
import org.neo4j.common.DependencyResolver;
import org.neo4j.common.TokenNameLookup;
import org.neo4j.configuration.Config;
import org.neo4j.dbms.database.DatabaseConfig;
import org.neo4j.exceptions.UnsatisfiedDependencyException;
import org.neo4j.function.Factory;
import org.neo4j.internal.helpers.collection.Iterables;
import org.neo4j.internal.id.BufferedIdController;
import org.neo4j.internal.id.BufferingIdGeneratorFactory;
import org.neo4j.internal.id.DefaultIdGeneratorFactory;
import org.neo4j.internal.id.IdController;
import org.neo4j.internal.id.IdGeneratorFactory;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.fs.watcher.DatabaseLayoutWatcher;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.io.pagecache.IOLimiter;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.io.pagecache.tracing.cursor.context.VersionContextSupplier;
import org.neo4j.kernel.api.procedure.GlobalProcedures;
import org.neo4j.kernel.availability.CompositeDatabaseAvailabilityGuard;
import org.neo4j.kernel.availability.DatabaseAvailabilityGuard;
import org.neo4j.kernel.database.Database;
import org.neo4j.kernel.database.DatabaseCreationContext;
import org.neo4j.kernel.database.DatabaseNameLogContext;
import org.neo4j.kernel.database.DatabaseStartupController;
import org.neo4j.kernel.database.NamedDatabaseId;
import org.neo4j.kernel.database.TestDatabaseIdRepository;
import org.neo4j.kernel.diagnostics.providers.DbmsDiagnosticsManager;
import org.neo4j.kernel.extension.ExtensionFactory;
import org.neo4j.kernel.extension.ExtensionType;
import org.neo4j.kernel.extension.context.ExtensionContext;
import org.neo4j.kernel.impl.api.CommitProcessFactory;
import org.neo4j.kernel.impl.api.LeaseService;
import org.neo4j.kernel.impl.constraints.ConstraintSemantics;
import org.neo4j.kernel.impl.constraints.StandardConstraintSemantics;
import org.neo4j.kernel.impl.context.TransactionVersionContextSupplier;
import org.neo4j.kernel.impl.factory.AccessCapabilityFactory;
import org.neo4j.kernel.impl.factory.CanWrite;
import org.neo4j.kernel.impl.factory.CommunityCommitProcessFactory;
import org.neo4j.kernel.impl.factory.DatabaseInfo;
import org.neo4j.kernel.impl.locking.Locks;
import org.neo4j.kernel.impl.locking.StatementLocks;
import org.neo4j.kernel.impl.locking.StatementLocksFactory;
import org.neo4j.kernel.impl.query.QueryEngineProvider;
import org.neo4j.kernel.impl.transaction.log.checkpoint.StoreCopyCheckPointMutex;
import org.neo4j.kernel.impl.transaction.stats.DatabaseTransactionStats;
import org.neo4j.kernel.impl.util.collection.CollectionsFactorySupplier;
import org.neo4j.kernel.internal.event.GlobalTransactionEventListeners;
import org.neo4j.kernel.internal.locker.FileLockerService;
import org.neo4j.kernel.internal.locker.GlobalLockerService;
import org.neo4j.kernel.lifecycle.Lifecycle;
import org.neo4j.kernel.monitoring.tracing.Tracers;
import org.neo4j.kernel.recovery.RecoveryExtension;
import org.neo4j.logging.NullLog;
import org.neo4j.logging.NullLogProvider;
import org.neo4j.logging.internal.DatabaseLogService;
import org.neo4j.logging.internal.LogService;
import org.neo4j.logging.internal.SimpleLogService;
import org.neo4j.monitoring.DatabaseEventListeners;
import org.neo4j.monitoring.DatabaseHealth;
import org.neo4j.monitoring.DatabasePanicEventGenerator;
import org.neo4j.monitoring.Monitors;
import org.neo4j.scheduler.JobScheduler;
import org.neo4j.storageengine.api.StorageEngineFactory;
import org.neo4j.time.Clocks;
import org.neo4j.time.SystemNanoClock;
import org.neo4j.token.TokenHolders;
import org.neo4j.token.api.TokenHolder;

import static org.mockito.Mockito.RETURNS_MOCKS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.neo4j.configuration.GraphDatabaseSettings.DEFAULT_DATABASE_NAME;
import static org.neo4j.configuration.GraphDatabaseSettings.default_schema_provider;
import static org.neo4j.index.internal.gbptree.RecoveryCleanupWorkCollector.immediate;
import static org.neo4j.kernel.api.index.IndexProvider.EMPTY;
import static org.neo4j.kernel.database.DatabaseStartupController.NEVER_ABORT;
import static org.neo4j.kernel.impl.util.collection.CollectionsFactorySupplier.ON_HEAP;

public class DatabaseRule extends ExternalResource
{
    private Database database;
    private final TestDatabaseIdRepository databaseIdRepository = new TestDatabaseIdRepository();

    public Database getDatabase( DatabaseLayout databaseLayout, FileSystemAbstraction fs, PageCache pageCache )
    {
        return getDatabase( databaseLayout, fs, pageCache, new Dependencies() );
    }

    public Database getDatabase( String databaseName, DatabaseLayout databaseLayout, FileSystemAbstraction fs, PageCache pageCache )
    {
        return getDatabase( databaseName, databaseLayout, fs, pageCache, new Dependencies() );
    }

    public Database getDatabase( DatabaseLayout databaseLayout, FileSystemAbstraction fs, PageCache pageCache,
            DependencyResolver otherCustomOverriddenDependencies )
    {
        return getDatabase( DEFAULT_DATABASE_NAME, databaseLayout, fs, pageCache, otherCustomOverriddenDependencies );
    }

    public Database getDatabase( String databaseName, DatabaseLayout databaseLayout, FileSystemAbstraction fs,
            PageCache pageCache, DependencyResolver otherCustomOverriddenDependencies )
    {
        shutdownAnyRunning();

        StatementLocksFactory locksFactory = mock( StatementLocksFactory.class );
        StatementLocks statementLocks = mock( StatementLocks.class );
        Locks.Client locks = mock( Locks.Client.class );
        when( statementLocks.optimistic() ).thenReturn( locks );
        when( statementLocks.pessimistic() ).thenReturn( locks );
        when( locksFactory.newInstance() ).thenReturn( statementLocks );

        JobScheduler jobScheduler = mock( JobScheduler.class, RETURNS_MOCKS );
        Monitors monitors = new Monitors();

        Dependencies mutableDependencies = new Dependencies( otherCustomOverriddenDependencies );

        // Satisfy non-satisfied dependencies
        mutableDependencies.satisfyDependency( mock( CompositeDatabaseAvailabilityGuard.class ) );
        Config config = dependency( mutableDependencies, Config.class, deps -> Config.defaults() );
        config.set( default_schema_provider, EMPTY.getProviderDescriptor().name() );
        LogService logService = dependency( mutableDependencies, LogService.class,
                deps -> new SimpleLogService( NullLogProvider.getInstance() ) );
        IdGeneratorFactory idGeneratorFactory = dependency( mutableDependencies, IdGeneratorFactory.class,
                deps -> new DefaultIdGeneratorFactory( fs, immediate() ) );
        DatabaseHealth databaseHealth = dependency( mutableDependencies, DatabaseHealth.class,
                deps -> new DatabaseHealth( mock( DatabasePanicEventGenerator.class ), NullLog.getInstance() ) );
        SystemNanoClock clock = dependency( mutableDependencies, SystemNanoClock.class, deps -> Clocks.nanoClock() );
        DatabaseTransactionStats transactionStats = dependency( mutableDependencies, DatabaseTransactionStats.class,
                deps -> new DatabaseTransactionStats() );
        dependency( mutableDependencies, DbmsDiagnosticsManager.class, deps -> mock( DbmsDiagnosticsManager.class ) );
        StorageEngineFactory storageEngineFactory = dependency( mutableDependencies, StorageEngineFactory.class,
                deps -> StorageEngineFactory.selectStorageEngine() );
        NamedDatabaseId namedDatabaseId = databaseIdRepository.getRaw( databaseName );

        database = new Database( new TestDatabaseCreationContext( namedDatabaseId, databaseLayout, config, idGeneratorFactory, logService,
                mock( JobScheduler.class, RETURNS_MOCKS ), mock( TokenNameLookup.class ), mutableDependencies, mockedTokenHolders(), locksFactory,
                mock( GlobalTransactionEventListeners.class ), fs, transactionStats, databaseHealth,
                new CommunityCommitProcessFactory(),
                pageCache, new StandardConstraintSemantics(), monitors,
                new Tracers( "null", NullLog.getInstance(), monitors, jobScheduler, clock ),
                mock( GlobalProcedures.class ), IOLimiter.UNLIMITED, clock, new StoreCopyCheckPointMutex(),
                new BufferedIdController( new BufferingIdGeneratorFactory( idGeneratorFactory ),
                        jobScheduler ), DatabaseInfo.COMMUNITY, new TransactionVersionContextSupplier(), ON_HEAP,
                Iterables.iterable( new EmptyIndexExtensionFactory() ),
                file -> mock( DatabaseLayoutWatcher.class ), null,
                storageEngineFactory, new GlobalLockerService(), LeaseService.NO_LEASES, NEVER_ABORT ) );
        return database;
    }

    private static <T> T dependency( Dependencies dependencies, Class<T> type, Function<DependencyResolver,T> defaultSupplier )
    {
        try
        {
            return dependencies.resolveDependency( type );
        }
        catch ( IllegalArgumentException | UnsatisfiedDependencyException e )
        {
            return dependencies.satisfyDependency( defaultSupplier.apply( dependencies ) );
        }
    }

    private void shutdownAnyRunning()
    {
        if ( database != null )
        {
            database.stop();
        }
    }

    @Override
    protected void after( boolean successful )
    {
        shutdownAnyRunning();
    }

    private static class TestDatabaseCreationContext implements DatabaseCreationContext
    {
        private final NamedDatabaseId namedDatabaseId;
        private final DatabaseLayout databaseLayout;
        private final Config config;
        private final LeaseService leaseService;
        private final DatabaseStartupController startupController;
        private final DatabaseConfig databaseConfig;
        private final IdGeneratorFactory idGeneratorFactory;
        private final DatabaseLogService logService;
        private final JobScheduler scheduler;
        private final TokenNameLookup tokenNameLookup;
        private final DependencyResolver dependencyResolver;
        private final TokenHolders tokenHolders;
        private final StatementLocksFactory statementLocksFactory;
        private final GlobalTransactionEventListeners globalTransactionEventListeners;
        private final FileSystemAbstraction fs;
        private final DatabaseTransactionStats databaseTransactionStats;
        private final DatabaseHealth databaseHealth;
        private final CommitProcessFactory commitProcessFactory;
        private final PageCache pageCache;
        private final ConstraintSemantics constraintSemantics;
        private final Monitors monitors;
        private final Tracers tracers;
        private final GlobalProcedures globalProcedures;
        private final IOLimiter ioLimiter;
        private final SystemNanoClock clock;
        private final StoreCopyCheckPointMutex storeCopyCheckPointMutex;
        private final IdController idController;
        private final DatabaseInfo databaseInfo;
        private final VersionContextSupplier versionContextSupplier;
        private final CollectionsFactorySupplier collectionsFactorySupplier;
        private final Iterable<ExtensionFactory<?>> extensionFactories;
        private final Function<DatabaseLayout,DatabaseLayoutWatcher> watcherServiceFactory;
        private final QueryEngineProvider engineProvider;
        private final DatabaseEventListeners eventListeners;
        private final StorageEngineFactory storageEngineFactory;
        private final FileLockerService fileLockerService;

        TestDatabaseCreationContext( NamedDatabaseId namedDatabaseId, DatabaseLayout databaseLayout, Config config, IdGeneratorFactory idGeneratorFactory,
                LogService logService, JobScheduler scheduler, TokenNameLookup tokenNameLookup, DependencyResolver dependencyResolver,
                TokenHolders tokenHolders, StatementLocksFactory statementLocksFactory, GlobalTransactionEventListeners globalTransactionEventListeners,
                FileSystemAbstraction fs, DatabaseTransactionStats databaseTransactionStats, DatabaseHealth databaseHealth,
                CommitProcessFactory commitProcessFactory, PageCache pageCache, ConstraintSemantics constraintSemantics, Monitors monitors, Tracers tracers,
                GlobalProcedures globalProcedures, IOLimiter ioLimiter, SystemNanoClock clock,
                StoreCopyCheckPointMutex storeCopyCheckPointMutex, IdController idController,
                DatabaseInfo databaseInfo, VersionContextSupplier versionContextSupplier, CollectionsFactorySupplier collectionsFactorySupplier,
                Iterable<ExtensionFactory<?>> extensionFactories, Function<DatabaseLayout,DatabaseLayoutWatcher> watcherServiceFactory,
                QueryEngineProvider engineProvider, StorageEngineFactory storageEngineFactory,
                FileLockerService fileLockerService, LeaseService leaseService, DatabaseStartupController startupController )
        {
            this.namedDatabaseId = namedDatabaseId;
            this.databaseLayout = databaseLayout;
            this.config = config;
            this.leaseService = leaseService;
            this.startupController = startupController;
            this.databaseConfig = new DatabaseConfig( config, namedDatabaseId );
            this.idGeneratorFactory = idGeneratorFactory;
            this.logService = new DatabaseLogService( new DatabaseNameLogContext( namedDatabaseId ), logService );
            this.scheduler = scheduler;
            this.tokenNameLookup = tokenNameLookup;
            this.dependencyResolver = dependencyResolver;
            this.tokenHolders = tokenHolders;
            this.statementLocksFactory = statementLocksFactory;
            this.globalTransactionEventListeners = globalTransactionEventListeners;
            this.fs = fs;
            this.databaseTransactionStats = databaseTransactionStats;
            this.databaseHealth = databaseHealth;
            this.commitProcessFactory = commitProcessFactory;
            this.pageCache = pageCache;
            this.constraintSemantics = constraintSemantics;
            this.monitors = monitors;
            this.tracers = tracers;
            this.globalProcedures = globalProcedures;
            this.ioLimiter = ioLimiter;
            this.clock = clock;
            this.storeCopyCheckPointMutex = storeCopyCheckPointMutex;
            this.idController = idController;
            this.databaseInfo = databaseInfo;
            this.versionContextSupplier = versionContextSupplier;
            this.collectionsFactorySupplier = collectionsFactorySupplier;
            this.extensionFactories = extensionFactories;
            this.watcherServiceFactory = watcherServiceFactory;
            this.engineProvider = engineProvider;
            this.eventListeners = mock( DatabaseEventListeners.class );
            this.storageEngineFactory = storageEngineFactory;
            this.fileLockerService = fileLockerService;
        }

        @Override
        public NamedDatabaseId getNamedDatabaseId()
        {
            return namedDatabaseId;
        }

        @Override
        public DatabaseLayout getDatabaseLayout()
        {
            return databaseLayout;
        }

        @Override
        public Config getGlobalConfig()
        {
            return config;
        }

        @Override
        public DatabaseConfig getDatabaseConfig()
        {
            return databaseConfig;
        }

        @Override
        public IdGeneratorFactory getIdGeneratorFactory()
        {
            return idGeneratorFactory;
        }

        @Override
        public DatabaseLogService getDatabaseLogService()
        {
            return logService;
        }

        @Override
        public JobScheduler getScheduler()
        {
            return scheduler;
        }

        @Override
        public TokenNameLookup getTokenNameLookup()
        {
            return tokenNameLookup;
        }

        @Override
        public DependencyResolver getGlobalDependencies()
        {
            return dependencyResolver;
        }

        public DependencyResolver getDependencyResolver()
        {
            return dependencyResolver;
        }

        @Override
        public TokenHolders getTokenHolders()
        {
            return tokenHolders;
        }

        @Override
        public Locks getLocks()
        {
            return mock( Locks.class );
        }

        @Override
        public StatementLocksFactory getStatementLocksFactory()
        {
            return statementLocksFactory;
        }

        @Override
        public GlobalTransactionEventListeners getTransactionEventListeners()
        {
            return globalTransactionEventListeners;
        }

        @Override
        public FileSystemAbstraction getFs()
        {
            return fs;
        }

        @Override
        public DatabaseTransactionStats getTransactionStats()
        {
            return databaseTransactionStats;
        }

        @Override
        public Factory<DatabaseHealth> getDatabaseHealthFactory()
        {
            return () -> databaseHealth;
        }

        @Override
        public CommitProcessFactory getCommitProcessFactory()
        {
            return commitProcessFactory;
        }

        @Override
        public PageCache getPageCache()
        {
            return pageCache;
        }

        @Override
        public ConstraintSemantics getConstraintSemantics()
        {
            return constraintSemantics;
        }

        @Override
        public Monitors getMonitors()
        {
            return monitors;
        }

        @Override
        public Tracers getTracers()
        {
            return tracers;
        }

        @Override
        public GlobalProcedures getGlobalProcedures()
        {
            return globalProcedures;
        }

        @Override
        public IOLimiter getIoLimiter()
        {
            return ioLimiter;
        }

        @Override
        public LongFunction<DatabaseAvailabilityGuard> getDatabaseAvailabilityGuardFactory()
        {
            return timeout -> new DatabaseAvailabilityGuard( namedDatabaseId,
                    clock,
                    NullLog.getInstance(),
                    timeout,
                    mock( CompositeDatabaseAvailabilityGuard.class ) );
        }

        @Override
        public SystemNanoClock getClock()
        {
            return clock;
        }

        @Override
        public StoreCopyCheckPointMutex getStoreCopyCheckPointMutex()
        {
            return storeCopyCheckPointMutex;
        }

        @Override
        public IdController getIdController()
        {
            return idController;
        }

        @Override
        public DatabaseInfo getDatabaseInfo()
        {
            return databaseInfo;
        }

        @Override
        public VersionContextSupplier getVersionContextSupplier()
        {
            return versionContextSupplier;
        }

        @Override
        public CollectionsFactorySupplier getCollectionsFactorySupplier()
        {
            return collectionsFactorySupplier;
        }

        @Override
        public Iterable<ExtensionFactory<?>> getExtensionFactories()
        {
            return extensionFactories;
        }

        @Override
        public Function<DatabaseLayout,DatabaseLayoutWatcher> getWatcherServiceFactory()
        {
            return watcherServiceFactory;
        }

        @Override
        public QueryEngineProvider getEngineProvider()
        {
            return engineProvider;
        }

        @Override
        public DatabaseEventListeners getDatabaseEventListeners()
        {
            return eventListeners;
        }

        @Override
        public StorageEngineFactory getStorageEngineFactory()
        {
            return storageEngineFactory;
        }

        @Override
        public FileLockerService getFileLockerService()
        {
            return fileLockerService;
        }

        @Override
        public AccessCapabilityFactory getAccessCapabilityFactory()
        {
            return AccessCapabilityFactory.fixed( new CanWrite() );
        }

        @Override
        public LeaseService getLeaseService()
        {
            return leaseService;
        }

        @Override
        public DatabaseStartupController getStartupController()
        {
            return startupController;
        }
    }

    public static TokenHolders mockedTokenHolders()
    {
        return new TokenHolders(
                mock( TokenHolder.class ),
                mock( TokenHolder.class ),
                mock( TokenHolder.class ) );
    }

    @RecoveryExtension
    private static class EmptyIndexExtensionFactory extends ExtensionFactory<EmptyIndexExtensionFactory.Dependencies>
    {
        interface Dependencies
        {
        }

        EmptyIndexExtensionFactory()
        {
            super( ExtensionType.DATABASE, "customExtension" );
        }

        @Override
        public Lifecycle newInstance( ExtensionContext context, Dependencies dependencies )
        {
            return EMPTY;
        }
    }
}
