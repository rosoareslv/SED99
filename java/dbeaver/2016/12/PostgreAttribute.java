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
package org.jkiss.dbeaver.ext.postgresql.model;

import org.jkiss.code.NotNull;
import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.DBException;
import org.jkiss.dbeaver.Log;
import org.jkiss.dbeaver.ext.postgresql.PostgreUtils;
import org.jkiss.dbeaver.model.DBPDataKind;
import org.jkiss.dbeaver.model.DBPHiddenObject;
import org.jkiss.dbeaver.model.DBPNamedObject2;
import org.jkiss.dbeaver.model.DBUtils;
import org.jkiss.dbeaver.model.exec.jdbc.JDBCResultSet;
import org.jkiss.dbeaver.model.impl.DBPositiveNumberTransformer;
import org.jkiss.dbeaver.model.impl.jdbc.JDBCUtils;
import org.jkiss.dbeaver.model.impl.jdbc.struct.JDBCTableColumn;
import org.jkiss.dbeaver.model.meta.IPropertyValueListProvider;
import org.jkiss.dbeaver.model.meta.IPropertyValueTransformer;
import org.jkiss.dbeaver.model.meta.Property;
import org.jkiss.dbeaver.model.sql.SQLUtils;
import org.jkiss.dbeaver.model.struct.DBSEntity;
import org.jkiss.dbeaver.model.struct.DBSTypedObjectEx;

import java.util.Comparator;
import java.util.Set;
import java.util.TreeSet;

/**
 * PostgreAttribute
 */
public abstract class PostgreAttribute<OWNER extends DBSEntity & PostgreObject> extends JDBCTableColumn<OWNER> implements DBSTypedObjectEx, DBPNamedObject2, DBPHiddenObject
{
    private static final Log log = Log.getLog(PostgreAttribute.class);

    @NotNull
    private PostgreDataType dataType;
    private String comment;
    private long charLength;
    private int arrayDim;
    private int inheritorsCount;
    private String description;

    protected PostgreAttribute(
        OWNER table)
    {
        super(table, false);
    }

    public PostgreAttribute(
        OWNER table,
        JDBCResultSet dbResult)
        throws DBException
    {
        super(table, true);
        loadInfo(dbResult);
    }

    public PostgreDatabase getDatabase() {
        return getTable().getDatabase();
    }

    private void loadInfo(JDBCResultSet dbResult)
        throws DBException
    {
        setName(JDBCUtils.safeGetString(dbResult, "attname"));
        setOrdinalPosition(JDBCUtils.safeGetInt(dbResult, "attnum"));
        setRequired(JDBCUtils.safeGetBoolean(dbResult, "attnotnull"));
        final long typeId = JDBCUtils.safeGetLong(dbResult, "atttypid");
        dataType = getTable().getDatabase().getDataType(typeId);
        if (dataType == null) {
            throw new DBException("Attribute data type '" + typeId + "' not found");
        }
        setTypeName(dataType.getTypeName());
        setValueType(dataType.getTypeID());
        setDefaultValue(JDBCUtils.safeGetString(dbResult, "def_value"));
        int typeMod = JDBCUtils.safeGetInt(dbResult, "atttypmod");
        int maxLength = PostgreUtils.getAttributePrecision(typeId, typeMod);
        DBPDataKind dataKind = dataType.getDataKind();
        if (dataKind == DBPDataKind.NUMERIC || dataKind == DBPDataKind.DATETIME) {
            setMaxLength(0);
        } else {
            if (maxLength <= 0) {
                maxLength = PostgreUtils.getDisplaySize(typeId, typeMod);
            }
            if (maxLength >= 0) {
                setMaxLength(maxLength);
            } else {
                // TypeMod can be anything.
                // It is often used in packed format and has no numeric meaning at all
                //setMaxLength(typeMod);
            }
        }
        setPrecision(maxLength);
        setScale(PostgreUtils.getScale(typeId, typeMod));
        this.description = JDBCUtils.safeGetString(dbResult, "description");
        this.arrayDim = JDBCUtils.safeGetInt(dbResult, "attndims");
        this.inheritorsCount = JDBCUtils.safeGetInt(dbResult, "attinhcount");
        setPersisted(true);
    }

    @NotNull
    @Override
    public PostgreDataSource getDataSource()
    {
        return getTable().getDataSource();
    }

    @NotNull
    @Override
    @Property(viewable = true, editable = true, updatable = true, order = 20, listProvider = DataTypeListProvider.class, valueTransformer = DataTypeValueTransformer.class)
    public PostgreDataType getDataType() {
        return dataType;
    }

    public void setDataType(@NotNull PostgreDataType dataType) {
        this.dataType = dataType;
        setTypeName(dataType.getTypeName());
        setValueType(dataType.getTypeID());
    }

    @Override
    public DBPDataKind getDataKind() {
        return dataType.getDataKind();
    }

    @Override
    @Property(viewable = true, editable = true, updatable = true, valueRenderer = DBPositiveNumberTransformer.class, order = 21)
    public long getMaxLength()
    {
        return super.getMaxLength();
    }

    @Override
    public String getTypeName()
    {
        return dataType.getTypeName();
    }

    @Override
    @Property(viewable = true, editable = true, valueRenderer = DBPositiveNumberTransformer.class, order = 22)
    public int getPrecision()
    {
        return super.getPrecision();
    }

    @Override
    @Property(viewable = true, editable = true, valueRenderer = DBPositiveNumberTransformer.class, order = 23)
    public int getScale()
    {
        return super.getScale();
    }

    @Override
    @Property(viewable = true, editable = true, updatable = true, order = 50)
    public boolean isRequired()
    {
        return super.isRequired();
    }

    @Override
    public boolean isAutoGenerated()
    {
        final String def = getDefaultValue();
        return def != null && def.contains("nextval(");
    }

    @Override
    @Property(viewable = true, editable = true, updatable = true, order = 70)
    public String getDefaultValue()
    {
        return super.getDefaultValue();
    }

    @Nullable
    @Override
    @Property(viewable = true, editable = true, updatable = true, order = 100)
    public String getDescription() {
        return description;
    }

    public void setDescription(String description) {
        this.description = description;
    }

    @Override
    public boolean isHidden() {
        return isPersisted() && getOrdinalPosition() < 0;
    }

    public String getFullTypeName() {
        String fqtn = dataType.getTypeName();
        if (dataType.getDataKind() != DBPDataKind.CONTENT) {
            return DBUtils.getFullTypeName(this);
        }
        return fqtn;
    }

    public static class DataTypeListProvider implements IPropertyValueListProvider<PostgreAttribute> {

        @Override
        public boolean allowCustomValue()
        {
            return true;
        }

        @Override
        public Object[] getPossibleValues(PostgreAttribute column)
        {
            Set<PostgreDataType> types = new TreeSet<>(new Comparator<PostgreDataType>() {
                @Override
                public int compare(PostgreDataType o1, PostgreDataType o2) {
                    return o1.getTypeName().compareTo(o2.getTypeName());
                }
            });
            for (PostgreDataType type : column.getDataSource().getLocalDataTypes()) {
                types.add(type);
            }
            return types.toArray(new PostgreDataType[types.size()]);
        }
    }

    public static class DataTypeValueTransformer implements IPropertyValueTransformer<PostgreAttribute, Object> {
        @Override
        public PostgreDataType transform(PostgreAttribute object, Object value) {
            if (value instanceof String) {
                PostgreDataType dataType = object.getDataSource().getDefaultInstance().getDataType((String) value);
                if (dataType == null) {
                    throw new IllegalArgumentException("Bad data type name specified: " + value);
                }
                return dataType;
            } else if (value instanceof PostgreDataType) {
                return (PostgreDataType) value;
            } else {
                throw new IllegalArgumentException("Invalid type value: " + value);
            }
        }
    }
}
