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
package org.neo4j.server.security.enterprise.auth.plugin;

import org.apache.shiro.crypto.hash.SimpleHash;
import org.junit.Test;

import java.util.List;

import org.neo4j.server.security.enterprise.auth.SecureHasher;
import org.neo4j.server.security.enterprise.auth.plugin.spi.AuthenticationInfo;
import org.neo4j.server.security.enterprise.auth.plugin.spi.CacheableAuthenticationInfo;
import org.neo4j.server.security.enterprise.auth.plugin.spi.CustomCacheableAuthenticationInfo;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class PluginAuthenticationInfoTest
{
    @Test
    public void shouldCreateCorrectAuthenticationInfo()
    {
        PluginAuthenticationInfo internalAuthInfo =
                PluginAuthenticationInfo.createCacheable( AuthenticationInfo.of( "thePrincipal" ), "theRealm", null );

        assertThat( (List<String>)internalAuthInfo.getPrincipals().asList(), containsInAnyOrder( "thePrincipal" ) );
    }

    @Test
    public void shouldCreateCorrectAuthenticationInfoFromCacheable()
    {
        SecureHasher hasher = mock( SecureHasher.class );
        when( hasher.hash( any() ) ).thenReturn( new SimpleHash( "some-hash" ) );

        PluginAuthenticationInfo internalAuthInfo =
                PluginAuthenticationInfo.createCacheable(
                        CacheableAuthenticationInfo.of( "thePrincipal", new byte[]{1} ),
                        "theRealm",
                        hasher
                );

        assertThat( (List<String>)internalAuthInfo.getPrincipals().asList(), containsInAnyOrder( "thePrincipal" ) );
    }

    @Test
    public void shouldCreateCorrectAuthenticationInfoFromCustomCacheable()
    {
        SecureHasher hasher = mock( SecureHasher.class );
        when( hasher.hash( any() ) ).thenReturn( new SimpleHash( "some-hash" ) );

        PluginAuthenticationInfo internalAuthInfo =
                PluginAuthenticationInfo.createCacheable(
                        CustomCacheableAuthenticationInfo.of( "thePrincipal", ignoredAuthToken -> true ),
                        "theRealm",
                        hasher
                );

        assertThat( (List<String>)internalAuthInfo.getPrincipals().asList(), containsInAnyOrder( "thePrincipal" ) );
    }
}
