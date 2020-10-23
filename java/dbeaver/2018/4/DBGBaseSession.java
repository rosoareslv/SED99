/*
 * DBeaver - Universal Database Manager
 * Copyright (C) 2010-2018 Serge Rider (serge@jkiss.org)
 * Copyright (C) 2017-2018 Andrew Khitrin (ahitrin@gmail.com)
 * Copyright (C) 2017-2018 Alexander Fedorov (alexander.fedorov@jkiss.org)
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
package org.jkiss.dbeaver.debug;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.FutureTask;
import java.util.concurrent.locks.ReentrantReadWriteLock;

import org.jkiss.dbeaver.model.impl.jdbc.JDBCExecutionContext;
import org.jkiss.dbeaver.model.runtime.VoidProgressMonitor;

public abstract class DBGBaseSession implements DBGSession {

    protected final ReentrantReadWriteLock lock = new ReentrantReadWriteLock(true);

    private final DBGBaseController controller;

    protected FutureTask<Void> task;

    private Thread workerThread = null;

    private JDBCExecutionContext connection = null;

    private final List<DBGBreakpointDescriptor> breakpoints = new ArrayList<>(1);

    protected DBGBaseSession(DBGBaseController controller) {
        this.controller = controller;
    }

    /**
     * Return connection used in debug session
     * 
     * @return java.sql.Connection
     * @throws DBGException
     */
    // FIXME: rework to DBC API
    protected Connection getConnection() throws DBGException {
        try {
            return ((JDBCExecutionContext) connection).getConnection(new VoidProgressMonitor());
        } catch (SQLException e) {
            throw new DBGException("SQL error", e);
        }
    }

    // FIXME: should be known during construction
    protected void setConnection(JDBCExecutionContext connection) {
        this.connection = connection;
    }

    protected DBGBaseController getController() {
        return controller;
    }

    /**
     * Return true if debug session up and running on server
     * 
     * @return boolean
     */
    public boolean isAttached() {
        return connection != null;
    }

    /**
     * Return true if session up and running debug thread
     * 
     * @return boolean
     */
    public boolean isWaiting() {
        return (task == null ? false : !task.isDone()) && (workerThread == null ? false : workerThread.isAlive());
    }

    public abstract boolean isDone();

    /**
     * Start thread for SQL command
     * 
     * @param commandSQL
     * @param name
     * @throws DBGException
     */
    protected void runAsync(String commandSQL, String name, DBGEvent begin, DBGEvent end) throws DBGException {
        Connection connection = getConnection();
        try (Statement stmt = connection.createStatement()) {
            connection.setAutoCommit(false);
        } catch (SQLException e) {
            throw new DBGException("SQL error", e);
        }
        DBGWorker worker = new DBGWorker(this, commandSQL, begin, end);
        task = new FutureTask<Void>(worker);
        workerThread = new Thread(task);
        workerThread.setName(name);
        workerThread.start();
    }

    public void close() throws DBGException {
        lock.writeLock().lock();
        try {
            if (!isAttached()) {
                lock.writeLock().unlock();
                throw new DBGException("Debug session not attached");
            }
            doDetach();
            if (!isDone() && task != null) {
                task.cancel(true);
            }

            connection.close();
        } finally {
            lock.writeLock().unlock();
        }
    }

    protected abstract void doDetach() throws DBGException;

    protected abstract String composeAbortCommand();

    @Override
    public List<? extends DBGBreakpointDescriptor> getBreakpoints() {
        return new ArrayList<DBGBreakpointDescriptor>(breakpoints);
    }

    @Override
    public void addBreakpoint(DBGBreakpointDescriptor descriptor) throws DBGException {
        acquireReadLock();
        try {
            try (Statement stmt = getConnection().createStatement()) {
                String sqlQuery = composeAddBreakpointCommand(descriptor);
                stmt.executeQuery(sqlQuery);
            } catch (SQLException e) {
                throw new DBGException("SQL error", e);
            }
            breakpoints.add(descriptor);
        } finally {
            lock.readLock().unlock();
        }
    }

    protected abstract String composeAddBreakpointCommand(DBGBreakpointDescriptor descriptor);

    @Override
    public void removeBreakpoint(DBGBreakpointDescriptor bp) throws DBGException {
        acquireReadLock();
        try {
            try (Statement stmt = getConnection().createStatement()) {
                String sqlCommand = composeRemoveBreakpointCommand(bp);
                stmt.executeQuery(sqlCommand);
            } catch (SQLException e) {
                throw new DBGException("SQL error", e);
            }
            breakpoints.remove(bp);
        } finally {
            lock.readLock().unlock();
        }
    }

    protected abstract String composeRemoveBreakpointCommand(DBGBreakpointDescriptor descriptor);

    /**
     * Try to acquire shared lock
     * 
     * @throws DBGException
     */
    protected void acquireReadLock() throws DBGException {
        try {
            lock.readLock().lockInterruptibly();
        } catch (InterruptedException e1) {
            throw new DBGException(e1.getMessage());
        }
        if (!isAttached()) {
            lock.readLock().unlock();
            throw new DBGException("Debug session not attached");
        }
        if (isWaiting()) {
            lock.readLock().unlock();
            throw new DBGException("Debug session in waiting state");
        }
        if (!isDone()) {
            lock.readLock().unlock();
            throw new DBGException("Debug session in incorrect state");
        }

    }

    /**
     * Try to acquire exclusive lock
     * 
     * @throws DBGException
     */
    protected void acquireWriteLock() throws DBGException {
        try {
            lock.writeLock().lockInterruptibly();
        } catch (InterruptedException e1) {
            throw new DBGException(e1.getMessage());
        }
        if (!isAttached()) {
            lock.writeLock().unlock();
            throw new DBGException("Debug session not attached");
        }
        if (isWaiting()) {
            lock.writeLock().unlock();
            throw new DBGException("Debug session in waiting state");
        }
        if (!isDone()) {
            lock.writeLock().unlock();
            throw new DBGException("Debug session in incorrect state");
        }
    }

    protected void fireEvent(DBGEvent event) {
        controller.fireEvent(event);
    }

}
