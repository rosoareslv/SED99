/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2019 Serge Rider (serge@jkiss.org)
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
package org.jkiss.dbeaver.ext.postgresql.model.plan;

import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.ext.postgresql.model.PostgreDataSource;
import org.jkiss.dbeaver.model.exec.plan.DBCPlanCostNode;
import org.jkiss.dbeaver.model.exec.plan.DBCPlanNode;
import org.jkiss.dbeaver.model.impl.PropertyDescriptor;
import org.jkiss.dbeaver.model.impl.plan.AbstractExecutionPlanNode;
import org.jkiss.dbeaver.model.meta.Property;
import org.jkiss.dbeaver.model.preferences.DBPPropertyDescriptor;
import org.jkiss.dbeaver.model.preferences.DBPPropertySource;
import org.jkiss.dbeaver.model.runtime.DBRProgressMonitor;
import org.jkiss.dbeaver.model.sql.SQLUtils;
import org.jkiss.utils.CommonUtils;
import org.jkiss.utils.xml.XMLUtils;
import org.w3c.dom.Element;
import org.w3c.dom.Node;

import java.util.*;

/**
 * Postgre execution plan node
 */
public abstract class PostgrePlanNodeBase<NODE extends PostgrePlanNodeBase> extends AbstractExecutionPlanNode implements DBCPlanCostNode, DBPPropertySource {

    public static final String ATTR_NODE_TYPE = "Node-Type";
    public static final String ATTR_RELATION_NAME = "Relation-Name";
    public static final String ATTR_FUNCTION_NAME = "Function-Name";
    public static final String ATTR_ALIAS = "Alias";
    public static final String ATTR_TOTAL_COST = "Total-Cost";
    public static final String ATTR_STARTUP_COST = "Startup-Cost";
    public static final String ATTR_INDEX_NAME = "Index-Name";
    public static final String ATTR_ACTUAL_TOTAL_TIME = "Actual-Total-Time";
    public static final String ATTR_ACTUAL_ROWS = "Actual-Rows";
    public static final String ATTR_PLAN_ROWS = "Plan-Rows";
    public static final String ATTR_FILTER = "Filter";

    private PostgreDataSource dataSource;
    protected NODE parent;
    protected final List<NODE> nested = new ArrayList<>();

    protected String nodeType;
    private String entity;
    private String cost;
    protected Map<String, String> attributes = Collections.emptyMap();

    protected PostgrePlanNodeBase(PostgreDataSource dataSource, NODE parent) {
        this.parent = parent;
        this.dataSource = dataSource;

    }

    protected void setAttributes(Map<String, String> attributes) {
        this.attributes = attributes;
        nodeType = attributes.remove(ATTR_NODE_TYPE);
        entity = attributes.get(ATTR_RELATION_NAME);
        if (entity == null) {
            entity = attributes.get(ATTR_FUNCTION_NAME);
        }
        if (entity == null) {
            entity = attributes.get(ATTR_INDEX_NAME);
        }
        String startCost = attributes.get(ATTR_STARTUP_COST);
        String totalCost = attributes.get(ATTR_TOTAL_COST);
        cost = startCost + " - " + totalCost;
    }

    @Override
    public String getNodeName() {
        return entity;
    }

    @Override
    @Property(order = 0, viewable = true)
    public String getNodeType() {
        return nodeType;
    }

    @Override
    public String getNodeDescription() {
        return attributes.get(ATTR_FILTER);
    }

    @Property(order = 2, viewable = true)
    public String getEntity() {
        return entity;
    }

    @Property(order = 3, viewable = true)
    public String getCost() {
        return cost;
    }

    /*
    @Property(order = 20, viewable = true)
    public String getPlanRows() {
        return attributes.get("Plan-Rows");
    }
     */

    @Property(order = 21, viewable = true)
    public String getActualRows() {
        String rows = attributes.get(ATTR_ACTUAL_ROWS);
        if (rows == null) {
            rows = attributes.get(ATTR_PLAN_ROWS);
        }
        return rows;
    }

    @Property(order = 22, viewable = true)
    public String getTotalTime() {
        return attributes.get(ATTR_ACTUAL_TOTAL_TIME);
    }

    @Property(order = 23, viewable = true)
    public String getNodeCondition() {
        String cond = attributes.get("Index-Cond");
        if (cond == null) {
            cond = attributes.get("Hash-Cond");
        }
        if (cond == null) {
            cond = attributes.get(ATTR_FILTER);
        }
        if (!CommonUtils.isEmpty(cond)) {
            cond = SQLUtils.formatSQL(dataSource, cond);
        }
        return cond;
    }

    @Override
    public NODE getParent()
    {
        return parent;
    }

    @Override
    public List<NODE> getNested()
    {
        return nested;
    }

    @Override
    public Number getNodeCost() {
        String totalCost = attributes.get(ATTR_TOTAL_COST);
        return totalCost == null ? null : CommonUtils.toDouble(totalCost);
    }

    @Override
    public Number getNodePercent() {
        return null;
        //        String costPercent = attributes.get(ATTR_TOTAL_COST);
        //        return costPercent == null ? null : CommonUtils.toDouble(costPercent);
    }

    @Override
    public Number getNodeDuration() {
        String time = attributes.get(ATTR_ACTUAL_TOTAL_TIME);
        return time == null ? null : CommonUtils.toDouble(time);
    }

    @Override
    public Number getNodeRowCount() {
        String rows = attributes.get(ATTR_ACTUAL_ROWS);
        return rows == null ? null : CommonUtils.toLong(rows);
    }

    @Override
    public String toString() {
        StringBuilder title = new StringBuilder();
        title.append("Type: ").append(nodeType);
        String joinType = attributes.get("Join-Type");
        if (!CommonUtils.isEmpty(joinType)) {
            title.append(" (").append(joinType).append(")");
        }
        title.append("; ");
        if (!CommonUtils.isEmpty(entity)) {
            title.append("Rel: ").append(entity).append(" ");
        }
        title.append("; Cost: ").append(cost);

        return title.toString();
    }

    //////////////////////////////////////////////////////////
    // Properties

    @Override
    public Object getEditableValue() {
        return this;
    }

    @Override
    public DBPPropertyDescriptor[] getPropertyDescriptors2() {
        DBPPropertyDescriptor[] props = new DBPPropertyDescriptor[attributes.size()];
        int index = 0;
        for (Map.Entry<String, String> attr : attributes.entrySet()) {
            props[index++] = new PropertyDescriptor(
                    "Details",
                    attr.getKey(),
                    attr.getKey(),
                    null,
                    String.class,
                    false,
                    null,
                    null,
                    false);
        }
        return props;
    }

    @Override
    public Object getPropertyValue(@Nullable DBRProgressMonitor monitor, Object id) {
        return attributes.get(id.toString());
    }

    @Override
    public boolean isPropertySet(Object id) {
        return false;//attributes.containsKey(id.toString());
    }

    @Override
    public boolean isPropertyResettable(Object id) {
        return false;
    }

    @Override
    public void resetPropertyValue(@Nullable DBRProgressMonitor monitor, Object id) {

    }

    @Override
    public void resetPropertyValueToDefault(Object id) {

    }

    @Override
    public void setPropertyValue(@Nullable DBRProgressMonitor monitor, Object id, Object value) {

    }

    @Override
    public boolean isDirty(Object id) {
        return false;
    }

}
