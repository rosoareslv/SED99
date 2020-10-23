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
package org.neo4j.kernel.impl.transaction.log.entry;

import java.io.IOException;

import org.neo4j.io.fs.PositionableChannel;
import org.neo4j.io.fs.ReadPastEndException;
import org.neo4j.kernel.impl.transaction.log.LogPosition;
import org.neo4j.kernel.impl.transaction.log.LogPositionMarker;
import org.neo4j.kernel.impl.transaction.log.ReadableClosablePositionAwareChecksumChannel;
import org.neo4j.kernel.impl.transaction.log.ServiceLoadingCommandReaderFactory;
import org.neo4j.storageengine.api.CommandReaderFactory;
import org.neo4j.util.FeatureToggles;

import static org.neo4j.internal.helpers.Exceptions.throwIfInstanceOf;
import static org.neo4j.internal.helpers.Exceptions.withMessage;
import static org.neo4j.kernel.impl.transaction.log.entry.LogEntryVersion.byVersion;
import static org.neo4j.storageengine.api.TransactionIdStore.BASE_TX_CHECKSUM;

/**
 * Version aware implementation of LogEntryReader
 * Starting with Neo4j version 2.1, log entries are prefixed with a version. This allows for Neo4j instances of
 * different versions to exchange transaction data, either directly or via logical logs.
 *
 * Read all about it at {@link LogEntryVersion}.
 */
public class VersionAwareLogEntryReader implements LogEntryReader
{
    private static final boolean VERIFY_CHECKSUM_CHAIN = FeatureToggles.flag( LogEntryReader.class, "verifyChecksumChain", false );
    private final CommandReaderFactory commandReaderFactory;
    private final LogPositionMarker positionMarker;
    private final boolean verifyChecksumChain;
    private LogEntryVersion version = LogEntryVersion.LATEST_VERSION;
    private int lastTxChecksum = BASE_TX_CHECKSUM;

    public VersionAwareLogEntryReader()
    {
        this( new ServiceLoadingCommandReaderFactory() );
    }

    public VersionAwareLogEntryReader( boolean verifyChecksumChain )
    {
        this( new ServiceLoadingCommandReaderFactory(), verifyChecksumChain );
    }

    public VersionAwareLogEntryReader( CommandReaderFactory commandReaderFactory )
    {
        this( commandReaderFactory, true );
    }

    public VersionAwareLogEntryReader( CommandReaderFactory commandReaderFactory, boolean verifyChecksumChain )
    {
        this.commandReaderFactory = commandReaderFactory;
        this.positionMarker = new LogPositionMarker();
        this.verifyChecksumChain = verifyChecksumChain;
    }

    @Override
    public LogEntry readLogEntry( ReadableClosablePositionAwareChecksumChannel channel ) throws IOException
    {
        try
        {
            while ( true )
            {
                channel.getCurrentPosition( positionMarker );

                byte versionCode = channel.get();
                if ( versionCode == 0 )
                {
                    // we reached the end of available records but still have space available in pre-allocated file
                    // we reset channel position to restore last read byte in case someone would like to re-read or check it again if possible
                    // and we report that we reach end of record stream from our point of view
                    if ( channel instanceof PositionableChannel )
                    {
                        resetChannelPosition( channel );
                    }
                    else
                    {
                        throw new IllegalStateException( "Log reader expects positionable channel to be able to reset offset. Current channel: " + channel );
                    }
                    return null;
                }
                if ( version.version() != versionCode )
                {
                    version = byVersion( versionCode );
                    // Since checksum is calculated over the whole entry we need to rewind and begin
                    // a new checksum segment if we change version parser.
                    resetChannelPosition( channel );
                    channel.beginChecksum();
                    channel.get();
                }

                byte typeCode = channel.get();

                LogEntryParser entryReader;
                LogEntry entry;
                try
                {
                    entryReader = version.entryParser( typeCode );
                    entry = entryReader.parse( version, channel, positionMarker, commandReaderFactory );
                }
                catch ( ReadPastEndException e )
                {   // Make these exceptions slip by straight out to the outer handler
                    throw e;
                }
                catch ( Exception e )
                {   // Tag all other exceptions with log position and other useful information
                    LogPosition position = positionMarker.newPosition();
                    withMessage( e, e.getMessage() + ". At position " + position + " and entry version " + version );
                    throwIfInstanceOf( e, UnsupportedLogVersionException.class );
                    throw new IOException( e );
                }

                verifyChecksumChain( entry );
                return entry;
            }
        }
        catch ( ReadPastEndException e )
        {
            return null;
        }
    }

    private void verifyChecksumChain( LogEntry e )
    {
        if ( VERIFY_CHECKSUM_CHAIN && verifyChecksumChain )
        {
            if ( e instanceof LogEntryStart )
            {
                int previousChecksum = ((LogEntryStart) e).getPreviousChecksum();
                if ( lastTxChecksum != BASE_TX_CHECKSUM )
                {
                    if ( previousChecksum != lastTxChecksum )
                    {
                        throw new IllegalStateException( "The checksum chain is broken. " + positionMarker );
                    }
                }
            }
            else if ( e instanceof LogEntryCommit )
            {
                lastTxChecksum = ((LogEntryCommit) e).getChecksum();
            }
        }
    }

    private void resetChannelPosition( ReadableClosablePositionAwareChecksumChannel channel ) throws IOException
    {
        //take current position
        channel.getCurrentPosition( positionMarker );
        PositionableChannel positionableChannel = (PositionableChannel) channel;
        positionableChannel.setCurrentPosition( positionMarker.getByteOffset() - 1 );
        // refresh with reset position
        channel.getCurrentPosition( positionMarker );
    }

    @Override
    public LogPosition lastPosition()
    {
        return positionMarker.newPosition();
    }
}
