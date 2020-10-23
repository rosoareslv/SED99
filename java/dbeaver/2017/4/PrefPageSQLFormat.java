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
import org.eclipse.jface.text.source.ISourceViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.*;
import org.eclipse.ui.IEditorSite;
import org.jkiss.dbeaver.ModelPreferences;
import org.jkiss.dbeaver.core.DBeaverUI;
import org.jkiss.dbeaver.model.DBPDataSource;
import org.jkiss.dbeaver.model.DBPDataSourceContainer;
import org.jkiss.dbeaver.model.DBPIdentifierCase;
import org.jkiss.dbeaver.model.exec.DBCExecutionContext;
import org.jkiss.dbeaver.model.preferences.DBPPreferenceStore;
import org.jkiss.dbeaver.model.sql.format.external.SQLExternalFormatter;
import org.jkiss.dbeaver.model.sql.format.tokenized.SQLTokenizedFormatter;
import org.jkiss.dbeaver.ui.UIUtils;
import org.jkiss.dbeaver.ui.editors.StringEditorInput;
import org.jkiss.dbeaver.ui.editors.SubEditorSite;
import org.jkiss.dbeaver.ui.editors.sql.SQLEditorBase;
import org.jkiss.dbeaver.ui.editors.sql.SQLPreferenceConstants;
import org.jkiss.dbeaver.utils.ContentUtils;
import org.jkiss.dbeaver.utils.GeneralUtils;
import org.jkiss.dbeaver.utils.PrefUtils;
import org.jkiss.utils.CommonUtils;

import java.io.InputStream;
import java.util.Locale;

/**
 * PrefPageSQLFormat
 */
public class PrefPageSQLFormat extends TargetPrefPage
{
    public static final String PAGE_ID = "org.jkiss.dbeaver.preferences.main.sql.format"; //$NON-NLS-1$

    private final static String FORMAT_FILE_NAME = "format_preview.sql";

    // Auto-close
    private Button acSingleQuotesCheck;
    private Button acDoubleQuotesCheck;
    private Button acBracketsCheck;
    // Auto-Format
    private Button afKeywordCase;
    private Button afExtractFromSource;

    // Formatter
    private Combo formatterSelector;
    private Combo keywordCaseCombo;

    private Text externalCmdText;
    private Button externalUseFile;
    private Spinner externalTimeout;

    private SQLEditorBase sqlViewer;
    private Composite defaultGroup;
    private Composite externalGroup;

    public PrefPageSQLFormat()
    {
        super();
    }

    @Override
    protected boolean hasDataSourceSpecificOptions(DBPDataSourceContainer dataSourceDescriptor)
    {
        DBPPreferenceStore store = dataSourceDescriptor.getPreferenceStore();
        return
            store.contains(SQLPreferenceConstants.SQLEDITOR_CLOSE_SINGLE_QUOTES) ||
            store.contains(SQLPreferenceConstants.SQLEDITOR_CLOSE_DOUBLE_QUOTES) ||
            store.contains(SQLPreferenceConstants.SQLEDITOR_CLOSE_BRACKETS) ||
            store.contains(SQLPreferenceConstants.SQL_FORMAT_KEYWORD_CASE_AUTO) ||
            store.contains(SQLPreferenceConstants.SQL_FORMAT_EXTRACT_FROM_SOURCE) ||

            store.contains(ModelPreferences.SQL_FORMAT_FORMATTER) ||
            store.contains(ModelPreferences.SQL_FORMAT_KEYWORD_CASE) ||
            store.contains(ModelPreferences.SQL_FORMAT_EXTERNAL_CMD) ||
            store.contains(ModelPreferences.SQL_FORMAT_EXTERNAL_FILE) ||
            store.contains(ModelPreferences.SQL_FORMAT_EXTERNAL_TIMEOUT)
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
        Composite composite = UIUtils.createPlaceholder(parent, 2, 5);

        // Autoclose
        {
            Composite acGroup = UIUtils.createControlGroup(composite, "Auto close", 1, GridData.FILL_HORIZONTAL | GridData.VERTICAL_ALIGN_BEGINNING, 0);

            acSingleQuotesCheck = UIUtils.createCheckbox(acGroup, "Single quotes", false);
            acDoubleQuotesCheck = UIUtils.createCheckbox(acGroup, "Double quotes", false);
            acBracketsCheck = UIUtils.createCheckbox(acGroup, "Brackets", false);
        }

        {
            // Formatting
            Composite afGroup = UIUtils.createControlGroup(composite, "Auto format", 1, GridData.FILL_HORIZONTAL | GridData.VERTICAL_ALIGN_BEGINNING, 0);
            afKeywordCase = UIUtils.createCheckbox(
                afGroup,
                "Convert keyword case",
                "Auto-convert keywords to upper/lower case on enter",
                false, 1);
            afExtractFromSource = UIUtils.createCheckbox(
                afGroup,
                "Extract SQL from source code",
                "On source code paste will remove all source language elements like quotes, +, \\n, etc", false, 1);
        }

        Composite formatterGroup = UIUtils.createControlGroup(composite, "Formatter", 1, GridData.FILL_BOTH, 0);
        ((GridData)formatterGroup.getLayoutData()).horizontalSpan = 2;

        {
            Composite formatterPanel = UIUtils.createPlaceholder(formatterGroup, 2);
            formatterPanel.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

            formatterSelector = UIUtils.createLabelCombo(formatterPanel, "Formatter", SWT.DROP_DOWN | SWT.READ_ONLY);
            formatterSelector.add(capitalizeCaseName(SQLTokenizedFormatter.FORMATTER_ID));
            formatterSelector.add(capitalizeCaseName(SQLExternalFormatter.FORMATTER_ID));
            formatterSelector.addSelectionListener(new SelectionAdapter() {
                @Override
                public void widgetSelected(SelectionEvent e) {
                    showFormatterSettings();
                }
            });
            formatterSelector.setLayoutData(new GridData(GridData.HORIZONTAL_ALIGN_BEGINNING));
        }

        // Default formatter settings
        {
            defaultGroup = UIUtils.createPlaceholder(formatterGroup, 2, 0);
            defaultGroup.setLayoutData(new GridData(GridData.HORIZONTAL_ALIGN_BEGINNING));
            keywordCaseCombo = UIUtils.createLabelCombo(defaultGroup, "Keyword case", SWT.DROP_DOWN | SWT.READ_ONLY);
            keywordCaseCombo.setLayoutData(new GridData(GridData.HORIZONTAL_ALIGN_BEGINNING));
            keywordCaseCombo.add("Database");
            for (DBPIdentifierCase c :DBPIdentifierCase.values()) {
                keywordCaseCombo.add(capitalizeCaseName(c.name()));
            }
            keywordCaseCombo.addSelectionListener(new SelectionAdapter() {
                @Override
                public void widgetSelected(SelectionEvent e) {
                    performApply();
                }
            });
        }

        // External formatter
        {
            externalGroup = UIUtils.createPlaceholder(formatterGroup, 2, 5);
            externalGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL | GridData.HORIZONTAL_ALIGN_BEGINNING));

            externalCmdText = UIUtils.createLabelText(externalGroup, "Command line", "");
            externalCmdText.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
            UIUtils.installContentProposal(
                    externalCmdText,
                    new TextContentAdapter(),
                    new SimpleContentProposalProvider(new String[] {
                            GeneralUtils.variablePattern(SQLExternalFormatter.VAR_FILE)
                    }));
            UIUtils.setContentProposalToolTip(externalCmdText, "External program with parameters", SQLExternalFormatter.VAR_FILE);

            externalUseFile = UIUtils.createLabelCheckbox(externalGroup,
                "Use temp file",
                "Use temporary file to pass SQL text.\nTo pass file name in command line use parameter " + GeneralUtils.variablePattern(SQLExternalFormatter.VAR_FILE),
                false);
            externalTimeout = UIUtils.createLabelSpinner(externalGroup,
                "Exec timeout",
                "Time to wait until formatter process finish (ms)",
                100, 100, 10000);
        }

        {
            // SQL preview
            Composite previewGroup = new Composite(formatterGroup, SWT.BORDER);
            previewGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
            previewGroup.setLayout(new FillLayout());

            sqlViewer = new SQLEditorBase() {
                @Override
                public DBCExecutionContext getExecutionContext() {
                    final DBPDataSourceContainer container = getDataSourceContainer();
                    if (container != null) {
                        final DBPDataSource dataSource = container.getDataSource();
                        if (dataSource != null) {
                            return dataSource.getDefaultContext(false);
                        }
                    }
                    return null;
                }
            };
            try {
                try (final InputStream sqlStream = getClass().getResourceAsStream(FORMAT_FILE_NAME)) {
                    final String sqlText = ContentUtils.readToString(sqlStream, GeneralUtils.DEFAULT_ENCODING);
                    IEditorSite subSite = new SubEditorSite(DBeaverUI.getActiveWorkbenchWindow().getActivePage().getActivePart().getSite());
                    StringEditorInput sqlInput = new StringEditorInput("SQL preview", sqlText, true, GeneralUtils.getDefaultFileEncoding());
                    sqlViewer.init(subSite, sqlInput);
                }
            } catch (Exception e) {
                log.error(e);
            }

            sqlViewer.createPartControl(previewGroup);
            Object text = sqlViewer.getAdapter(Control.class);
            if (text instanceof StyledText) {
                ((StyledText) text).setWordWrap(true);
            }
            sqlViewer.reloadSyntaxRules();

            previewGroup.addDisposeListener(new DisposeListener() {
                @Override
                public void widgetDisposed(DisposeEvent e) {
                    sqlViewer.dispose();
                }
            });
        }

        return composite;
    }

    @Override
    protected void loadPreferences(DBPPreferenceStore store)
    {
        acSingleQuotesCheck.setSelection(store.getBoolean(SQLPreferenceConstants.SQLEDITOR_CLOSE_SINGLE_QUOTES));
        acDoubleQuotesCheck.setSelection(store.getBoolean(SQLPreferenceConstants.SQLEDITOR_CLOSE_DOUBLE_QUOTES));
        acBracketsCheck.setSelection(store.getBoolean(SQLPreferenceConstants.SQLEDITOR_CLOSE_BRACKETS));
        afKeywordCase.setSelection(store.getBoolean(SQLPreferenceConstants.SQL_FORMAT_KEYWORD_CASE_AUTO));
        afExtractFromSource.setSelection(store.getBoolean(SQLPreferenceConstants.SQL_FORMAT_EXTRACT_FROM_SOURCE));


        UIUtils.setComboSelection(formatterSelector, capitalizeCaseName(store.getString(ModelPreferences.SQL_FORMAT_FORMATTER)));
        final String caseName = store.getString(ModelPreferences.SQL_FORMAT_KEYWORD_CASE);
        if (CommonUtils.isEmpty(caseName)) {
            keywordCaseCombo.select(0);
        } else {
            UIUtils.setComboSelection(keywordCaseCombo, capitalizeCaseName(caseName));
        }

        externalCmdText.setText(store.getString(ModelPreferences.SQL_FORMAT_EXTERNAL_CMD));
        externalUseFile.setSelection(store.getBoolean(ModelPreferences.SQL_FORMAT_EXTERNAL_FILE));
        externalTimeout.setSelection(store.getInt(ModelPreferences.SQL_FORMAT_EXTERNAL_TIMEOUT));

        formatSQL();
        showFormatterSettings();
    }

    @Override
    protected void savePreferences(DBPPreferenceStore store)
    {
        store.setValue(SQLPreferenceConstants.SQLEDITOR_CLOSE_SINGLE_QUOTES, acSingleQuotesCheck.getSelection());
        store.setValue(SQLPreferenceConstants.SQLEDITOR_CLOSE_DOUBLE_QUOTES, acDoubleQuotesCheck.getSelection());
        store.setValue(SQLPreferenceConstants.SQLEDITOR_CLOSE_BRACKETS, acBracketsCheck.getSelection());

        store.setValue(SQLPreferenceConstants.SQL_FORMAT_KEYWORD_CASE_AUTO, afKeywordCase.getSelection());
        store.setValue(SQLPreferenceConstants.SQL_FORMAT_EXTRACT_FROM_SOURCE, afExtractFromSource.getSelection());

        store.setValue(ModelPreferences.SQL_FORMAT_FORMATTER, formatterSelector.getText().toUpperCase(Locale.ENGLISH));

        final String caseName;
        if (keywordCaseCombo.getSelectionIndex() == 0) {
            caseName = "";
        } else {
            caseName = keywordCaseCombo.getText().toUpperCase(Locale.ENGLISH);
        }
        store.setValue(ModelPreferences.SQL_FORMAT_KEYWORD_CASE, caseName);

        store.setValue(ModelPreferences.SQL_FORMAT_EXTERNAL_CMD, externalCmdText.getText());
        store.setValue(ModelPreferences.SQL_FORMAT_EXTERNAL_FILE, externalUseFile.getSelection());
        store.setValue(ModelPreferences.SQL_FORMAT_EXTERNAL_TIMEOUT, externalTimeout.getSelection());

        PrefUtils.savePreferenceStore(store);
    }

    @Override
    protected void clearPreferences(DBPPreferenceStore store)
    {
        store.setToDefault(SQLPreferenceConstants.SQLEDITOR_CLOSE_SINGLE_QUOTES);
        store.setToDefault(SQLPreferenceConstants.SQLEDITOR_CLOSE_DOUBLE_QUOTES);
        store.setToDefault(SQLPreferenceConstants.SQLEDITOR_CLOSE_BRACKETS);
        store.setToDefault(SQLPreferenceConstants.SQL_FORMAT_KEYWORD_CASE_AUTO);
        store.setToDefault(SQLPreferenceConstants.SQL_FORMAT_EXTRACT_FROM_SOURCE);

        store.setToDefault(ModelPreferences.SQL_FORMAT_FORMATTER);
        store.setToDefault(ModelPreferences.SQL_FORMAT_KEYWORD_CASE);
        store.setToDefault(ModelPreferences.SQL_FORMAT_EXTERNAL_CMD);
    }

    @Override
    protected void performApply() {
        super.performApply();
        formatSQL();
    }

    @Override
    protected String getPropertyPageID()
    {
        return PAGE_ID;
    }

    private void showFormatterSettings() {
        final boolean isDefFormatter = formatterSelector.getSelectionIndex() == 0;
        defaultGroup.setVisible(isDefFormatter);
        externalGroup.setVisible(!isDefFormatter);
        ((GridData)defaultGroup.getLayoutData()).exclude = !isDefFormatter;
        ((GridData)externalGroup.getLayoutData()).exclude = isDefFormatter;
        defaultGroup.getParent().layout();
    }

    private static String capitalizeCaseName(String name) {
        return CommonUtils.capitalizeWord(name.toLowerCase(Locale.ENGLISH));
    }

    private void formatSQL() {
        try {
            try (final InputStream sqlStream = getClass().getResourceAsStream(FORMAT_FILE_NAME)) {
                final String sqlText = ContentUtils.readToString(sqlStream, GeneralUtils.DEFAULT_ENCODING);
                sqlViewer.setInput(new StringEditorInput("SQL preview", sqlText, true, GeneralUtils.getDefaultFileEncoding()));
            }
        } catch (Exception e) {
            log.error(e);
        }
        sqlViewer.getTextViewer().doOperation(ISourceViewer.FORMAT);
        sqlViewer.reloadSyntaxRules();
    }

}