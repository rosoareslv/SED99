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
package org.neo4j.kernel.ha.cluster;

/**
 * These callback methods correspond to broadcasted HA events. The supplied event argument contains the
 * result of the state change and required information, as interpreted by the HA state machine.
 */
public interface HighAvailabilityMemberListener
{
    void masterIsElected( HighAvailabilityMemberChangeEvent event );

    void masterIsAvailable( HighAvailabilityMemberChangeEvent event );

    void slaveIsAvailable( HighAvailabilityMemberChangeEvent event );

    void instanceStops( HighAvailabilityMemberChangeEvent event );

    /**
     * This event is different than the rest, in the sense that it is not a response to a broadcasted message,
     * rather than the interpretation of the loss of connectivity to other cluster members. This corresponds generally
     * to a loss of quorum but a special case is the event of being partitioned away completely from the cluster.
     */
    void instanceDetached( HighAvailabilityMemberChangeEvent event );

    class Adapter implements HighAvailabilityMemberListener
    {
        @Override
        public void masterIsElected( HighAvailabilityMemberChangeEvent event )
        {
        }

        @Override
        public void masterIsAvailable( HighAvailabilityMemberChangeEvent event )
        {
        }

        @Override
        public void slaveIsAvailable( HighAvailabilityMemberChangeEvent event )
        {
        }

        @Override
        public void instanceStops( HighAvailabilityMemberChangeEvent event )
        {
        }

        @Override
        public void instanceDetached( HighAvailabilityMemberChangeEvent event )
        {
        }
    }
}
