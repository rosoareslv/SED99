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
package org.neo4j.server.security.auth;

import org.junit.jupiter.api.Test;

import org.neo4j.internal.kernel.api.security.AuthenticationResult;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.neo4j.internal.kernel.api.security.AuthenticationResult.FAILURE;
import static org.neo4j.internal.kernel.api.security.AuthenticationResult.PASSWORD_CHANGE_REQUIRED;
import static org.neo4j.internal.kernel.api.security.AuthenticationResult.SUCCESS;
import static org.neo4j.internal.kernel.api.security.AuthenticationResult.TOO_MANY_ATTEMPTS;

class ShiroAuthenticationInfoTest
{
    private final ShiroAuthenticationInfo successInfo = new ShiroAuthenticationInfo( "user", "realm", SUCCESS );
    private final ShiroAuthenticationInfo failureInfo = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
    private final ShiroAuthenticationInfo tooManyAttemptsInfo = new ShiroAuthenticationInfo( "user", "realm", TOO_MANY_ATTEMPTS );
    private final ShiroAuthenticationInfo pwChangeRequiredInfo = new ShiroAuthenticationInfo( "user", "realm", PASSWORD_CHANGE_REQUIRED );

    // These tests are here to remind you that you need to update the ShiroAuthenticationInfo.mergeMatrix[][]
    // whenever you add/remove/move values in the AuthenticationResult enum

    @Test
    void shouldChangeMergeMatrixIfAuthenticationResultEnumChanges()
    {
        // These are the assumptions made for ShiroAuthenticationInfo.mergeMatrix[][]
        // which have to stay in sync with the enum
        assertEquals( AuthenticationResult.SUCCESS.ordinal(), 0 );
        assertEquals( AuthenticationResult.FAILURE.ordinal(), 1 );
        assertEquals( AuthenticationResult.TOO_MANY_ATTEMPTS.ordinal(), 2 );
        assertEquals( AuthenticationResult.PASSWORD_CHANGE_REQUIRED.ordinal(), 3 );
        assertEquals( AuthenticationResult.values().length, 4 );
    }

    @Test
    void shouldMergeTwoSuccessToSameValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", SUCCESS );
        info.merge( successInfo );

        assertEquals( info.getAuthenticationResult(), SUCCESS );
    }

    @Test
    void shouldMergeTwoFailureToSameValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
        info.merge( failureInfo );

        assertEquals( info.getAuthenticationResult(), FAILURE );
    }

    @Test
    void shouldMergeTwoTooManyAttemptsToSameValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", TOO_MANY_ATTEMPTS );
        info.merge( tooManyAttemptsInfo );

        assertEquals( info.getAuthenticationResult(), TOO_MANY_ATTEMPTS );
    }

    @Test
    void shouldMergeTwoPasswordChangeRequiredToSameValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", PASSWORD_CHANGE_REQUIRED );
        info.merge( pwChangeRequiredInfo );

        assertEquals( info.getAuthenticationResult(), PASSWORD_CHANGE_REQUIRED );
    }

    @Test
    void shouldMergeFailureWithSuccessToNewValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
        info.merge( successInfo );

        assertEquals( info.getAuthenticationResult(), SUCCESS );
    }

    @Test
    void shouldMergeFailureWithTooManyAttemptsToNewValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
        info.merge( tooManyAttemptsInfo );

        assertEquals( info.getAuthenticationResult(), TOO_MANY_ATTEMPTS );
    }

    @Test
    void shouldMergeFailureWithPasswordChangeRequiredToNewValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
        info.merge( pwChangeRequiredInfo );

        assertEquals( info.getAuthenticationResult(), PASSWORD_CHANGE_REQUIRED );
    }

    @Test
    void shouldMergeToNewValue()
    {
        ShiroAuthenticationInfo info = new ShiroAuthenticationInfo( "user", "realm", FAILURE );
        info.merge( pwChangeRequiredInfo );

        assertEquals( info.getAuthenticationResult(), PASSWORD_CHANGE_REQUIRED );
    }
}
