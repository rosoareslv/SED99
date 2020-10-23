/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2017 Serge Rider (serge@jkiss.org)
 * Copyright (C) 2011-2012 Eugene Fradkin (eugene.fradkin@gmail.com)
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
package org.jkiss.dbeaver.ui.preferences;

import org.eclipse.jface.fieldassist.SimpleContentProposalProvider;
import org.eclipse.jface.fieldassist.TextContentAdapter;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.*;
import org.jkiss.dbeaver.DBeaverPreferences;
import org.jkiss.dbeaver.core.CoreMessages;
import org.jkiss.dbeaver.model.DBPDataSourceContainer;
import org.jkiss.dbeaver.model.preferences.DBPPreferenceStore;
import org.jkiss.dbeaver.ui.UIUtils;
import org.jkiss.dbeaver.ui.editors.sql.SQLEditor;
import org.jkiss.dbeaver.ui.editors.sql.SQLPreferenceConstants;
import org.jkiss.dbeaver.utils.GeneralUtils;
import org.jkiss.dbeaver.utils.PrefUtils;

/**
 * PrefPageSQLEditor
 */
public class PrefPageSQLEditor extends TargetPrefPage
{
    public static final String PAGE_ID = "org.jkiss.dbeaver.preferences.main.sqleditor"; //$NON-NLS-1$

    private Button editorSeparateConnectionCheck;
    private Button connectOnActivationCheck;
    private Button connectOnExecuteCheck;

    private Button saveOnQueryExecution;
    private Button autoSaveOnClose;

    private Button deleteEmptyCheck;
    private Button autoFoldersCheck;
    private Text scriptTitlePattern;

    public PrefPageSQLEditor()
    {
        super();
    }

    @Override
    protected boolean hasDataSourceSpecificOptions(DBPDataSourceContainer dataSourceDescriptor)
    {
        DBPPreferenceStore store = dataSourceDescriptor.getPreferenceStore();
        return
            store.contains(DBeaverPreferences.EDITOR_SEPARATE_CONNECTION) ||
            store.contains(DBeaverPreferences.EDITOR_CONNECT_ON_ACTIVATE) ||
            store.contains(DBeaverPreferences.EDITOR_CONNECT_ON_EXECUTE) ||
    
            store.contains(SQLPreferenceConstants.AUTO_SAVE_ON_CLOSE) ||
            store.contains(SQLPreferenceConstants.AUTO_SAVE_ON_EXECUTE) ||

            store.contains(DBeaverPreferences.SCRIPT_DELETE_EMPTY) ||
            store.contains(DBeaverPreferences.SCRIPT_AUTO_FOLDERS)
        ;
    }

    @Override
    protected boolean supportsDataSourceSpecificOptions()
    {
        return true;
    }

    @Override
    protected Control createPreferenceContent(Composite parent)
    {
        Composite composite = UIUtils.createPlaceholder(parent, 1);

        {
            Group connectionsGroup = UIUtils.createControlGroup(composite, "Connections", 1, GridData.VERTICAL_ALIGN_BEGINNING | GridData.FILL_HORIZONTAL, 0);
            editorSeparateConnectionCheck = UIUtils.createCheckbox(connectionsGroup, "Open separate connection for each editor", false);

            connectOnActivationCheck = UIUtils.createCheckbox(connectionsGroup, "Connect on editor activation", false);
            connectOnExecuteCheck = UIUtils.createCheckbox(connectionsGroup, "Connect on query execute", false);
        }

        {
            Group connectionsGroup = UIUtils.createControlGroup(composite, "Auto-save", 1, GridData.FILL_HORIZONTAL, 0);
            autoSaveOnClose = UIUtils.createCheckbox(connectionsGroup, "Auto-save editor on close", false);
            saveOnQueryExecution = UIUtils.createCheckbox(connectionsGroup, "Save editor on query execute", false);
        }

        // Scripts
        {
            Composite scriptsGroup = UIUtils.createControlGroup(composite, CoreMessages.pref_page_sql_editor_group_resources, 2, GridData.FILL_HORIZONTAL, 0);
            ((GridData)scriptsGroup.getLayoutData()).horizontalSpan = 2;

            deleteEmptyCheck = UIUtils.createCheckbox(scriptsGroup, CoreMessages.pref_page_sql_editor_checkbox_delete_empty_scripts, null, false, 2);
            autoFoldersCheck = UIUtils.createCheckbox(scriptsGroup, CoreMessages.pref_page_sql_editor_checkbox_put_new_scripts, null, false, 2);
            scriptTitlePattern = UIUtils.createLabelText(scriptsGroup, CoreMessages.pref_page_sql_editor_title_pattern, "");
            UIUtils.installContentProposal(
                    scriptTitlePattern,
                    new TextContentAdapter(),
                    new SimpleContentProposalProvider(new String[] {
                            GeneralUtils.variablePattern(SQLEditor.VAR_CONNECTION_NAME),
                            GeneralUtils.variablePattern(SQLEditor.VAR_DRIVER_NAME),
                            GeneralUtils.variablePattern(SQLEditor.VAR_FILE_NAME),
                            GeneralUtils.variablePattern(SQLEditor.VAR_FILE_EXT)
                    }));
            UIUtils.setContentProposalToolTip(scriptTitlePattern, "Output file name patterns",
                    SQLEditor.VAR_CONNECTION_NAME, SQLEditor.VAR_DRIVER_NAME, SQLEditor.VAR_FILE_NAME, SQLEditor.VAR_FILE_EXT);
        }

        return composite;
    }

    @Override
    protected void loadPreferences(DBPPreferenceStore store)
    {
        try {
            editorSeparateConnectionCheck.setSelection(store.getBoolean(DBeaverPreferences.EDITOR_SEPARATE_CONNECTION));
            connectOnActivationCheck.setSelection(store.getBoolean(DBeaverPreferences.EDITOR_CONNECT_ON_ACTIVATE));
            connectOnExecuteCheck.setSelection(store.getBoolean(DBeaverPreferences.EDITOR_CONNECT_ON_EXECUTE));

            autoSaveOnClose.setSelection(store.getBoolean(SQLPreferenceConstants.AUTO_SAVE_ON_CLOSE));
            saveOnQueryExecution.setSelection(store.getBoolean(SQLPreferenceConstants.AUTO_SAVE_ON_EXECUTE));

            deleteEmptyCheck.setSelection(store.getBoolean(DBeaverPreferences.SCRIPT_DELETE_EMPTY));
            autoFoldersCheck.setSelection(store.getBoolean(DBeaverPreferences.SCRIPT_AUTO_FOLDERS));
            scriptTitlePattern.setText(store.getString(DBeaverPreferences.SCRIPT_TITLE_PATTERN));

        } catch (Exception e) {
            log.warn(e);
        }
    }

    @Override
    protected void savePreferences(DBPPreferenceStore store)
    {
        try {
            store.setValue(DBeaverPreferences.EDITOR_SEPARATE_CONNECTION, editorSeparateConnectionCheck.getSelection());
            store.setValue(DBeaverPreferences.EDITOR_CONNECT_ON_ACTIVATE, connectOnActivationCheck.getSelection());
            store.setValue(DBeaverPreferences.EDITOR_CONNECT_ON_EXECUTE, connectOnExecuteCheck.getSelection());

            store.setValue(SQLPreferenceConstants.AUTO_SAVE_ON_CLOSE, autoSaveOnClose.getSelection());
            store.setValue(SQLPreferenceConstants.AUTO_SAVE_ON_EXECUTE, saveOnQueryExecution.getSelection());

            store.setValue(DBeaverPreferences.SCRIPT_DELETE_EMPTY, deleteEmptyCheck.getSelection());
            store.setValue(DBeaverPreferences.SCRIPT_AUTO_FOLDERS, autoFoldersCheck.getSelection());
            store.setValue(DBeaverPreferences.SCRIPT_TITLE_PATTERN, scriptTitlePattern.getText());
        } catch (Exception e) {
            log.warn(e);
        }
        PrefUtils.savePreferenceStore(store);
    }

    @Override
    protected void clearPreferences(DBPPreferenceStore store)
    {
        store.setToDefault(DBeaverPreferences.EDITOR_SEPARATE_CONNECTION);
        store.setToDefault(DBeaverPreferences.EDITOR_CONNECT_ON_ACTIVATE);
        store.setToDefault(DBeaverPreferences.EDITOR_CONNECT_ON_EXECUTE);

        store.setToDefault(SQLPreferenceConstants.AUTO_SAVE_ON_CLOSE);
        store.setToDefault(SQLPreferenceConstants.AUTO_SAVE_ON_EXECUTE);

        store.setToDefault(DBeaverPreferences.SCRIPT_DELETE_EMPTY);
        store.setToDefault(DBeaverPreferences.SCRIPT_AUTO_FOLDERS);
        store.setToDefault(DBeaverPreferences.SCRIPT_TITLE_PATTERN);
    }

    @Override
    protected String getPropertyPageID()
    {
        return PAGE_ID;
    }

}