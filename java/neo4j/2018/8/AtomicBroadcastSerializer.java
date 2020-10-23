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
package org.neo4j.cluster.protocol.atomicbroadcast;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;

/**
 * Serializes and deserializes value to/from Payloads.
 */
public class AtomicBroadcastSerializer
{
    private ObjectInputStreamFactory objectInputStreamFactory;
    private ObjectOutputStreamFactory objectOutputStreamFactory;

    public AtomicBroadcastSerializer()
    {
        this( new ObjectStreamFactory(), new ObjectStreamFactory() );
    }

    public AtomicBroadcastSerializer( ObjectInputStreamFactory objectInputStreamFactory,
            ObjectOutputStreamFactory objectOutputStreamFactory )
    {
        this.objectInputStreamFactory = objectInputStreamFactory;
        this.objectOutputStreamFactory = objectOutputStreamFactory;
    }

    public Payload broadcast( Object value )
            throws IOException
    {
        ByteArrayOutputStream bout = new ByteArrayOutputStream();
        byte[] bytes;
        try ( ObjectOutputStream oout = objectOutputStreamFactory.create( bout ) )
        {
            oout.writeObject( value );
        }
        bytes = bout.toByteArray();
        return new Payload( bytes, bytes.length );
    }

    public Object receive( Payload payload )
            throws IOException, ClassNotFoundException
    {
        ByteArrayInputStream in = new ByteArrayInputStream( payload.getBuf(), 0, payload.getLen() );
        try ( ObjectInputStream oin = objectInputStreamFactory.create( in ) )
        {
            return oin.readObject();
        }
    }
}
