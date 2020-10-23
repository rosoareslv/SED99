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
package org.neo4j.tools.txlog.checktypes;

import org.neo4j.kernel.impl.store.record.NodeRecord;
import org.neo4j.kernel.impl.transaction.command.Command;

class NodeCheckType extends CheckType<Command.NodeCommand,NodeRecord>
{
    NodeCheckType()
    {
        super( Command.NodeCommand.class );
    }

    @Override
    public NodeRecord before( Command.NodeCommand command )
    {
        return command.getBefore();
    }

    @Override
    public NodeRecord after( Command.NodeCommand command )
    {
        return command.getAfter();
    }

    @Override
    protected boolean inUseRecordsEqual( NodeRecord record1, NodeRecord record2 )
    {
        return record1.getNextProp() == record2.getNextProp() &&
               record1.getNextRel() == record2.getNextRel() &&
               record1.isDense() == record2.isDense() &&
               record1.getLabelField() == record2.getLabelField();
    }

    @Override
    public String name()
    {
        return "node";
    }
}
