/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2016 Serge Rieder (serge@jkiss.org)
 * Copyright (C) 2011-2012 Eugene Fradkin (eugene.fradkin@gmail.com)
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
package org.jkiss.dbeaver.ext.oracle.edit;

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.DBException;
import org.jkiss.dbeaver.core.DBeaverUI;
import org.jkiss.dbeaver.model.edit.DBEPersistAction;
import org.jkiss.dbeaver.ext.oracle.model.OracleDataSource;
import org.jkiss.dbeaver.ext.oracle.model.OracleSchema;
import org.jkiss.dbeaver.ext.oracle.model.OracleUser;
import org.jkiss.dbeaver.model.DBUtils;
import org.jkiss.dbeaver.model.edit.DBECommandContext;
import org.jkiss.dbeaver.model.edit.DBEObjectRenamer;
import org.jkiss.dbeaver.model.impl.DBObjectNameCaseTransformer;
import org.jkiss.dbeaver.model.impl.DBSObjectCache;
import org.jkiss.dbeaver.model.impl.edit.SQLDatabasePersistAction;
import org.jkiss.dbeaver.model.impl.sql.edit.SQLObjectEditor;
import org.jkiss.dbeaver.model.runtime.DBRProgressMonitor;
import org.jkiss.dbeaver.model.struct.DBSObject;
import org.jkiss.dbeaver.ui.UITask;
import org.jkiss.dbeaver.ui.UIUtils;

import java.util.List;

/**
 * OracleSchemaManager
 */
public class OracleSchemaManager extends SQLObjectEditor<OracleSchema, OracleDataSource> implements DBEObjectRenamer<OracleSchema> {

    @Override
    public long getMakerOptions()
    {
        return FEATURE_SAVE_IMMEDIATELY;
    }

    @Nullable
    @Override
    public DBSObjectCache<? extends DBSObject, OracleSchema> getObjectsCache(OracleSchema object)
    {
        return object.getDataSource().schemaCache;
    }

    @Override
    protected OracleSchema createDatabaseObject(DBRProgressMonitor monitor, DBECommandContext context, final OracleDataSource parent, Object copyFrom)
    {
        return new UITask<OracleSchema>() {
            @Override
            protected OracleSchema runTask() {
                NewUserDialog dialog = new NewUserDialog(DBeaverUI.getActiveWorkbenchShell(), parent);
                if (dialog.open() != IDialogConstants.OK_ID) {
                    return null;
                }
                OracleSchema newSchema = new OracleSchema(parent, -1, dialog.getUser().getName());
                newSchema.setUser(dialog.getUser());

                return newSchema;
            }
        }.execute();
    }

    @Override
    protected void addObjectCreateActions(List<DBEPersistAction> actions, ObjectCreateCommand command)
    {
        OracleUser user = command.getObject().getUser();
        actions.add(
            new SQLDatabasePersistAction("Create schema",
                "CREATE USER " + DBUtils.getQuotedIdentifier(user) + " IDENTIFIED BY \"" + user.getPassword() + "\"")
        );
    }

    @Override
    protected void addObjectDeleteActions(List<DBEPersistAction> actions, ObjectDeleteCommand command)
    {
        actions.add(
            new SQLDatabasePersistAction("Drop schema",
                "DROP USER " + DBUtils.getQuotedIdentifier(command.getObject()) + " CASCADE") //$NON-NLS-2$
        );
    }

    @Override
    public void renameObject(DBECommandContext commandContext, OracleSchema schema, String newName) throws DBException
    {
        throw new DBException("Direct database rename is not yet implemented in Oracle. You should use export/import functions for that.");
    }

    static class NewUserDialog extends Dialog {

        private OracleUser user;
        private Text nameText;
        private Text passwordText;

        public NewUserDialog(Shell parentShell, OracleDataSource dataSource)
        {
            super(parentShell);
            this.user = new OracleUser(dataSource);
        }

        public OracleUser getUser()
        {
            return user;
        }

        @Override
        protected boolean isResizable()
        {
            return true;
        }

        @Override
        protected Control createDialogArea(Composite parent)
        {
            getShell().setText("Set schema/user properties");
            Control container = super.createDialogArea(parent);
            Composite composite = UIUtils.createPlaceholder((Composite) container, 2);
            composite.setLayoutData(new GridData(GridData.FILL_BOTH));

            nameText = UIUtils.createLabelText(composite, "Schema/User Name", null);
            nameText.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
            passwordText = UIUtils.createLabelText(composite, "User Password", null, SWT.BORDER | SWT.PASSWORD);
            passwordText.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

            return parent;
        }

        @Override
        protected void okPressed()
        {
            user.setName(DBObjectNameCaseTransformer.transformObjectName(user, nameText.getText()));
            user.setPassword(passwordText.getText());
            super.okPressed();
        }

    }

}

