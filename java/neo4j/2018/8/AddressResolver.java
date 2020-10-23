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
package org.neo4j.backup.impl;

import org.neo4j.helpers.AdvertisedSocketAddress;
import org.neo4j.helpers.HostnamePort;
import org.neo4j.kernel.configuration.Config;
import org.neo4j.kernel.impl.enterprise.configuration.OnlineBackupSettings;
import org.neo4j.kernel.impl.util.OptionalHostnamePort;

class AddressResolver
{
    HostnamePort resolveCorrectHAAddress( Config config, OptionalHostnamePort userProvidedAddress )
    {
        HostnamePort defaultValues = readDefaultConfigAddressHA( config );
        return new HostnamePort( userProvidedAddress.getHostname().orElse( defaultValues.getHost() ),
                userProvidedAddress.getPort().orElse( defaultValues.getPort() ) );
    }

    AdvertisedSocketAddress resolveCorrectCCAddress( Config config, OptionalHostnamePort userProvidedAddress )
    {
        AdvertisedSocketAddress defaultValue = readDefaultConfigAddressCC( config );
        return new AdvertisedSocketAddress( userProvidedAddress.getHostname().orElse( defaultValue.getHostname() ),
                userProvidedAddress.getPort().orElse( defaultValue.getPort() ) );
    }

    private HostnamePort readDefaultConfigAddressHA( Config config )
    {
        return config.get( OnlineBackupSettings.online_backup_server );
    }

    private AdvertisedSocketAddress readDefaultConfigAddressCC( Config config )
    {
        return asAdvertised( config.get( OnlineBackupSettings.online_backup_server ) );
    }

    private AdvertisedSocketAddress asAdvertised( HostnamePort listenSocketAddress )
    {
        return new AdvertisedSocketAddress( listenSocketAddress.getHost(), listenSocketAddress.getPort() );
    }
}
