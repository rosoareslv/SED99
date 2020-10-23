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
package org.jkiss.dbeaver.model.net.ssh;

import com.jcraft.jsch.*;
import org.jkiss.dbeaver.DBException;
import org.jkiss.dbeaver.Log;
import org.jkiss.dbeaver.model.net.DBWHandlerConfiguration;
import org.jkiss.dbeaver.model.runtime.DBRProgressMonitor;
import org.jkiss.dbeaver.utils.RuntimeUtils;
import org.jkiss.utils.CommonUtils;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;

/**
 * SSH tunnel
 */
public class SSHImplementationJsch extends SSHImplementationAbstract {

    private static final Log log = Log.getLog(SSHImplementationJsch.class);

    private static transient JSch jsch;
    private transient Session session;

    @Override
    protected void setupTunnel(DBRProgressMonitor monitor, DBWHandlerConfiguration configuration, String dbHost, String sshHost, String aliveInterval, int sshPortNum, File privKeyFile, int connectTimeout, int dbPort, int localPort) throws DBException, IOException {
        UserInfo ui = new UIUserInfo(configuration);
        try {
            if (jsch == null) {
                jsch = new JSch();
                JSch.setLogger(new LoggerProxy());
            }
            if (privKeyFile != null) {
                if (!CommonUtils.isEmpty(ui.getPassphrase())) {
                    jsch.addIdentity(privKeyFile.getAbsolutePath(), ui.getPassphrase());
                } else {
                    jsch.addIdentity(privKeyFile.getAbsolutePath());
                }
            }

            log.debug("Instantiate SSH tunnel");
            session = jsch.getSession(configuration.getUserName(), sshHost, sshPortNum);
            session.setConfig("StrictHostKeyChecking", "no");
            //session.setConfig("PreferredAuthentications", "password,publickey,keyboard-interactive");
            session.setConfig("PreferredAuthentications",
                privKeyFile != null ? "publickey" : "password");
            session.setConfig("ConnectTimeout", String.valueOf(connectTimeout));
            session.setUserInfo(ui);
            if (!CommonUtils.isEmpty(aliveInterval)) {
                session.setServerAliveInterval(Integer.parseInt(aliveInterval));
            }
            log.debug("Connect to tunnel host");
            session.connect(connectTimeout);
            try {
                session.setPortForwardingL(localPort, dbHost, dbPort);
            } catch (JSchException e) {
                closeTunnel(monitor);
                throw e;
            }
        } catch (JSchException e) {
            throw new DBException("Cannot establish tunnel", e);
        }
    }

    @Override
    public void closeTunnel(DBRProgressMonitor monitor) throws DBException, IOException {
        if (session != null) {
            RuntimeUtils.runTask(monitor1 -> {
                try {
                    session.disconnect();
                } catch (Exception e) {
                    throw new InvocationTargetException(e);
                }
            }, "Close SSH session", 1000);
            session = null;
        }
    }

    @Override
    public void invalidateTunnel(DBRProgressMonitor monitor) throws DBException, IOException {
        boolean isAlive = session != null && session.isConnected();
        if (isAlive) {
            try {
                session.sendKeepAliveMsg();
            } catch (Exception e) {
                isAlive = false;
            }
        }
        if (!isAlive) {
            closeTunnel(monitor);
            initTunnel(monitor, null, savedConfiguration, savedConnectionInfo);
        }
    }

    private class UIUserInfo implements UserInfo {
        DBWHandlerConfiguration configuration;
        private UIUserInfo(DBWHandlerConfiguration configuration)
        {
            this.configuration = configuration;
        }

        @Override
        public String getPassphrase()
        {
            return configuration.getPassword();
        }

        @Override
        public String getPassword()
        {
            return configuration.getPassword();
        }

        @Override
        public boolean promptPassword(String message)
        {
            return true;
        }

        @Override
        public boolean promptPassphrase(String message)
        {
            return true;
        }

        @Override
        public boolean promptYesNo(String message)
        {
            return false;
        }

        @Override
        public void showMessage(String message)
        {
            log.info(message);
        }
    }

    private class LoggerProxy implements Logger {
        @Override
        public boolean isEnabled(int level) {
            return true;
        }

        @Override
        public void log(int level, String message) {
            String levelStr;
            switch (level) {
                case INFO: levelStr = "INFO"; break;
                case WARN: levelStr = "WARN"; break;
                case ERROR: levelStr = "ERROR"; break;
                case FATAL: levelStr = "FATAL"; break;
                case DEBUG:
                default:
                    levelStr = "DEBUG";
                    break;
            }
            log.debug("SSH " + levelStr + ": " + message);

        }
    }
}
