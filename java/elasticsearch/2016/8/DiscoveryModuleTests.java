/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package org.elasticsearch.discovery;

import org.elasticsearch.Version;
import org.elasticsearch.common.inject.ModuleTestCase;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.discovery.local.LocalDiscovery;
import org.elasticsearch.discovery.zen.ZenDiscovery;
import org.elasticsearch.discovery.zen.elect.ElectMasterService;
import org.elasticsearch.node.Node;
import org.elasticsearch.test.NoopDiscovery;

/**
 */
public class DiscoveryModuleTests extends ModuleTestCase {

    public static class DummyMasterElectionService extends ElectMasterService {

        public DummyMasterElectionService(Settings settings) {
            super(settings);
        }
    }

    public void testRegisterMasterElectionService() {
        Settings settings = Settings.builder().put(DiscoveryModule.ZEN_MASTER_SERVICE_TYPE_SETTING.getKey(), "custom").build();
        DiscoveryModule module = new DiscoveryModule(settings);
        module.addElectMasterService("custom", DummyMasterElectionService.class);
        assertBinding(module, ElectMasterService.class, DummyMasterElectionService.class);
        assertBinding(module, Discovery.class, ZenDiscovery.class);
    }

    public void testLoadUnregisteredMasterElectionService() {
        Settings settings = Settings.builder().put(DiscoveryModule.ZEN_MASTER_SERVICE_TYPE_SETTING.getKey(), "foobar").build();
        DiscoveryModule module = new DiscoveryModule(settings);
        module.addElectMasterService("custom", DummyMasterElectionService.class);
        assertBindingFailure(module, "Unknown master service type [foobar]");
    }

    public void testRegisterDefaults() {
        Settings settings = Settings.EMPTY;
        DiscoveryModule module = new DiscoveryModule(settings);
        assertBinding(module, Discovery.class, ZenDiscovery.class);
    }

    public void testRegisterDiscovery() {
        Settings settings = Settings.builder().put(DiscoveryModule.DISCOVERY_TYPE_SETTING.getKey(), "custom").build();
        DiscoveryModule module = new DiscoveryModule(settings);
        module.addDiscoveryType("custom", NoopDiscovery.class);
        assertBinding(module, Discovery.class, NoopDiscovery.class);
    }


}
