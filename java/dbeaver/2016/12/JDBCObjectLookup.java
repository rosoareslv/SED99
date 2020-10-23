/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2016 Serge Rieder (serge@jkiss.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
package org.jkiss.dbeaver.model.impl.jdbc.cache;

import org.jkiss.code.NotNull;
import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.model.exec.jdbc.JDBCSession;
import org.jkiss.dbeaver.model.exec.jdbc.JDBCStatement;
import org.jkiss.dbeaver.model.struct.DBSObject;

import java.sql.SQLException;

/**
 * Extension of {@link JDBCObjectCache} - support object lookup by name
 */
public interface JDBCObjectLookup<OWNER extends DBSObject, OBJECT extends DBSObject>
{
    /**
     * Creates statement to read just one object.
     * Parameter @object OR @objectName may be specified to find an object
     * @throws SQLException
     */
    @NotNull
    JDBCStatement prepareLookupStatement(@NotNull JDBCSession session, @NotNull OWNER owner, @Nullable OBJECT object, @Nullable String objectName)
        throws SQLException;

}
