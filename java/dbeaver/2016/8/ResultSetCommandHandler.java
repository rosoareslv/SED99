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
package org.jkiss.dbeaver.ui.controls.resultset;

import org.eclipse.core.commands.AbstractHandler;
import org.eclipse.core.commands.ExecutionEvent;
import org.eclipse.core.commands.ExecutionException;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.dialogs.IDialogSettings;
import org.eclipse.jface.dialogs.IInputValidator;
import org.eclipse.jface.dialogs.InputDialog;
import org.eclipse.jface.text.IFindReplaceTarget;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IWorkbenchCommandConstants;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.handlers.HandlerUtil;
import org.eclipse.ui.texteditor.FindReplaceAction;
import org.eclipse.ui.texteditor.ITextEditorActionDefinitionIds;
import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.DBException;
import org.jkiss.dbeaver.core.CoreCommands;
import org.jkiss.dbeaver.core.CoreMessages;
import org.jkiss.dbeaver.core.DBeaverActivator;
import org.jkiss.dbeaver.core.DBeaverUI;
import org.jkiss.dbeaver.model.DBUtils;
import org.jkiss.dbeaver.model.data.DBDAttributeBinding;
import org.jkiss.dbeaver.model.data.DBDDisplayFormat;
import org.jkiss.dbeaver.model.edit.DBEPersistAction;
import org.jkiss.dbeaver.model.runtime.AbstractJob;
import org.jkiss.dbeaver.model.runtime.DBRProgressMonitor;
import org.jkiss.dbeaver.model.runtime.DBRRunnableWithProgress;
import org.jkiss.dbeaver.model.sql.SQLUtils;
import org.jkiss.dbeaver.ui.UIIcon;
import org.jkiss.dbeaver.ui.UIUtils;
import org.jkiss.dbeaver.ui.dialogs.sql.ViewSQLDialog;
import org.jkiss.dbeaver.ui.editors.MultiPageAbstractEditor;
import org.jkiss.dbeaver.utils.GeneralUtils;

import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.List;

/**
 * ResultSetCommandHandler
 */
public class ResultSetCommandHandler extends AbstractHandler {

    public static final String CMD_TOGGLE_PANELS = "org.jkiss.dbeaver.core.resultset.grid.togglePreview";
    public static final String CMD_TOGGLE_MODE = "org.jkiss.dbeaver.core.resultset.toggleMode";
    public static final String CMD_SWITCH_PRESENTATION = "org.jkiss.dbeaver.core.resultset.switchPresentation";
    public static final String CMD_ROW_FIRST = "org.jkiss.dbeaver.core.resultset.row.first";
    public static final String CMD_ROW_PREVIOUS = "org.jkiss.dbeaver.core.resultset.row.previous";
    public static final String CMD_ROW_NEXT = "org.jkiss.dbeaver.core.resultset.row.next";
    public static final String CMD_ROW_LAST = "org.jkiss.dbeaver.core.resultset.row.last";
    public static final String CMD_FETCH_PAGE = "org.jkiss.dbeaver.core.resultset.fetch.page";
    public static final String CMD_FETCH_ALL = "org.jkiss.dbeaver.core.resultset.fetch.all";
    public static final String CMD_ROW_EDIT = "org.jkiss.dbeaver.core.resultset.row.edit";
    public static final String CMD_ROW_EDIT_INLINE = "org.jkiss.dbeaver.core.resultset.row.edit.inline";
    public static final String CMD_ROW_ADD = "org.jkiss.dbeaver.core.resultset.row.add";
    public static final String CMD_ROW_COPY = "org.jkiss.dbeaver.core.resultset.row.copy";
    public static final String CMD_ROW_DELETE = "org.jkiss.dbeaver.core.resultset.row.delete";
    public static final String CMD_APPLY_CHANGES = "org.jkiss.dbeaver.core.resultset.applyChanges";
    public static final String CMD_REJECT_CHANGES = "org.jkiss.dbeaver.core.resultset.rejectChanges";
    public static final String CMD_GENERATE_SCRIPT = "org.jkiss.dbeaver.core.resultset.generateScript";
    public static final String CMD_NAVIGATE_LINK = "org.jkiss.dbeaver.core.resultset.navigateLink";
    public static final String CMD_FILTER_MENU = "org.jkiss.dbeaver.core.resultset.filterMenu";

    public static IResultSetController getActiveResultSet(IWorkbenchPart activePart) {
        if (activePart instanceof IResultSetContainer) {
            return ((IResultSetContainer) activePart).getResultSetController();
        } else if (activePart instanceof MultiPageAbstractEditor) {
            return getActiveResultSet(((MultiPageAbstractEditor) activePart).getActiveEditor());
        } else if (activePart != null) {
            return activePart.getAdapter(ResultSetViewer.class);
        } else {
            return null;
        }
    }

    @Nullable
    @Override
    public Object execute(ExecutionEvent event) throws ExecutionException {
        final ResultSetViewer rsv = (ResultSetViewer) getActiveResultSet(HandlerUtil.getActivePart(event));
        if (rsv == null) {
            return null;
        }
        boolean shiftPressed = event.getTrigger() instanceof Event && ((((Event)event.getTrigger()).stateMask & SWT.SHIFT) == SWT.SHIFT);
        String actionId = event.getCommand().getId();
        IResultSetPresentation presentation = rsv.getActivePresentation();
        switch (actionId) {
            case IWorkbenchCommandConstants.FILE_REFRESH:
                rsv.refresh();
                break;
            case CMD_TOGGLE_MODE:
                rsv.toggleMode();
                break;
            case CMD_TOGGLE_PANELS:
                rsv.showPanels(!rsv.isPanelsVisible());
                break;
            case CMD_SWITCH_PRESENTATION:
                rsv.switchPresentation();
                break;
            case CMD_ROW_PREVIOUS:
            case ITextEditorActionDefinitionIds.WORD_PREVIOUS:
                presentation.scrollToRow(IResultSetPresentation.RowPosition.PREVIOUS);
                break;
            case CMD_ROW_NEXT:
            case ITextEditorActionDefinitionIds.WORD_NEXT:
                presentation.scrollToRow(IResultSetPresentation.RowPosition.NEXT);
                break;
            case CMD_ROW_FIRST:
            case ITextEditorActionDefinitionIds.SELECT_WORD_PREVIOUS:
                presentation.scrollToRow(IResultSetPresentation.RowPosition.FIRST);
                break;
            case CMD_ROW_LAST:
            case ITextEditorActionDefinitionIds.SELECT_WORD_NEXT:
                presentation.scrollToRow(IResultSetPresentation.RowPosition.LAST);
                break;
            case CMD_FETCH_PAGE:
                rsv.readNextSegment();
                break;
            case CMD_FETCH_ALL:
                rsv.readAllData();
                break;
            case CMD_ROW_EDIT:
                if (presentation instanceof IResultSetEditor) {
                    ((IResultSetEditor) presentation).openValueEditor(false);
                }
                break;
            case CMD_ROW_EDIT_INLINE:
                if (presentation instanceof IResultSetEditor) {
                    ((IResultSetEditor) presentation).openValueEditor(true);
                }
                break;
            case CMD_ROW_ADD:
                rsv.addNewRow(false, shiftPressed);
                break;
            case CMD_ROW_COPY:
                rsv.addNewRow(true, shiftPressed);
                break;
            case CMD_ROW_DELETE:
            case IWorkbenchCommandConstants.EDIT_DELETE:
                rsv.deleteSelectedRows();
                break;
            case CMD_APPLY_CHANGES:
                rsv.applyChanges(null);
                break;
            case CMD_REJECT_CHANGES:
                rsv.rejectChanges();
                break;
            case CMD_GENERATE_SCRIPT: {
                try {
                    final List<DBEPersistAction> sqlScript = new ArrayList<>();
                    try {
                        DBeaverUI.runInProgressService(new DBRRunnableWithProgress() {
                            @Override
                            public void run(DBRProgressMonitor monitor) throws InvocationTargetException, InterruptedException {
                                List<DBEPersistAction> script = rsv.generateChangesScript(monitor);
                                if (script != null) {
                                    sqlScript.addAll(script);
                                }
                            }
                        });
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    if (!sqlScript.isEmpty()) {
                        String scriptText = DBUtils.generateScript(sqlScript.toArray(new DBEPersistAction[sqlScript.size()]), false);
                        scriptText =
                            SQLUtils.generateCommentLine(
                                rsv.getExecutionContext() == null ? null : rsv.getExecutionContext().getDataSource(),
                                "Actual parameter values may differ, what you see is a default string representation of values") +
                            scriptText;
                        ViewSQLDialog dialog = new ViewSQLDialog(
                            HandlerUtil.getActivePart(event).getSite(),
                            rsv.getExecutionContext(),
                            CoreMessages.editors_entity_dialog_preview_title,
                            UIIcon.SQL_PREVIEW,
                            scriptText);
                        dialog.open();
                    }

                } catch (InvocationTargetException e) {
                    UIUtils.showErrorDialog(HandlerUtil.getActiveShell(event), "Script generation", "Can't generate changes script", e.getTargetException());
                }
                break;
            }
            case IWorkbenchCommandConstants.EDIT_COPY:
                ResultSetUtils.copyToClipboard(
                    presentation.copySelectionToString(
                        new ResultSetCopySettings(false, false, false, null, null, DBDDisplayFormat.EDIT)));
                break;
            case IWorkbenchCommandConstants.EDIT_PASTE:
            case CoreCommands.CMD_PASTE_SPECIAL:
                if (presentation instanceof IResultSetEditor) {
                    ((IResultSetEditor) presentation).pasteFromClipboard(actionId.equals(CoreCommands.CMD_PASTE_SPECIAL));
                }
                break;
            case IWorkbenchCommandConstants.EDIT_CUT:
                ResultSetUtils.copyToClipboard(
                    presentation.copySelectionToString(
                        new ResultSetCopySettings(false, false, true, null, null, DBDDisplayFormat.EDIT))
                );
                break;
            case IWorkbenchCommandConstants.FILE_PRINT:
                presentation.printResultSet();
                break;
            case ITextEditorActionDefinitionIds.SMART_ENTER:
                if (presentation instanceof IResultSetEditor) {
                    ((IResultSetEditor) presentation).openValueEditor(false);
                }
                break;
            case IWorkbenchCommandConstants.EDIT_FIND_AND_REPLACE:
                FindReplaceAction action = new FindReplaceAction(
                    DBeaverActivator.getCoreResourceBundle(),
                    "Editor.FindReplace.",
                    HandlerUtil.getActiveShell(event),
                    rsv.getAdapter(IFindReplaceTarget.class));
                action.run();
                break;
            case CMD_NAVIGATE_LINK:
                final ResultSetRow row = rsv.getCurrentRow();
                final DBDAttributeBinding attr = rsv.getActivePresentation().getCurrentAttribute();
                if (row != null && attr != null) {
                    new AbstractJob("Navigate association") {
                        @Override
                        protected IStatus run(DBRProgressMonitor monitor) {
                            try {
                                rsv.navigateAssociation(monitor, attr, row, false);
                            } catch (DBException e) {
                                return GeneralUtils.makeExceptionStatus(e);
                            }
                            return Status.OK_STATUS;
                        }
                    }.schedule();
                }
                break;
            case IWorkbenchCommandConstants.NAVIGATE_BACKWARD_HISTORY: {
                final int hp = rsv.getHistoryPosition();
                if (hp > 0) {
                    rsv.navigateHistory(hp - 1);
                }
                break;
            }
            case IWorkbenchCommandConstants.NAVIGATE_FORWARD_HISTORY: {
                final int hp = rsv.getHistoryPosition();
                if (hp < rsv.getHistorySize() - 1) {
                    rsv.navigateHistory(hp + 1);
                }
                break;
            }
            case ITextEditorActionDefinitionIds.LINE_GOTO: {
                ResultSetRow currentRow = rsv.getCurrentRow();
                final int rowCount = rsv.getModel().getRowCount();
                if (rowCount <= 0) {
                    break;
                }
                GotoLineDialog d = new GotoLineDialog(
                    HandlerUtil.getActiveShell(event),
                    "Go to Row",
                    "Enter row number (1.." + rowCount + ")",
                    String.valueOf(currentRow == null ? 1 : currentRow.getVisualNumber() + 1),
                    new IInputValidator() {
                        @Override
                        public String isValid(String input) {
                            try {
                                int i = Integer.parseInt(input);
                                if (i <= 0 || rowCount < i) {
                                    return "Row number is out of range";
                                }
                            } catch (NumberFormatException x) {
                                return "Not a number";
                            }

                            return null;
                        }
                    });
                if (d.open() == Window.OK) {
                    int line = Integer.parseInt(d.getValue());
                    rsv.setCurrentRow(rsv.getModel().getRow(line - 1));
                    rsv.getActivePresentation().scrollToRow(IResultSetPresentation.RowPosition.CURRENT);
                }
                break;
            }
            case CMD_FILTER_MENU: {
                rsv.showFiltersMenu();
                break;
            }
        }


        return null;
    }

    static class GotoLineDialog extends InputDialog {
        private static final String DIALOG_ID = "ResultSetCommandHandler.GotoLineDialog";

        public GotoLineDialog(Shell parent, String title, String message, String initialValue, IInputValidator validator) {
            super(parent, title, message, initialValue, validator);
        }

        protected IDialogSettings getDialogBoundsSettings() {
            return UIUtils.getDialogSettings(DIALOG_ID);
        }
    }

}