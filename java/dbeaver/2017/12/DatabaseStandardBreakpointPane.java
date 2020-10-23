package org.jkiss.dbeaver.debug.ui.details;

import org.eclipse.swt.widgets.Composite;
import org.jkiss.dbeaver.debug.internal.ui.DebugUiMessages;
import org.jkiss.dbeaver.debug.ui.DebugUI;

public class DatabaseStandardBreakpointPane extends DatabaseDebugDetailPane<DatabaseBreakpointEditor> {

    public static final String DETAIL_PANE_STANDARD_BREAKPOINT = DebugUI.BUNDLE_SYMBOLIC_NAME + '.'
            + "DETAIL_PANE_STANDARD_BREAKPOINT"; //$NON-NLS-1$

    public DatabaseStandardBreakpointPane()
    {
        super(DebugUiMessages.DatabaseStandardBreakpointPane_name,
                DebugUiMessages.DatabaseStandardBreakpointPane_description, DETAIL_PANE_STANDARD_BREAKPOINT);
    }

    @Override
    protected DatabaseBreakpointEditor createEditor(Composite parent)
    {
        return new DatabaseBreakpointEditor();
    }

}
