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
package org.neo4j.bolt.v1.runtime;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.InOrder;

import java.util.Optional;

import org.neo4j.bolt.runtime.BoltResult;
import org.neo4j.bolt.runtime.BoltResultHandle;
import org.neo4j.bolt.runtime.StatementMetadata;
import org.neo4j.bolt.v1.runtime.TransactionStateMachine.MutableTransactionState;
import org.neo4j.bolt.v1.runtime.TransactionStateMachine.StatementOutcome;
import org.neo4j.bolt.v1.runtime.bookmarking.Bookmark;
import org.neo4j.bolt.v4.messaging.ResultConsumer;
import org.neo4j.exceptions.KernelException;
import org.neo4j.helpers.collection.MapUtil;
import org.neo4j.internal.kernel.api.security.LoginContext;
import org.neo4j.kernel.api.KernelTransaction;
import org.neo4j.kernel.api.exceptions.Status;
import org.neo4j.kernel.impl.query.QueryExecutionKernelException;
import org.neo4j.kernel.impl.util.ValueUtils;
import org.neo4j.time.FakeClock;
import org.neo4j.values.virtual.MapValue;

import static java.util.Arrays.asList;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.hasSize;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.neo4j.bolt.security.auth.AuthenticationResult.AUTH_DISABLED;
import static org.neo4j.bolt.v4.messaging.MessageMetadataParser.ABSENT_DB_ID;
import static org.neo4j.values.virtual.VirtualValues.EMPTY_MAP;

class TransactionStateMachineTest
{
    private static final String PERIODIC_COMMIT_QUERY =
            "USING PERIODIC COMMIT 1 " +
            "LOAD CSV FROM ''https://neo4j.com/test.csv'' AS line " +
            "CREATE (:Node {id: line[0], name: line[1]})";

    private TransactionStateMachineV1SPI stateMachineSPI;
    private MutableTransactionState mutableState;
    private TransactionStateMachine stateMachine;
    private static EmptyResultConsumer EMPTY = new EmptyResultConsumer();
    private static EmptyResultConsumer ERROR = new EmptyResultConsumer()
    {
        @Override
        public void consume( BoltResult boltResult )
        {
            throw new RuntimeException( "some error" );
        }
    };

    @BeforeEach
    void createMocks()
    {
        FakeClock clock = new FakeClock();
        stateMachineSPI = mock( TransactionStateMachineV1SPI.class );
        mutableState = new MutableTransactionState( AUTH_DISABLED, clock );
        stateMachine = new TransactionStateMachine( ABSENT_DB_ID, stateMachineSPI, AUTH_DISABLED, clock );
    }

    @Test
    void shouldTransitionToExplicitTransactionOnBegin() throws Exception
    {
        assertEquals( TransactionStateMachine.State.EXPLICIT_TRANSACTION,
                TransactionStateMachine.State.AUTO_COMMIT.beginTransaction( mutableState, stateMachineSPI, null, null, null ) );
    }

    @Test
    void shouldTransitionToAutoCommitOnCommit() throws Exception
    {
        assertEquals( TransactionStateMachine.State.AUTO_COMMIT,
                TransactionStateMachine.State.EXPLICIT_TRANSACTION.commitTransaction( mutableState, stateMachineSPI ) );
    }

    @Test
    void shouldTransitionToAutoCommitOnRollback() throws Exception
    {
        assertEquals( TransactionStateMachine.State.AUTO_COMMIT,
                TransactionStateMachine.State.EXPLICIT_TRANSACTION.rollbackTransaction( mutableState, stateMachineSPI ) );
    }

    @Test
    void shouldThrowOnBeginInExplicitTransaction() throws Exception
    {
        QueryExecutionKernelException e = assertThrows( QueryExecutionKernelException.class, () ->
                TransactionStateMachine.State.EXPLICIT_TRANSACTION.beginTransaction( mutableState, stateMachineSPI, null, null, null ) );

        assertEquals( "Nested transactions are not supported.", e.getMessage() );
    }

    @Test
    void shouldAllowRollbackInAutoCommit() throws Exception
    {
        assertEquals( TransactionStateMachine.State.AUTO_COMMIT,
                TransactionStateMachine.State.AUTO_COMMIT.rollbackTransaction( mutableState, stateMachineSPI ) );
    }

    @Test
    void shouldThrowOnCommitInAutoCommit() throws Exception
    {
        QueryExecutionKernelException e = assertThrows( QueryExecutionKernelException.class, () ->
                TransactionStateMachine.State.AUTO_COMMIT.commitTransaction( mutableState, stateMachineSPI ) );

        assertEquals( "No current transaction to commit.", e.getMessage() );
    }

    @Test
    void shouldNotWaitWhenNoBookmarkSupplied() throws Exception
    {
        stateMachine.beginTransaction( null );
        verify( stateMachineSPI, never() ).awaitUpToDate( anyLong() );
    }

    @Test
    void shouldAwaitSingleBookmark() throws Exception
    {
        MapValue params = map( "bookmark", "neo4j:bookmark:v1:tx15" );
        stateMachine.beginTransaction( Bookmark.fromParamsOrNull( params ) );
        verify( stateMachineSPI ).awaitUpToDate( 15 );
    }

    @Test
    void shouldAwaitMultipleBookmarks() throws Exception
    {
        MapValue params = map( "bookmarks", asList(
                "neo4j:bookmark:v1:tx15", "neo4j:bookmark:v1:tx5", "neo4j:bookmark:v1:tx92", "neo4j:bookmark:v1:tx9" )
        );
        stateMachine.beginTransaction( Bookmark.fromParamsOrNull( params ) );
        verify( stateMachineSPI ).awaitUpToDate( 92 );
    }

    @Test
    void shouldAwaitMultipleBookmarksWhenBothSingleAndMultipleSupplied() throws Exception
    {
        MapValue params = map(
                "bookmark", "neo4j:bookmark:v1:tx42",
                "bookmarks", asList( "neo4j:bookmark:v1:tx47", "neo4j:bookmark:v1:tx67", "neo4j:bookmark:v1:tx45" )
        );
        stateMachine.beginTransaction( Bookmark.fromParamsOrNull( params ) );
        verify( stateMachineSPI ).awaitUpToDate( 67 );
    }

    @Test
    void shouldStartWithAutoCommitState()
    {
        TransactionStateMachineV1SPI stateMachineSPI = mock( TransactionStateMachineV1SPI.class );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );
        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );
    }

    @Test
    void shouldDoNothingInAutoCommitTransactionUponInitialisationWhenValidated() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        // We're in auto-commit state
        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );

        // call validate transaction
        stateMachine.validateTransaction();

        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );

        verify( transaction, never() ).getReasonIfTerminated();
        verify( transaction, never() ).failure();
        verify( transaction, never() ).close();
    }

    @Test
    void shouldTryToTerminateAllActiveStatements() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();

        BoltResultHandle resultHandle = newResultHandle();
        doThrow( new RuntimeException( "You shall not pass" ) ).doThrow( new RuntimeException( "Not pass twice" ) ).when( resultHandle ).terminate();
        TransactionStateMachineV1SPI stateMachineSPI = mock( TransactionStateMachineV1SPI.class );

        when( stateMachineSPI.beginTransaction( any(), any(), any() ) ).thenReturn( transaction );
        when( stateMachineSPI.executeQuery( any(), anyString(), any(), any(), any() ) ).thenReturn( resultHandle );
        when( stateMachineSPI.supportsNestedStatementsInTransaction() ).thenReturn( true ); // V4

        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        // We're in explicit-commit state
        stateMachine.beginTransaction( Bookmark.EMPTY_BOOKMARK );

        assertThat( stateMachine.state, is( TransactionStateMachine.State.EXPLICIT_TRANSACTION ) );
        assertNotNull( stateMachine.ctx.currentTransaction );

        // We run two statements
        stateMachine.run( "RETURN 1", null );
        stateMachine.run( "RETURN 2", null );

        assertThat( stateMachine.state, is( TransactionStateMachine.State.EXPLICIT_TRANSACTION ) );
        assertNotNull( stateMachine.ctx.currentTransaction );
        assertThat( stateMachine.ctx.statementCounter, equalTo( 2 ) );

        RuntimeException error = assertThrows( RuntimeException.class, () -> stateMachine.reset() );

        assertThat( error.getCause().getMessage(), equalTo( "You shall not pass" ) );
        assertThat( error.getSuppressed().length, equalTo( 1 ) );
        assertThat( error.getSuppressed()[0].getMessage(), equalTo( "Not pass twice" ) );
    }

    @Test
    void shouldResetInAutoCommitTransactionWhileStatementIsRunningWhenValidated() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        // We're in auto-commit state
        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );

        stateMachine.run( "RETURN 1", null );

        // We're in auto-commit state
        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNotNull( stateMachine.ctx.currentTransaction );

        // call validate transaction
        stateMachine.validateTransaction();

        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );
        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );

        verify( transaction ).getReasonIfTerminated();
        verify( transaction ).failure();
        verify( transaction ).close();
    }

    @Test
    void shouldResetInExplicitTransactionUponTxBeginWhenValidated() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        // start an explicit transaction
        stateMachine.beginTransaction( null );
        assertThat( stateMachine.state, is( TransactionStateMachine.State.EXPLICIT_TRANSACTION ) );
        assertNotNull( stateMachine.ctx.currentTransaction );

        // verify transaction, which is timed out
        stateMachine.validateTransaction();

        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );
        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );

        verify( transaction ).getReasonIfTerminated();
        verify( transaction ).failure();
        verify( transaction ).close();
    }

    @Test
    void shouldResetInExplicitTransactionWhileStatementIsRunningWhenValidated() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        // start an explicit transaction
        stateMachine.beginTransaction( null );
        assertThat( stateMachine.state, is( TransactionStateMachine.State.EXPLICIT_TRANSACTION ) );
        assertNotNull( stateMachine.ctx.currentTransaction );

        stateMachine.run( "RETURN 1", null );

        // verify transaction, which is timed out
        stateMachine.validateTransaction();

        assertThat( stateMachine.state, is( TransactionStateMachine.State.AUTO_COMMIT ) );
        assertNull( stateMachine.ctx.currentTransaction );
        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );

        verify( transaction ).getReasonIfTerminated();
        verify( transaction ).failure();
        verify( transaction ).close();
    }

    @Test
    void shouldUnbindTxAfterRun() throws Exception
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.run( "SOME STATEMENT", null );

        verify( stateMachineSPI ).unbindTransactionFromCurrentThread();
    }

    @Test
    void shouldUnbindTxAfterStreamResult() throws Throwable
    {
        KernelTransaction transaction = newTimedOutTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.run( "SOME STATEMENT", null );
        stateMachine.streamResult( StatementMetadata.ABSENT_QUERY_ID, EMPTY );

        verify( stateMachineSPI, times( 2 ) ).unbindTransactionFromCurrentThread();
    }

    @Test
    void shouldCloseResultAndTransactionHandlesWhenExecutionFails() throws Exception
    {
        KernelTransaction transaction = newTransaction();
        BoltResultHandle resultHandle = newResultHandle( new RuntimeException( "some error" ) );
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction, resultHandle );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        RuntimeException e = assertThrows( RuntimeException.class, () -> stateMachine.run( "SOME STATEMENT", null ) );
        assertEquals( "some error", e.getMessage() );

        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );
        assertNull( stateMachine.ctx.currentTransaction );
    }

    @Test
    void shouldCloseResultAndTransactionHandlesWhenConsumeFails() throws Exception
    {
        KernelTransaction transaction = newTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.run( "SOME STATEMENT", null );

        StatementOutcome outcome = stateMachine.ctx.statementOutcomes.get( StatementMetadata.ABSENT_QUERY_ID );
        assertNotNull( outcome );
        assertNotNull( outcome.resultHandle );
        assertNotNull( outcome.result );

        RuntimeException e = assertThrows( RuntimeException.class, () ->
        {
            stateMachine.streamResult( StatementMetadata.ABSENT_QUERY_ID, ERROR );
        } );
        assertEquals( "some error", e.getMessage() );

        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );
        assertNull( stateMachine.ctx.currentTransaction );
    }

    @Test
    void shouldCloseResultHandlesWhenExecutionFailsInExplicitTransaction() throws Exception
    {
        KernelTransaction transaction = newTransaction();
        BoltResultHandle resultHandle = newResultHandle( new RuntimeException( "some error" ) );
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction, resultHandle );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        RuntimeException e = assertThrows( RuntimeException.class, () ->
        {
            stateMachine.beginTransaction( null );
            stateMachine.run( "SOME STATEMENT", null );
        } );
        assertEquals( "some error", e.getMessage() );

        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );
        assertNotNull( stateMachine.ctx.currentTransaction );
    }

    @Test
    void shouldCloseResultHandlesWhenConsumeFailsInExplicitTransaction() throws Throwable
    {
        KernelTransaction transaction = newTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.beginTransaction( null );
        stateMachine.run( "SOME STATEMENT", null );

        StatementOutcome outcome = stateMachine.ctx.statementOutcomes.get( StatementMetadata.ABSENT_QUERY_ID );
        assertNotNull( outcome );
        assertNotNull( outcome.resultHandle );
        assertNotNull( outcome.result );

        RuntimeException e = assertThrows( RuntimeException.class, () ->
        {
            stateMachine.streamResult( StatementMetadata.ABSENT_QUERY_ID, ERROR );
        } );
        assertEquals( "some error", e.getMessage() );

        assertThat( stateMachine.ctx.statementOutcomes.entrySet(), hasSize( 0 ) );
        assertNotNull( stateMachine.ctx.currentTransaction );
    }

    @Test
    void shouldNotOpenExplicitTransactionForPeriodicCommitQuery() throws Exception
    {
        KernelTransaction transaction = newTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );
        when( stateMachineSPI.isPeriodicCommit( PERIODIC_COMMIT_QUERY ) ).thenReturn( true );

        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.run( PERIODIC_COMMIT_QUERY, EMPTY_MAP );

        // transaction was created only to stream back result of the periodic commit query
        assertEquals( transaction, stateMachine.ctx.currentTransaction );

        InOrder inOrder = inOrder( stateMachineSPI );
        inOrder.verify( stateMachineSPI ).isPeriodicCommit( PERIODIC_COMMIT_QUERY );
        // periodic commit query was executed without starting an explicit transaction
        inOrder.verify( stateMachineSPI ).executeQuery( any( LoginContext.class ), eq( PERIODIC_COMMIT_QUERY ), eq( EMPTY_MAP ), any(), any() );
        // explicit transaction was started only after query execution to stream the result
        inOrder.verify( stateMachineSPI ).beginTransaction( any( LoginContext.class ), any(), any() );
    }

    @Test
    void shouldNotMarkForTerminationWhenNoTransaction() throws Exception
    {
        KernelTransaction transaction = newTransaction();
        TransactionStateMachineV1SPI stateMachineSPI = newTransactionStateMachineSPI( transaction );

        TransactionStateMachine stateMachine = newTransactionStateMachine( stateMachineSPI );

        stateMachine.markCurrentTransactionForTermination();
        verify( transaction, never() ).markForTermination( any() );
    }

    private static KernelTransaction newTransaction()
    {
        KernelTransaction transaction = mock( KernelTransaction.class );

        when( transaction.isOpen() ).thenReturn( true );

        return transaction;
    }

    private static KernelTransaction newTimedOutTransaction()
    {
        KernelTransaction transaction = newTransaction();

        when( transaction.getReasonIfTerminated() ).thenReturn( Optional.of( Status.Transaction.TransactionTimedOut ) );

        return transaction;
    }

    private static TransactionStateMachine newTransactionStateMachine( TransactionStateMachineV1SPI stateMachineSPI )
    {
        return new TransactionStateMachine( ABSENT_DB_ID, stateMachineSPI, AUTH_DISABLED, new FakeClock() );
    }

    private static MapValue map( Object... keyValues )
    {
        return ValueUtils.asMapValue( MapUtil.map( keyValues ) );
    }

    private static TransactionStateMachineV1SPI newTransactionStateMachineSPI( KernelTransaction transaction ) throws KernelException
    {
        BoltResultHandle resultHandle = newResultHandle();
        TransactionStateMachineV1SPI stateMachineSPI = mock( TransactionStateMachineV1SPI.class );

        when( stateMachineSPI.beginTransaction( any(), any(), any() ) ).thenReturn( transaction );
        when( stateMachineSPI.executeQuery( any(), anyString(), any(), any(), any() ) ).thenReturn( resultHandle );

        return stateMachineSPI;
    }

    private static TransactionStateMachineV1SPI newTransactionStateMachineSPI( KernelTransaction transaction,
            BoltResultHandle resultHandle ) throws KernelException
    {
        TransactionStateMachineV1SPI stateMachineSPI = mock( TransactionStateMachineV1SPI.class );

        when( stateMachineSPI.beginTransaction( any(), any(), any() ) ).thenReturn( transaction );
        when( stateMachineSPI.executeQuery( any(), anyString(), any(), any(), any() ) ).thenReturn( resultHandle );

        return stateMachineSPI;
    }

    private static BoltResultHandle newResultHandle() throws KernelException
    {
        BoltResultHandle resultHandle = mock( BoltResultHandle.class );

        when( resultHandle.start() ).thenReturn( BoltResult.EMPTY );

        return resultHandle;
    }

    private static BoltResultHandle newResultHandle( Throwable t ) throws KernelException
    {
        BoltResultHandle resultHandle = mock( BoltResultHandle.class );

        when( resultHandle.start() ).thenThrow( t );

        return resultHandle;
    }

    private static class EmptyResultConsumer implements ResultConsumer
    {
        @Override
        public boolean hasMore()
        {
            return false;
        }

        @Override
        public void consume( BoltResult boltResult )
        {
        }
    }
}
