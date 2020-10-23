/*
 * Copyright (c) 2002-2018 "Neo4j,"
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
package org.neo4j.consistency;

import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.time.ZoneId;
import java.util.List;

import org.neo4j.consistency.checking.full.ConsistencyCheckIncompleteException;
import org.neo4j.consistency.checking.full.ConsistencyFlags;
import org.neo4j.graphdb.factory.GraphDatabaseSettings;
import org.neo4j.helpers.Args;
import org.neo4j.helpers.progress.ProgressMonitorFactory;
import org.neo4j.io.fs.DefaultFileSystemAbstraction;
import org.neo4j.io.fs.FileSystemAbstraction;
import org.neo4j.io.layout.DatabaseLayout;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.configuration.LayoutConfig;
import org.neo4j.logging.FormattedLogProvider;
import org.neo4j.logging.LogProvider;

import static java.lang.String.format;
import static org.neo4j.helpers.Args.jarUsage;
import static org.neo4j.helpers.Strings.joinAsLines;
import static org.neo4j.kernel.recovery.Recovery.isRecoveryRequired;

public class ConsistencyCheckTool
{
    private static final String CONFIG = "config";
    private static final String VERBOSE = "v";

    public static void main( String[] args )
    {
        try
        {
            System.err.println("WARNING: ConsistencyCheckTool is deprecated and support for it will be" +
                    "removed in a future version of Neo4j. Please use neo4j-admin check-consistency.");
            runConsistencyCheckTool( args, System.out, System.err );
        }
        catch ( ToolFailureException e )
        {
            e.exitTool();
        }
    }

    public static ConsistencyCheckService.Result runConsistencyCheckTool( String[] args, PrintStream outStream,
                                                                          PrintStream errStream )
            throws ToolFailureException
    {
        FileSystemAbstraction fileSystem = new DefaultFileSystemAbstraction();
        try
        {
            ConsistencyCheckTool tool =
                    new ConsistencyCheckTool( new ConsistencyCheckService(), fileSystem, outStream, errStream );
            return tool.run( args );
        }
        finally
        {
            try
            {
                fileSystem.close();
            }
            catch ( IOException e )
            {
                System.err.print( "Failure during file system shutdown." );
            }
        }
    }

    private final ConsistencyCheckService consistencyCheckService;
    private final PrintStream systemOut;
    private final PrintStream systemError;
    private final FileSystemAbstraction fs;

    ConsistencyCheckTool( ConsistencyCheckService consistencyCheckService, FileSystemAbstraction fs,
            PrintStream systemOut, PrintStream systemError )
    {
        this.consistencyCheckService = consistencyCheckService;
        this.fs = fs;
        this.systemOut = systemOut;
        this.systemError = systemError;
    }

    ConsistencyCheckService.Result run( String... args ) throws ToolFailureException
    {
        Args arguments = Args.withFlags( VERBOSE ).parse( args );

        File storeDir = determineStoreDirectory( arguments );
        Config tuningConfiguration = readConfiguration( arguments, storeDir );
        boolean verbose = isVerbose( arguments );

        DatabaseLayout databaseLayout = DatabaseLayout.of( storeDir, LayoutConfig.of( tuningConfiguration ) );
        checkDbState( databaseLayout, tuningConfiguration );

        ZoneId logTimeZone = tuningConfiguration.get( GraphDatabaseSettings.db_timezone ).getZoneId();
        LogProvider logProvider = FormattedLogProvider.withZoneId( logTimeZone ).toOutputStream( systemOut );
        try
        {
            return consistencyCheckService.runFullConsistencyCheck( databaseLayout, tuningConfiguration,
                    ProgressMonitorFactory.textual( systemError ), logProvider, fs, verbose,
                    new ConsistencyFlags( tuningConfiguration ) );
        }
        catch ( ConsistencyCheckIncompleteException e )
        {
            throw new ToolFailureException( "Check aborted due to exception", e );
        }
    }

    private static boolean isVerbose( Args arguments )
    {
        return arguments.getBoolean( VERBOSE, false, true );
    }

    private void checkDbState( DatabaseLayout databaseLayout, Config additionalConfiguration ) throws ToolFailureException
    {
        if ( checkRecoveryState( databaseLayout, additionalConfiguration ) )
        {
            throw new ToolFailureException( joinAsLines( "Active logical log detected, this might be a source of inconsistencies.",
                    "Please recover database before running the consistency check.",
                    "To perform recovery please start database and perform clean shutdown." ) );
        }
    }

    private boolean checkRecoveryState( DatabaseLayout databaseLayout, Config additionalConfiguration )
    {
        try
        {
            return isRecoveryRequired( databaseLayout, additionalConfiguration );
        }
        catch ( Exception e )
        {
            systemError.printf( "Failure when checking for recovery state: '%s', continuing as normal.%n", e );
            return false;
        }
    }

    private File determineStoreDirectory( Args arguments ) throws ToolFailureException
    {
        List<String> unprefixedArguments = arguments.orphans();
        if ( unprefixedArguments.size() != 1 )
        {
            throw new ToolFailureException( usage() );
        }
        File storeDir = new File( unprefixedArguments.get( 0 ) );
        if ( !storeDir.isDirectory() )
        {
            throw new ToolFailureException( format( "'%s' is not a directory", storeDir ) + usage() );
        }
        return storeDir;
    }

    private static Config readConfiguration( Args arguments, File databasePath ) throws ToolFailureException
    {
        String configFilePath = arguments.get( CONFIG, null );
        if ( configFilePath != null )
        {
            File configFile = new File( configFilePath );
            try
            {
                Config config = Config.fromFile( configFile ).build();
                config.augment( GraphDatabaseSettings.database_path, databasePath.getAbsolutePath() );
                return config;
            }
            catch ( Exception e )
            {
                throw new ToolFailureException( format( "Could not read configuration file [%s]", configFilePath ), e );
            }
        }
        return Config.defaults();
    }

    private String usage()
    {
        return joinAsLines(
                jarUsage( getClass(), " [-config <neo4j.conf>] [-v] <storedir>" ),
                "WHERE:   -config <filename>  Is the location of an optional properties file",
                "                             containing tuning parameters for the consistency check.",
                "         -v                  Produce execution output.",
                "         <storedir>          Is the path to the store to check."
        );
    }

    public static class ToolFailureException extends Exception
    {
        ToolFailureException( String message )
        {
            super( message );
        }

        ToolFailureException( String message, Throwable cause )
        {
            super( message, cause );
        }

        public void exitTool()
        {
            printErrorMessage();
            exit();
        }

        public void printErrorMessage()
        {
            System.err.println( getMessage() );
            if ( getCause() != null )
            {
                getCause().printStackTrace( System.err );
            }
        }
    }

    private static void exit()
    {
        System.exit( 1 );
    }
}
