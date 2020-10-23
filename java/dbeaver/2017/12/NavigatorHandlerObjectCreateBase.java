/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2017 Serge Rider (serge@jkiss.org)
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
package org.jkiss.dbeaver.ui.actions.navigator;

import org.eclipse.core.resources.IProject;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.ui.IWorkbenchWindow;
import org.jkiss.dbeaver.DBException;
import org.jkiss.dbeaver.core.DBeaverCore;
import org.jkiss.dbeaver.core.DBeaverUI;
import org.jkiss.dbeaver.model.DBPDataSource;
import org.jkiss.dbeaver.model.DBPObject;
import org.jkiss.dbeaver.model.DBPRefreshableObject;
import org.jkiss.dbeaver.model.DBPScriptObject;
import org.jkiss.dbeaver.model.edit.DBEObjectMaker;
import org.jkiss.dbeaver.model.edit.DBEObjectManager;
import org.jkiss.dbeaver.model.navigator.DBNContainer;
import org.jkiss.dbeaver.model.navigator.DBNDataSource;
import org.jkiss.dbeaver.model.navigator.DBNDatabaseNode;
import org.jkiss.dbeaver.model.navigator.DBNNode;
import org.jkiss.dbeaver.model.runtime.AbstractJob;
import org.jkiss.dbeaver.model.runtime.DBRProgressMonitor;
import org.jkiss.dbeaver.model.struct.DBSObject;
import org.jkiss.dbeaver.registry.editor.EntityEditorsRegistry;
import org.jkiss.dbeaver.runtime.ui.DBUserInterface;
import org.jkiss.dbeaver.ui.UIUtils;
import org.jkiss.dbeaver.ui.editors.IDatabaseEditor;
import org.jkiss.dbeaver.ui.editors.entity.EntityEditor;
import org.jkiss.dbeaver.ui.editors.entity.EntityEditorInput;
import org.jkiss.dbeaver.ui.navigator.database.DatabaseNavigatorView;
import org.jkiss.dbeaver.utils.GeneralUtils;
import org.jkiss.dbeaver.utils.RuntimeUtils;

public abstract class NavigatorHandlerObjectCreateBase extends NavigatorHandlerObjectBase {

    protected boolean createNewObject(final IWorkbenchWindow workbenchWindow, DBNNode element, DBNDatabaseNode copyFrom)
    {
        try {
            DBNContainer container = null;
            if (element instanceof DBNContainer && !(element instanceof DBNDataSource)) {
                container = (DBNContainer) element;
            } else {
                DBNNode parentNode = element.getParentNode();
                if (parentNode instanceof DBNContainer) {
                    container = (DBNContainer) parentNode;
                }
            }
            if (container == null) {
                throw new DBException("Can't detect container for '" + element.getNodeName() + "'");
            }
            Class<?> childType = container.getChildrenClass();
            if (childType == null) {
                throw new DBException("Can't determine child element type for container '" + container + "'");
            }
            if (childType == IProject.class) {
                return NavigatorHandlerProjectCreate.createNewProject(workbenchWindow);
            }

            DBSObject sourceObject = copyFrom == null ? null : copyFrom.getObject();
            // Do not check for type - manager must do it. Potentially we can copy anything into anything.
//            if (sourceObject != null && !childType.isAssignableFrom(sourceObject.getClass())) {
//                throw new DBException("Can't create '" + childType.getName() + "' from '" + sourceObject.getClass().getName() + "'");
//            }

            final EntityEditorsRegistry editorsRegistry = EntityEditorsRegistry.getInstance();
            DBEObjectManager<?> objectManager = editorsRegistry.getObjectManager(childType);
            if (objectManager == null) {
                throw new DBException("Object manager not found for type '" + childType.getName() + "'");
            }
            DBEObjectMaker objectMaker = (DBEObjectMaker) objectManager;

            DBPDataSource dataSource = container instanceof DBNDatabaseNode ? ((DBNDatabaseNode) container).getDataSource() : null;

            final boolean openEditor = dataSource != null &&
                (objectMaker.getMakerOptions(dataSource) & DBEObjectMaker.FEATURE_EDITOR_ON_CREATE) != 0;
            CommandTarget commandTarget = getCommandTarget(
                workbenchWindow,
                container,
                childType,
                openEditor);

            // Parent is model object - not node
            final Object parentObject = container.getValueObject();
            if (parentObject instanceof DBPObject) {
                createDatabaseObject(commandTarget, objectMaker, (DBPObject) parentObject, sourceObject);
            } else {
                throw new DBException("Parent object type is not supported: " + parentObject);
            }
        }
        catch (Throwable e) {
            DBUserInterface.getInstance().showError("Create object", null, e);
            return false;
        }

        return true;
    }

    private <OBJECT_TYPE extends DBSObject, CONTAINER_TYPE extends DBPObject> void createDatabaseObject(
        CommandTarget commandTarget,
        DBEObjectMaker<OBJECT_TYPE, CONTAINER_TYPE> objectMaker,
        CONTAINER_TYPE parentObject,
        DBSObject sourceObject) throws DBException
    {
        final CreateJob<OBJECT_TYPE, CONTAINER_TYPE> job = new CreateJob<>(commandTarget, objectMaker, parentObject, sourceObject);
        job.schedule();
    }

    static class CreateJob<OBJECT_TYPE extends DBSObject, CONTAINER_TYPE extends DBPObject> extends AbstractJob {
        private final CommandTarget commandTarget;
        private final DBEObjectMaker<OBJECT_TYPE, CONTAINER_TYPE> objectMaker;
        private final CONTAINER_TYPE parentObject;
        private final DBSObject sourceObject;
        private OBJECT_TYPE newObject;

        public CreateJob(CommandTarget commandTarget, DBEObjectMaker<OBJECT_TYPE, CONTAINER_TYPE> objectMaker, CONTAINER_TYPE parentObject, DBSObject sourceObject) {
            super("Create new database object with " + objectMaker.getClass().getSimpleName());
            setUser(true);
            this.commandTarget = commandTarget;
            this.objectMaker = objectMaker;
            this.parentObject = parentObject;
            this.sourceObject = sourceObject;
        }

        @Override
        protected IStatus run(DBRProgressMonitor monitor) {
            try {
                newObject = objectMaker.createNewObject(monitor, commandTarget.getContext(), parentObject, sourceObject);
                if (newObject == null) {
                    return Status.CANCEL_STATUS;
                }
                if (parentObject instanceof DBSObject) {
                    if ((objectMaker.getMakerOptions(((DBSObject) parentObject).getDataSource()) & DBEObjectMaker.FEATURE_SAVE_IMMEDIATELY) != 0) {
                        // Save object manager's content
                        commandTarget.getContext().saveChanges(monitor, DBPScriptObject.EMPTY_OPTIONS);
                        // Refresh new object (so it can load some props from database)
                        if (newObject instanceof DBPRefreshableObject) {
                            final DBNDatabaseNode newChild = DBeaverCore.getInstance().getNavigatorModel().findNode(newObject);
                            if (newChild != null) {
                                newChild.refreshNode(monitor, this);
                                newObject = (OBJECT_TYPE) newChild.getObject();
                            }
                        }
                    }
                }

                {
                    // Wait for a few seconds to let listeners to add new object's node in navigator node
                    for (int i = 0; i < 50; i++) {
                        if (DBeaverCore.getInstance().getNavigatorModel().findNode(newObject) != null) {
                            break;
                        }
                        RuntimeUtils.pause(100);
                    }
                }

                // Open object in UI thread
                DBeaverUI.syncExec(this::openNewObject);

                return Status.OK_STATUS;
            } catch (Exception e) {
                return GeneralUtils.makeExceptionStatus(e);
            }
        }

        private void openNewObject() {
            IWorkbenchWindow workbenchWindow = DBeaverUI.getActiveWorkbenchWindow();
            try {
                final DBNDatabaseNode newChild = DBeaverCore.getInstance().getNavigatorModel().findNode(newObject);
                if (newChild != null) {
                    DatabaseNavigatorView view = UIUtils.findView(workbenchWindow, DatabaseNavigatorView.class);
                    if (view != null) {
                        view.showNode(newChild);
                    }
                    final boolean openEditor =
                        parentObject instanceof DBSObject &&
                        (objectMaker.getMakerOptions(((DBSObject) parentObject).getDataSource()) & DBEObjectMaker.FEATURE_EDITOR_ON_CREATE) != 0;
                    IDatabaseEditor editor = commandTarget.getEditor();
                    if (editor != null) {
                        // Just activate existing editor
                        workbenchWindow.getActivePage().activate(editor);
                    } else if (openEditor) {
                        // Open new one with existing context
                        EntityEditorInput editorInput = new EntityEditorInput(
                            newChild,
                            commandTarget.getContext());
                        workbenchWindow.getActivePage().openEditor(
                            editorInput,
                            EntityEditor.class.getName());
                    }
                } else {
                    throw new DBException("Can't find node corresponding to new object");
                }
            } catch (Throwable e) {
                DBUserInterface.getInstance().showError("Create object", null, e);
            }
        }

    }

}