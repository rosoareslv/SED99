/*
 * Copyright (C) 2010-2015 Serge Rieder
 * serge@jkiss.org
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
package org.jkiss.dbeaver.registry;

import org.eclipse.core.runtime.Platform;
import org.jkiss.dbeaver.core.Log;
import org.eclipse.core.runtime.IConfigurationElement;
import org.eclipse.core.runtime.IExtensionRegistry;
import org.eclipse.jface.resource.StringConverter;
import org.jkiss.code.Nullable;
import org.jkiss.dbeaver.core.DBeaverCore;
import org.jkiss.dbeaver.model.DBPConnectionType;
import org.jkiss.dbeaver.model.DBPDataSource;
import org.jkiss.dbeaver.model.DBPDriver;
import org.jkiss.dbeaver.model.DBPRegistryListener;
import org.jkiss.dbeaver.model.struct.DBSTypedObject;
import org.jkiss.dbeaver.utils.ContentUtils;
import org.jkiss.utils.CommonUtils;
import org.jkiss.utils.xml.SAXListener;
import org.jkiss.utils.xml.SAXReader;
import org.jkiss.utils.xml.XMLBuilder;
import org.jkiss.utils.xml.XMLException;
import org.xml.sax.Attributes;

import java.io.*;
import java.net.URL;
import java.util.*;

public class DataSourceProviderRegistry
{
    static final Log log = Log.getLog(DataSourceProviderRegistry.class);

    private static DataSourceProviderRegistry instance = null;

    public synchronized static DataSourceProviderRegistry getInstance()
    {
        if (instance == null) {
            instance = new DataSourceProviderRegistry();
            instance.loadExtensions(Platform.getExtensionRegistry());
        }
        return instance;
    }

    private final List<DataSourceProviderDescriptor> dataSourceProviders = new ArrayList<DataSourceProviderDescriptor>();
    private final List<DataTypeProviderDescriptor> dataTypeProviders = new ArrayList<DataTypeProviderDescriptor>();
    private final List<DBPRegistryListener> registryListeners = new ArrayList<DBPRegistryListener>();
    private final Map<String, DBPConnectionType> connectionTypes = new LinkedHashMap<String, DBPConnectionType>();
    private final Map<String, ExternalResourceDescriptor> resourceContributions = new HashMap<String, ExternalResourceDescriptor>();

    private DataSourceProviderRegistry()
    {
    }

    public void loadExtensions(IExtensionRegistry registry)
    {
        // Load datasource providers from external plugins
        {
            IConfigurationElement[] extElements = registry.getConfigurationElementsFor(DataSourceProviderDescriptor.EXTENSION_ID);
            for (IConfigurationElement ext : extElements) {
                DataSourceProviderDescriptor provider = new DataSourceProviderDescriptor(this, ext);
                dataSourceProviders.add(provider);
            }
            Collections.sort(dataSourceProviders, new Comparator<DataSourceProviderDescriptor>() {
                @Override
                public int compare(DataSourceProviderDescriptor o1, DataSourceProviderDescriptor o2)
                {
                    if (o1.isDriversManagable() && !o2.isDriversManagable()) {
                        return 1;
                    }
                    if (o2.isDriversManagable() && !o1.isDriversManagable()) {
                        return -1;
                    }
                    return o1.getName().compareToIgnoreCase(o2.getName());
                }
            });
        }

        // Load data type providers from external plugins
        {
            IConfigurationElement[] extElements = registry.getConfigurationElementsFor(DataTypeProviderDescriptor.EXTENSION_ID);
            for (IConfigurationElement ext : extElements) {
                DataTypeProviderDescriptor provider = new DataTypeProviderDescriptor(this, ext);
                dataTypeProviders.add(provider);
            }
        }

        // Load drivers
        File driversConfig = DBeaverCore.getInstance().getConfigurationFile(RegistryConstants.DRIVERS_FILE_NAME, true);
        if (driversConfig.exists()) {
            loadDrivers(driversConfig);
        }

        // Resolve all driver replacements
        {
            List<DriverDescriptor> allDrivers = new ArrayList<DriverDescriptor>();
            for (DataSourceProviderDescriptor provider : dataSourceProviders) {
                allDrivers.addAll(provider.getDrivers());
            }
            for (DriverDescriptor driver1 : allDrivers) {
                for (DriverDescriptor driver2 : allDrivers) {
                    if (driver1 != driver2 && driver1.replaces(driver2)) {
                        driver2.setReplacedBy(driver1);
                    }
                }
            }
        }

        // Load connection types
        {
            for (DBPConnectionType ct : DBPConnectionType.SYSTEM_TYPES) {
                connectionTypes.put(ct.getId(), ct);
            }
            File ctConfig = DBeaverCore.getInstance().getConfigurationFile(RegistryConstants.CONNECTION_TYPES_FILE_NAME, true);
            if (ctConfig.exists()) {
                loadConnectionTypes(ctConfig);
            }
        }

        // Load external resources information
        {
            IConfigurationElement[] extElements = registry.getConfigurationElementsFor(ExternalResourceDescriptor.EXTENSION_ID);
            for (IConfigurationElement ext : extElements) {
                ExternalResourceDescriptor resource = new ExternalResourceDescriptor(ext);
                resourceContributions.put(resource.getName(), resource);
            }
        }

    }

    public void dispose()
    {
        synchronized (registryListeners) {
            if (!registryListeners.isEmpty()) {
                log.warn("Some datasource registry listeners are still registered: " + registryListeners);
            }
            registryListeners.clear();
        }
        for (DataSourceProviderDescriptor providerDescriptor : this.dataSourceProviders) {
            providerDescriptor.dispose();
        }
        this.dataSourceProviders.clear();
        this.dataTypeProviders.clear();
        this.resourceContributions.clear();
    }

    @Nullable
    public DataSourceProviderDescriptor getDataSourceProvider(String id)
    {
        for (DataSourceProviderDescriptor provider : dataSourceProviders) {
            if (provider.getId().equals(id)) {
                return provider;
            }
        }
        return null;
    }

    public List<DataSourceProviderDescriptor> getDataSourceProviders()
    {
        return dataSourceProviders;
    }

    ////////////////////////////////////////////////////
    // DataType providers

    @Nullable
    public DataTypeProviderDescriptor getDataTypeProvider(DBPDataSource dataSource, DBSTypedObject typedObject)
    {
        DBPDriver driver = dataSource.getContainer().getDriver();
        if (!(driver instanceof DriverDescriptor)) {
            log.warn("Bad datasource specified (driver is not recognized by registry) - " + dataSource);
            return null;
        }
        DataSourceProviderDescriptor dsProvider = ((DriverDescriptor) driver).getProviderDescriptor();

        // First try to find type provider for specific datasource type
        for (DataTypeProviderDescriptor dtProvider : dataTypeProviders) {
            if (!dtProvider.isDefault() && dtProvider.supportsDataSource(dsProvider) && dtProvider.supportsType(typedObject)) {
                return dtProvider;
            }
        }

        // Find in default providers
        for (DataTypeProviderDescriptor dtProvider : dataTypeProviders) {
            if (dtProvider.isDefault() && dtProvider.supportsType(typedObject)) {
                return dtProvider;
            }
        }
        return null;
    }

    private void loadDrivers(File driversConfig)
    {
        if (driversConfig.exists()) {
            try {
                InputStream is = new FileInputStream(driversConfig);
                try {
                    new SAXReader(is).parse(new DriverDescriptor.DriversParser());
                }
                catch (XMLException ex) {
                    log.warn("Drivers config parse error", ex);
                }
                finally {
                    is.close();
                }
            } catch (Exception ex) {
                log.warn("Error loading drivers from " + driversConfig.getPath(), ex);
            }
        }
    }

    public void saveDrivers()
    {
        File driversConfig = DBeaverCore.getInstance().getConfigurationFile(RegistryConstants.DRIVERS_FILE_NAME, false);
        try {
            OutputStream os = new FileOutputStream(driversConfig);
            XMLBuilder xml = new XMLBuilder(os, ContentUtils.DEFAULT_FILE_CHARSET_NAME);
            xml.setButify(true);
            xml.startElement(RegistryConstants.TAG_DRIVERS);
            for (DataSourceProviderDescriptor provider : this.dataSourceProviders) {
                xml.startElement(RegistryConstants.TAG_PROVIDER);
                xml.addAttribute(RegistryConstants.ATTR_ID, provider.getId());
                for (DriverDescriptor driver : provider.getDrivers()) {
                    if (driver.isModified()) {
                        driver.serialize(xml, false);
                    }
                }
                xml.endElement();
            }
            xml.endElement();
            xml.flush();
            os.close();
        }
        catch (Exception ex) {
            log.warn("Error saving drivers", ex);
        }
    }

    private void loadConnectionTypes(File configFile)
    {
        try {
            InputStream is = new FileInputStream(configFile);
            try {
                new SAXReader(is).parse(new ConnectionTypeParser());
            } catch (XMLException ex) {
                log.warn("Can't load connection types config from " + configFile.getPath(), ex);
            }
            finally {
                is.close();
            }
        }
        catch (Exception ex) {
            log.warn("Error parsing connection types", ex);
        }
    }

    public Collection<DBPConnectionType> getConnectionTypes()
    {
        return connectionTypes.values();
    }

    public DBPConnectionType getConnectionType(String id, DBPConnectionType defaultType)
    {
        DBPConnectionType connectionType = connectionTypes.get(id);
        return connectionType == null ? defaultType : connectionType;
    }

    public void addConnectionType(DBPConnectionType connectionType)
    {
        if (this.connectionTypes.containsKey(connectionType.getId())) {
            log.warn("Duplicate connection type id: " + connectionType.getId());
            return;
        }
        this.connectionTypes.put(connectionType.getId(), connectionType);
    }

    public void removeConnectionType(DBPConnectionType connectionType)
    {
        if (!this.connectionTypes.containsKey(connectionType.getId())) {
            log.warn("Connection type doesn't exist: " + connectionType.getId());
            return;
        }
        this.connectionTypes.remove(connectionType.getId());
    }

    public void saveConnectionTypes()
    {
        File ctConfig = DBeaverCore.getInstance().getConfigurationFile(RegistryConstants.CONNECTION_TYPES_FILE_NAME, false);
        try {
            OutputStream os = new FileOutputStream(ctConfig);
            XMLBuilder xml = new XMLBuilder(os, ContentUtils.DEFAULT_FILE_CHARSET_NAME);
            xml.setButify(true);
            xml.startElement(RegistryConstants.TAG_TYPES);
            for (DBPConnectionType connectionType : connectionTypes.values()) {
                xml.startElement(RegistryConstants.TAG_TYPE);
                xml.addAttribute(RegistryConstants.ATTR_ID, connectionType.getId());
                xml.addAttribute(RegistryConstants.ATTR_NAME, CommonUtils.toString(connectionType.getName()));
                xml.addAttribute(RegistryConstants.ATTR_COLOR, StringConverter.asString(connectionType.getColor().getRGB()));
                xml.addAttribute(RegistryConstants.ATTR_DESCRIPTION, CommonUtils.toString(connectionType.getDescription()));
                xml.addAttribute(RegistryConstants.ATTR_AUTOCOMMIT, connectionType.isAutocommit());
                xml.addAttribute(RegistryConstants.ATTR_CONFIRM_EXECUTE, connectionType.isConfirmExecute());
                xml.endElement();
            }
            xml.endElement();
            xml.flush();
            os.close();
        }
        catch (Exception ex) {
            log.warn("Error saving drivers", ex);
        }
    }

    /**
     * Searches for resource within external resources provided by plugins
     * @param resourcePath path
     * @return URL or null if specified resource not found
     */
    @Nullable
    public URL findResourceURL(String resourcePath)
    {
        ExternalResourceDescriptor descriptor = resourceContributions.get(resourcePath);
        return descriptor == null ? null : descriptor.getURL();
    }

    public void addDataSourceRegistryListener(DBPRegistryListener listener)
    {
        synchronized (registryListeners) {
            registryListeners.add(listener);
        }
    }

    public void removeDataSourceRegistryListener(DBPRegistryListener listener)
    {
        synchronized (registryListeners) {
            registryListeners.remove(listener);
        }
    }

    void fireRegistryChange(DataSourceRegistry registry, boolean load)
    {
        synchronized (registryListeners) {
            for (DBPRegistryListener listener : registryListeners) {
                if (load) {
                    listener.handleRegistryLoad(registry);
                } else {
                    listener.handleRegistryUnload(registry);
                }
            }
        }
    }

    class ConnectionTypeParser implements SAXListener
    {
        @Override
        public void saxStartElement(SAXReader reader, String namespaceURI, String localName, Attributes atts)
            throws XMLException
        {
            if (localName.equals(RegistryConstants.TAG_TYPE)) {
                DBPConnectionType connectionType = new DBPConnectionType(
                    atts.getValue(RegistryConstants.ATTR_ID),
                    atts.getValue(RegistryConstants.ATTR_NAME),
                    StringConverter.asRGB(atts.getValue(RegistryConstants.ATTR_COLOR)),
                    atts.getValue(RegistryConstants.ATTR_DESCRIPTION),
                    CommonUtils.getBoolean(atts.getValue(RegistryConstants.ATTR_AUTOCOMMIT)),
                    CommonUtils.getBoolean(atts.getValue(RegistryConstants.ATTR_CONFIRM_EXECUTE)));
                connectionTypes.put(connectionType.getId(), connectionType);
            }
        }

        @Override
        public void saxText(SAXReader reader, String data) {}

        @Override
        public void saxEndElement(SAXReader reader, String namespaceURI, String localName) {}
    }

}
