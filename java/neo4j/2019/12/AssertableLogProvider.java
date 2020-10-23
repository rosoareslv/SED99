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
package org.neo4j.logging;

import org.hamcrest.Description;
import org.hamcrest.Matcher;
import org.hamcrest.Matchers;
import org.hamcrest.StringDescription;
import org.junit.rules.TestRule;
import org.junit.runners.model.Statement;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.IllegalFormatException;
import java.util.Iterator;
import java.util.List;
import java.util.Queue;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.function.Consumer;
import java.util.function.Function;
import javax.annotation.Nonnull;

import org.neo4j.internal.helpers.collection.Iterators;

import static java.lang.String.format;
import static java.util.Arrays.asList;
import static org.apache.commons.text.StringEscapeUtils.escapeJava;
import static org.hamcrest.CoreMatchers.anyOf;
import static org.hamcrest.Matchers.any;
import static org.hamcrest.Matchers.arrayContaining;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

public class AssertableLogProvider extends AbstractLogProvider<Log> implements TestRule
{
    private final boolean debugEnabled;
    private final Queue<LogCall> logCalls = new ConcurrentLinkedQueue<>();

    public AssertableLogProvider()
    {
        this( false );
    }

    public AssertableLogProvider( boolean debugEnabled )
    {
        this.debugEnabled = debugEnabled;
    }

    @Override
    public Statement apply( final Statement base, org.junit.runner.Description description )
    {
        return new Statement()
        {
            @Override
            public void evaluate() throws Throwable
            {
                try
                {
                    base.evaluate();
                }
                catch ( Throwable failure )
                {
                    print( System.out );
                    throw failure;
                }
            }
        };
    }

    public void print( PrintStream out )
    {
        for ( LogCall call : logCalls )
        {
            out.println( call.toLogLikeString() );
            if ( call.throwable != null )
            {
                call.throwable.printStackTrace( out );
            }
        }
    }

    public enum Level
    {
        DEBUG,
        INFO,
        WARN,
        ERROR
    }

    private static final class LogCall
    {
        private final String context;
        private final Level level;
        private final String message;
        private final Object[] arguments;
        private final Throwable throwable;

        private LogCall( String context, Level level, String message, Object[] arguments, Throwable throwable )
        {
            this.level = level;
            this.context = context;
            this.message = message;
            this.arguments = arguments;
            this.throwable = throwable;
        }

        @Override
        public String toString()
        {
            StringBuilder builder = new StringBuilder( "LogCall{" );
            builder.append( context );
            builder.append( " " );
            builder.append( level );
            builder.append( ", message=" );
            if ( message != null )
            {
                builder.append( '\'' ).append( escapeJava( message ) ).append( '\'' );
            }
            else
            {
                builder.append( "null" );
            }
            builder.append( ", arguments=" );
            if ( arguments != null )
            {
                builder.append( "[" );
                boolean first = true;
                for ( Object arg : arguments )
                {
                    if ( !first )
                    {
                        builder.append( ',' );
                    }
                    first = false;
                    builder.append( escapeJava( "" + arg ) );
                }
                builder.append( "]" );
            }
            else
            {
                builder.append( "null" );
            }
            builder.append( ", throwable=" );
            if ( throwable != null )
            {
                builder.append( '\'' ).append( escapeJava( throwable.toString() ) ).append( '\'' );
            }
            else
            {
                builder.append( "null" );
            }
            builder.append( "}" );
            return builder.toString();
        }

        String toLogLikeString()
        {
            String msg;
            if ( arguments != null )
            {
                try
                {
                    msg = format( message, arguments );
                }
                catch ( IllegalFormatException e )
                {
                    msg = format( "IllegalFormat{message: \"%s\", arguments: %s}",
                            message, Arrays.toString( arguments ) );
                }
            }
            else
            {
                msg = message;
            }
            return format( "%s @ %s: %s", level, context, msg );
        }
    }

    private class LogCallRecorder implements Logger
    {
        private final String context;
        private final Level level;

        LogCallRecorder( String context, Level level )
        {
            this.context = context;
            this.level = level;
        }

        @Override
        public void log( @Nonnull String message )
        {
            logCalls.add( new LogCall( context, level, message, null, null ) );
        }

        @Override
        public void log( @Nonnull String message, @Nonnull Throwable throwable )
        {
            logCalls.add( new LogCall( context, level, message, null, throwable ) );
        }

        @Override
        public void log( @Nonnull String format, @Nonnull Object... arguments )
        {
            logCalls.add( new LogCall( context, level, format, arguments, null ) );
        }

        @Override
        public void bulk( @Nonnull Consumer<Logger> consumer )
        {
            consumer.accept( this );
        }
    }

    private class AssertableLog extends AbstractLog
    {
        private final Logger debugLogger;
        private final Logger infoLogger;
        private final Logger warnLogger;
        private final Logger errorLogger;

        AssertableLog( String context )
        {
            this.debugLogger = new LogCallRecorder( context, Level.DEBUG );
            this.infoLogger = new LogCallRecorder( context, Level.INFO );
            this.warnLogger = new LogCallRecorder( context, Level.WARN );
            this.errorLogger = new LogCallRecorder( context, Level.ERROR );
        }

        @Override
        public boolean isDebugEnabled()
        {
            return debugEnabled;
        }

        @Nonnull
        @Override
        public Logger debugLogger()
        {
            return debugLogger;
        }

        @Nonnull
        @Override
        public Logger infoLogger()
        {
            return infoLogger;
        }

        @Nonnull
        @Override
        public Logger warnLogger()
        {
            return warnLogger;
        }

        @Nonnull
        @Override
        public Logger errorLogger()
        {
            return errorLogger;
        }

        @Override
        public void bulk( @Nonnull Consumer<Log> consumer )
        {
            consumer.accept( this );
        }
    }

    @Override
    protected Log buildLog( Class<?> loggingClass )
    {
        return new AssertableLog( loggingClass.getName() );
    }

    @Override
    protected Log buildLog( String context )
    {
        return new AssertableLog( context );
    }

    //
    // TEST TOOLS
    //

    private static final Matcher<Level> DEBUG_LEVEL_MATCHER = equalTo( Level.DEBUG );
    private static final Matcher<Level> INFO_LEVEL_MATCHER = equalTo( Level.INFO );
    private static final Matcher<Level> WARN_LEVEL_MATCHER = equalTo( Level.WARN );
    private static final Matcher<Level> ERROR_LEVEL_MATCHER = equalTo( Level.ERROR );
    private static final Matcher<Level> ANY_LEVEL_MATCHER = any( Level.class );
    private static final Matcher<String> ANY_MESSAGE_MATCHER = anyOf( any( String.class ), nullValue() );
    private static final Matcher<Object[]> NULL_ARGUMENTS_MATCHER = nullValue( Object[].class );
    private static final Matcher<Object[]> ANY_ARGUMENTS_MATCHER = anyOf( any( Object[].class ), nullValue() );
    private static final Matcher<Throwable> NULL_THROWABLE_MATCHER = nullValue( Throwable.class );
    private static final Matcher<Throwable> ANY_THROWABLE_MATCHER = anyOf( any( Throwable.class ), nullValue() );

    public static final class LogMatcher
    {
        private final Matcher<String> contextMatcher;
        private final Matcher<Level> levelMatcher;
        private final Matcher<String> messageMatcher;
        private final Matcher<? extends Object[]> argumentsMatcher;
        private final Matcher<? extends Throwable> throwableMatcher;

        public LogMatcher( Matcher<String> contextMatcher, Matcher<Level> levelMatcher, Matcher<String> messageMatcher,
                Matcher<? extends Object[]> argumentsMatcher, Matcher<? extends Throwable> throwableMatcher )
        {
            this.contextMatcher = contextMatcher;
            this.levelMatcher = levelMatcher;
            this.messageMatcher = messageMatcher;
            this.argumentsMatcher = argumentsMatcher;
            this.throwableMatcher = throwableMatcher;
        }

        protected boolean matches( LogCall logCall )
        {
            return logCall != null &&
                    contextMatcher.matches( logCall.context ) &&
                    levelMatcher.matches( logCall.level ) &&
                    messageMatcher.matches( logCall.message ) &&
                    argumentsMatcher.matches( logCall.arguments ) &&
                    throwableMatcher.matches( logCall.throwable );
        }

        @Override
        public String toString()
        {
            Description description = new StringDescription();
            description.appendText( "LogMatcher{" );
            description.appendDescriptionOf( contextMatcher );
            description.appendText( ", " );
            description.appendDescriptionOf( levelMatcher );
            description.appendText( ", message=" );
            description.appendDescriptionOf( messageMatcher );
            description.appendText( ", arguments=" );
            description.appendDescriptionOf( argumentsMatcher );
            description.appendText( ", throwable=" );
            description.appendDescriptionOf( throwableMatcher );
            description.appendText( "}" );
            return description.toString();
        }
    }

    public static final class LogMatcherBuilder
    {
        private final Matcher<String> contextMatcher;

        private LogMatcherBuilder( Matcher<String> contextMatcher )
        {
            this.contextMatcher = contextMatcher;
        }

        public LogMatcher debug( String message )
        {
            return new LogMatcher( contextMatcher, DEBUG_LEVEL_MATCHER, equalTo( message ), NULL_ARGUMENTS_MATCHER,
                    NULL_THROWABLE_MATCHER );
        }

        public LogMatcher debug( Matcher<String> messageMatcher )
        {
            return new LogMatcher( contextMatcher, DEBUG_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER,
                    NULL_THROWABLE_MATCHER );
        }

        public LogMatcher debug( Matcher<String> messageMatcher, Matcher<Throwable> throwableMatcher )
        {
            return new LogMatcher( contextMatcher, DEBUG_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER,
                    throwableMatcher );
        }

        public LogMatcher debug( String format, Object... arguments )
        {
            return debug( equalTo( format ), arguments );
        }

        public LogMatcher debug( Matcher<String> format, Object... arguments )
        {
            return new LogMatcher( contextMatcher, DEBUG_LEVEL_MATCHER, format,
                    arrayContaining( ensureMatchers( arguments ) ), NULL_THROWABLE_MATCHER );
        }

        public LogMatcher info( String message )
        {
            return new LogMatcher( contextMatcher, INFO_LEVEL_MATCHER, equalTo( message ), NULL_ARGUMENTS_MATCHER, NULL_THROWABLE_MATCHER );
        }

        public LogMatcher info( Matcher<String> messageMatcher )
        {
            return new LogMatcher( contextMatcher, INFO_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER, NULL_THROWABLE_MATCHER );
        }

        public LogMatcher info( Matcher<String> messageMatcher, Matcher<Throwable> throwableMatcher )
        {
            return new LogMatcher( contextMatcher, INFO_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER, throwableMatcher );
        }

        public LogMatcher info( String format, Object... arguments )
        {
            return info( equalTo( format ), arguments );
        }

        public LogMatcher info( Matcher<String> format, Object... arguments )
        {
            return new LogMatcher( contextMatcher, INFO_LEVEL_MATCHER, format,
                    arrayContaining( ensureMatchers( arguments ) ), NULL_THROWABLE_MATCHER );
        }

        public LogMatcher warn( String message )
        {
            return new LogMatcher( contextMatcher, WARN_LEVEL_MATCHER, equalTo( message ), NULL_ARGUMENTS_MATCHER,
                    NULL_THROWABLE_MATCHER );
        }

        public LogMatcher warn( Matcher<String> messageMatcher )
        {
            return new LogMatcher( contextMatcher, WARN_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER,
                    NULL_THROWABLE_MATCHER );
        }

        public LogMatcher warn( Matcher<String> messageMatcher, Matcher<Throwable> throwableMatcher )
        {
            return new LogMatcher( contextMatcher, WARN_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER, throwableMatcher );
        }

        public LogMatcher warn( String format, Object... arguments )
        {
            return warn( equalTo( format ), arguments );
        }

        public LogMatcher warn( Matcher<String> format, Object... arguments )
        {
            return new LogMatcher( contextMatcher, WARN_LEVEL_MATCHER, format,
                    arrayContaining( ensureMatchers( arguments ) ), NULL_THROWABLE_MATCHER );
        }

        public LogMatcher anyError()
        {
            return new LogMatcher( contextMatcher, ERROR_LEVEL_MATCHER, Matchers.any( String.class ),
                    ANY_ARGUMENTS_MATCHER, ANY_THROWABLE_MATCHER );
        }

        public LogMatcher error( String message )
        {
            return new LogMatcher( contextMatcher, ERROR_LEVEL_MATCHER, equalTo( message ), NULL_ARGUMENTS_MATCHER,
                    NULL_THROWABLE_MATCHER );
        }

        public LogMatcher error( Matcher<String> messageMatcher )
        {
            return new LogMatcher( contextMatcher, ERROR_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER, NULL_THROWABLE_MATCHER );
        }

        public LogMatcher error( Matcher<String> messageMatcher, Matcher<? extends Throwable> throwableMatcher )
        {
            return new LogMatcher( contextMatcher, ERROR_LEVEL_MATCHER, messageMatcher, NULL_ARGUMENTS_MATCHER, throwableMatcher );
        }

        public LogMatcher error( String format, Object... arguments )
        {
            return error( equalTo( format ), arguments );
        }

        public LogMatcher error( Matcher<String> format, Object... arguments )
        {
            return new LogMatcher( contextMatcher, ERROR_LEVEL_MATCHER, format,
                    arrayContaining( ensureMatchers( arguments ) ), NULL_THROWABLE_MATCHER );
        }

        public LogMatcher any()
        {
            return new LogMatcher(
                    contextMatcher, ANY_LEVEL_MATCHER, ANY_MESSAGE_MATCHER,
                    anyOf( NULL_ARGUMENTS_MATCHER, ANY_ARGUMENTS_MATCHER ),
                    anyOf( NULL_THROWABLE_MATCHER, ANY_THROWABLE_MATCHER ) );
        }

        @SuppressWarnings( "unchecked" )
        private Matcher<Object>[] ensureMatchers( Object... arguments )
        {
            List<Matcher> matchers = new ArrayList<>();
            for ( Object arg : arguments )
            {
                if ( arg instanceof Matcher )
                {
                    matchers.add( (Matcher) arg );
                }
                else
                {
                    matchers.add( equalTo( arg ) );
                }
            }
            return matchers.toArray( new Matcher[arguments.length] );
        }
    }

    public static LogMatcherBuilder inLog( Class logClass )
    {
        return inLog( equalTo( logClass.getName() ) );
    }

    public static LogMatcherBuilder inLog( String context )
    {
        return inLog( equalTo( context ) );
    }

    public static LogMatcherBuilder inLog( Matcher<String> contextMatcher )
    {
        return new LogMatcherBuilder( contextMatcher );
    }

    public void assertExactly( LogMatcher... expected )
    {
        Iterator<LogMatcher> expectedIterator = asList( expected ).iterator();

        Iterator<LogCall> callsIterator = logCalls.iterator();

        while ( expectedIterator.hasNext() )
        {
            if ( callsIterator.hasNext() )
            {
                LogMatcher logMatcher = expectedIterator.next();
                LogCall logCall = callsIterator.next();
                if ( !logMatcher.matches( logCall ) )
                {
                    fail( format( "Log call did not match expectation\n  Expected: %s\n  Call was: %s", logMatcher, logCall ) );
                }
            }
            else
            {
                fail( format( "Got fewer log calls than expected. The missing log calls were:\n%s", describe( expectedIterator ) ) );
            }
        }

        if ( callsIterator.hasNext() )
        {
            fail( format( "Got more log calls than expected. The remaining log calls were:\n%s", serialize( callsIterator ) ) );
        }
    }

    /**
     * @return a {@link MessageMatcher} which compares the raw messages, i.e. even for format strings that gets an array of arguments
     * passed along with it the comparison will be on the message string with the formatting indicators intact and no arguments formatted in.
     */
    public MessageMatcher rawMessageMatcher()
    {
        return new MessageMatcher( logCall -> logCall.message );
    }

    /**
     * @return a {@link MessageMatcher} which compares the formatted messages, i.e. after the message and its arguments have
     * been formatted by {@link String#format(String, Object...)} - the resulting log messages.
     */
    public MessageMatcher formattedMessageMatcher()
    {
        return new MessageMatcher( LogCall::toLogLikeString );
    }

    /**
     * @return a {@link MessageMatcher} which compares strings from {@link LogCall#toString()}.
     */
    public MessageMatcher internalToStringMessageMatcher()
    {
        return new MessageMatcher( LogCall::toString );
    }

    @SafeVarargs
    private final void assertContains( int logSkipCount, Function<LogCall,String> stringifyer, Matcher<String>... matchers )
    {
        LogCall[] calls = logCalls.stream().skip( logSkipCount ).toArray( LogCall[]::new );
        assertEquals( calls.length, matchers.length );
        for ( int i = 0; i < matchers.length; i++ )
        {
            LogCall logCall = calls[i];
            Matcher<String> matcher = matchers[i];

            if ( !matcher.matches( stringifyer.apply( logCall ) ) )
            {
                StringDescription description = new StringDescription();
                description.appendDescriptionOf( matcher );
                fail( format( "Expected log statement with message as %s, but none found. Actual log call was:\n%s",
                        description.toString(), logCall.toString() ) );
            }
        }
    }

    /**
     * Note: Does not care about ordering.
     */
    public void assertAtLeastOnce( LogMatcher... expected )
    {
        Set<LogMatcher> expectedMatchers = Iterators.asSet( expected );
        expectedMatchers.removeIf( this::containsMatchingLogCall );

        if ( !expectedMatchers.isEmpty() )
        {
            fail( format(
                    "These log calls were expected, but never occurred:\n%s\nActual log calls were:\n%s",
                    describe( expectedMatchers.iterator() ),
                    serialize( logCalls.iterator() )
            ) );
        }
    }

    public void assertNone( LogMatcher notExpected )
    {
        LogCall logCall = firstMatchingLogCall( notExpected );
        if ( logCall != null )
        {
            fail( format(
                    "Log call was not expected, but occurred:\n%s\n", logCall.toString()
            ) );
        }
    }

    private void assertNotContains( String partOfMessage, Function<LogCall,String> stringifyer )
    {
        if ( containsLogCallContaining( partOfMessage, stringifyer ) )
        {
            fail( format(
                    "Expected no log statement containing '%s', but at least one found. Actual log calls were:\n%s",
                    partOfMessage, serialize( logCalls.iterator() ) ) );
        }
    }

    private void assertContains( String partOfMessage, Function<LogCall,String> stringifyer )
    {
        for ( LogCall logCall : logCalls )
        {
            if ( stringifyer.apply( logCall ).contains( partOfMessage ) )
            {
                return;
            }
        }
        fail( format(
                "Expected at least one log strings containing '%s', but none found. Actual log calls were:\n%s",
                partOfMessage, serialize( logCalls.iterator() ) ) );
    }

    private boolean containsLogCallContaining( String partOfMessage, Function<LogCall,String> stringifyer )
    {
        for ( LogCall logCall : logCalls )
        {
            if ( stringifyer.apply( logCall ).contains( partOfMessage ) )
            {
                return true;
            }
        }
        return false;
    }

    public boolean containsMatchingLogCall( LogMatcher logMatcher )
    {
        return firstMatchingLogCall( logMatcher ) != null;
    }

    private LogCall firstMatchingLogCall( LogMatcher logMatcher )
    {
        for ( LogCall logCall : logCalls )
        {
            if ( logMatcher.matches( logCall ) )
            {
                return logCall;
            }
        }
        return null;
    }

    private void assertContains( Matcher<String> messageMatcher, Function<LogCall,String> stringifyer )
    {
        for ( LogCall logCall : logCalls )
        {
            if ( messageMatcher.matches( stringifyer.apply( logCall ) ) )
            {
                return;
            }
        }
        StringDescription description = new StringDescription();
        description.appendDescriptionOf( messageMatcher );
        fail( format(
                "Expected at least one log statement with message as %s, but none found. Actual log calls were:\n%s",
                description.toString(), serialize( logCalls.iterator() ) ) );
    }

    private void assertContainsSingle( Matcher<String> messageMatcher, Function<LogCall,String> stringifyer )
    {
        boolean found = false;
        for ( LogCall logCall : logCalls )
        {
            if ( messageMatcher.matches( stringifyer.apply( logCall ) ) )
            {
                if ( !found )
                {
                    found = true;
                }
                else
                {
                    StringDescription description = new StringDescription();
                    description.appendDescriptionOf( messageMatcher );
                    fail( format( "Expected exactly one log statement with message as %s, but multiple found. Actual log calls were:%n%s",
                            description.toString(), serialize( logCalls.iterator() ) ) );
                }
            }
        }
        if ( !found )
        {
            StringDescription description = new StringDescription();
            description.appendDescriptionOf( messageMatcher );
            fail( format(
                    "Expected at least one log statement with message as %s, but none found. Actual log calls were:\n%s",
                    description.toString(), serialize( logCalls.iterator() ) ) );
        }
    }

    public void assertNoLoggingOccurred()
    {
        if ( !logCalls.isEmpty() )
        {
            fail( format( "Expected no log messages at all, but got:\n%s", serialize( logCalls.iterator() ) ) );
        }
    }

    /**
     * Clear this logger for re-use.
     */
    public void clear()
    {
        logCalls.clear();
    }

    public String serialize()
    {
        return serialize( logCalls.iterator(), LogCall::toLogLikeString );
    }

    private String describe( Iterator<LogMatcher> matchers )
    {
        StringBuilder sb = new StringBuilder();
        while ( matchers.hasNext() )
        {
            sb.append( matchers.next().toString() );
            sb.append( "\n" );
        }
        return sb.toString();
    }

    private String serialize( Iterator<LogCall> events )
    {
        return serialize( events, LogCall::toString );
    }

    private String serialize( Iterator<LogCall> events, Function<LogCall,String> serializer )
    {
        StringBuilder sb = new StringBuilder();
        while ( events.hasNext() )
        {
            sb.append( serializer.apply( events.next() ) );
            sb.append( "\n" );
        }
        return sb.toString();
    }

    public class MessageMatcher
    {
        private final Function<LogCall,String> stringifyer;

        MessageMatcher( Function<LogCall,String> stringifyer )
        {
            this.stringifyer = stringifyer;
        }

        public void assertContainsSingle( Matcher<String> messageMatcher )
        {
            AssertableLogProvider.this.assertContainsSingle( messageMatcher, stringifyer );
        }

        public void assertContains( Matcher<String> messageMatcher )
        {
            AssertableLogProvider.this.assertContains( messageMatcher, stringifyer );
        }

        public void assertContains( String partOfMessage )
        {
            AssertableLogProvider.this.assertContains( partOfMessage, stringifyer );
        }

        @SafeVarargs
        public final void assertContains( int logSkipCount, Matcher<String>... matchers )
        {
            AssertableLogProvider.this.assertContains( logSkipCount, stringifyer, matchers );
        }

        public void assertNotContains( String partOfMessage )
        {
            AssertableLogProvider.this.assertNotContains( partOfMessage, stringifyer );
        }
    }
}
