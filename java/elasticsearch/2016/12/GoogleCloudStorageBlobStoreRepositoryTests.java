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

package org.elasticsearch.repositories.gcs;

import com.google.api.services.storage.Storage;
import org.elasticsearch.common.blobstore.gcs.MockHttpTransport;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.common.unit.ByteSizeUnit;
import org.elasticsearch.common.unit.TimeValue;
import org.elasticsearch.env.Environment;
import org.elasticsearch.plugin.repository.gcs.GoogleCloudStoragePlugin;
import org.elasticsearch.plugins.Plugin;
import org.elasticsearch.repositories.blobstore.ESBlobStoreRepositoryIntegTestCase;
import org.junit.BeforeClass;

import java.util.Arrays;
import java.util.Collection;
import java.util.concurrent.atomic.AtomicReference;

import static org.elasticsearch.test.hamcrest.ElasticsearchAssertions.assertAcked;

public class GoogleCloudStorageBlobStoreRepositoryTests extends ESBlobStoreRepositoryIntegTestCase {

    private static final String BUCKET = "gcs-repository-test";

    // Static storage client shared among all nodes in order to act like a remote repository service:
    // all nodes must see the same content
    private static final AtomicReference<Storage> storage = new AtomicReference<>();

    @Override
    protected Collection<Class<? extends Plugin>> nodePlugins() {
        return Arrays.asList(MockGoogleCloudStoragePlugin.class);
    }

    @Override
    protected void createTestRepository(String name) {
        assertAcked(client().admin().cluster().preparePutRepository(name)
                .setType(GoogleCloudStorageRepository.TYPE)
                .setSettings(Settings.builder()
                        .put("bucket", BUCKET)
                        .put("base_path", GoogleCloudStorageBlobStoreRepositoryTests.class.getSimpleName())
                        .put("service_account", "_default_")
                        .put("compress", randomBoolean())
                        .put("chunk_size", randomIntBetween(100, 1000), ByteSizeUnit.BYTES)));
    }

    @BeforeClass
    public static void setUpStorage() {
        storage.set(MockHttpTransport.newStorage(BUCKET, GoogleCloudStorageBlobStoreRepositoryTests.class.getName()));
    }

    public static class MockGoogleCloudStoragePlugin extends GoogleCloudStoragePlugin {
        @Override
        protected GoogleCloudStorageService createStorageService(Environment environment) {
            return new MockGoogleCloudStorageService();
        }
    }

    public static class MockGoogleCloudStorageService implements GoogleCloudStorageService {
        @Override
        public Storage createClient(String serviceAccount, String application, TimeValue connectTimeout, TimeValue readTimeout) throws
                Exception {
            return storage.get();
        }
    }
}
