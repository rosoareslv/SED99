/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2018 Serge Rider (serge@jkiss.org)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.jkiss.dbeaver.tools.transfer.stream;

import org.jkiss.dbeaver.model.DBPDataKind;
import org.jkiss.dbeaver.model.data.DBDValueMeta;
import org.jkiss.dbeaver.model.exec.*;
import org.jkiss.dbeaver.model.impl.local.LocalResultSetColumn;
import org.jkiss.dbeaver.model.impl.local.LocalResultSetMeta;

import java.util.ArrayList;
import java.util.List;

/**
 * Stream session
 */
public class StreamTransferResultSet implements DBCResultSet {

    private final StreamTransferSession session;
    private final DBCStatement statement;
    private StreamProducerSettings.EntityMapping entityMapping;
    private List<DBCAttributeMetaData> metaAttrs;
    private Object[] streamRow;
    private final List<StreamProducerSettings.AttributeMapping> attributeMappings;

    public StreamTransferResultSet(StreamTransferSession session, DBCStatement statement, StreamProducerSettings.EntityMapping entityMapping) {
        this.session = session;
        this.statement = statement;
        this.entityMapping = entityMapping;
        this.attributeMappings = this.entityMapping.getValuableAttributeMappings();
        this.metaAttrs = new ArrayList<>(attributeMappings.size());
        for (int i = 0; i < attributeMappings.size(); i++) {
            StreamProducerSettings.AttributeMapping attr = attributeMappings.get(i);
            metaAttrs.add(new LocalResultSetColumn(this, i, attr.getTargetAttributeName(), DBPDataKind.STRING));
        }
    }

    public void setStreamRow(Object[] streamRow) {
        for (int i = 0; i < attributeMappings.size(); i++) {
            StreamProducerSettings.AttributeMapping attr = attributeMappings.get(i);
            if (attr.getMappingType() == StreamProducerSettings.AttributeMapping.MappingType.DEFAULT_VALUE) {
                streamRow[attr.getSourceColumn().getColumnIndex()] = attr.getDefaultValue();
            }
        }
        this.streamRow = streamRow;
    }

    @Override
    public DBCSession getSession() {
        return session;
    }

    @Override
    public DBCStatement getSourceStatement() {
        return statement;
    }

    @Override
    public Object getAttributeValue(int index) throws DBCException {
        StreamProducerSettings.AttributeMapping attr = this.attributeMappings.get(index);
        StreamDataImporterColumnInfo sourceColumn = attr.getSourceColumn();
        if (sourceColumn != null) {
            return streamRow[sourceColumn.getColumnIndex()];
        } else {
            return null;
        }
    }

    @Override
    public Object getAttributeValue(String name) throws DBCException {
        return null;
    }

    @Override
    public DBDValueMeta getAttributeValueMeta(int index) throws DBCException {
        return null;
    }

    @Override
    public DBDValueMeta getRowMeta() throws DBCException {
        return null;
    }

    @Override
    public boolean nextRow() throws DBCException {
        return false;
    }

    @Override
    public boolean moveTo(int position) throws DBCException {
        return false;
    }

    @Override
    public DBCResultSetMetaData getMeta() throws DBCException {
        return new LocalResultSetMeta(metaAttrs);
    }

    @Override
    public String getResultSetName() throws DBCException {
        return null;
    }

    @Override
    public void close() {

    }

}
