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

package org.elasticsearch.ingest.useragent;

import org.elasticsearch.ingest.RandomDocumentPicks;
import org.elasticsearch.ingest.IngestDocument;
import org.elasticsearch.ingest.useragent.UserAgentProcessor;
import org.elasticsearch.test.ESTestCase;
import org.junit.BeforeClass;

import java.io.IOException;
import java.io.InputStream;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.Map;

import static org.hamcrest.Matchers.hasKey;
import static org.hamcrest.Matchers.is;

public class UserAgentProcessorTests extends ESTestCase {

    private static UserAgentProcessor processor;
    
    @BeforeClass
    public static void setupProcessor() throws IOException {
        InputStream regexStream = UserAgentProcessor.class.getResourceAsStream("/regexes.yaml");
        assertNotNull(regexStream);
        
        UserAgentParser parser = new UserAgentParser(randomAsciiOfLength(10), regexStream, new UserAgentCache(1000));
        
        processor = new UserAgentProcessor(randomAsciiOfLength(10), "source_field", "target_field", parser,
                EnumSet.allOf(UserAgentProcessor.Property.class));
    }
    
    @SuppressWarnings("unchecked")
    public void testCommonBrowser() throws Exception {
        Map<String, Object> document = new HashMap<>();
        document.put("source_field",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/33.0.1750.149 Safari/537.36");
        IngestDocument ingestDocument = RandomDocumentPicks.randomIngestDocument(random(), document);
        
        processor.execute(ingestDocument);
        Map<String, Object> data = ingestDocument.getSourceAndMetadata();

        assertThat(data, hasKey("target_field"));
        Map<String, Object> target = (Map<String, Object>) data.get("target_field");
        
        assertThat(target.get("name"), is("Chrome"));
        assertThat(target.get("major"), is("33"));
        assertThat(target.get("minor"), is("0"));
        assertThat(target.get("patch"), is("1750"));
        assertNull(target.get("build"));
        
        assertThat(target.get("os"), is("Mac OS X 10.9.2"));
        assertThat(target.get("os_name"), is("Mac OS X"));
        assertThat(target.get("os_major"), is("10"));
        assertThat(target.get("os_minor"), is("9"));
        
        assertThat(target.get("device"), is("Other"));
    }
    
    @SuppressWarnings("unchecked")
    public void testUncommonDevice() throws Exception {
        Map<String, Object> document = new HashMap<>();
        document.put("source_field",
                "Mozilla/5.0 (Linux; U; Android 3.0; en-us; Xoom Build/HRI39) AppleWebKit/525.10+ "
                + "(KHTML, like Gecko) Version/3.0.4 Mobile Safari/523.12.2");
        IngestDocument ingestDocument = RandomDocumentPicks.randomIngestDocument(random(), document);
        
        processor.execute(ingestDocument);
        Map<String, Object> data = ingestDocument.getSourceAndMetadata();

        assertThat(data, hasKey("target_field"));
        Map<String, Object> target = (Map<String, Object>) data.get("target_field");
        
        assertThat(target.get("name"), is("Android"));
        assertThat(target.get("major"), is("3"));
        assertThat(target.get("minor"), is("0"));
        assertNull(target.get("patch"));
        assertNull(target.get("build"));
        
        assertThat(target.get("os"), is("Android 3.0"));
        assertThat(target.get("os_name"), is("Android"));
        assertThat(target.get("os_major"), is("3"));
        assertThat(target.get("os_minor"), is("0"));
        
        assertThat(target.get("device"), is("Motorola Xoom"));
    }
    
    @SuppressWarnings("unchecked")
    public void testSpider() throws Exception {
        Map<String, Object> document = new HashMap<>();
        document.put("source_field",
            "Mozilla/5.0 (compatible; EasouSpider; +http://www.easou.com/search/spider.html)");
        IngestDocument ingestDocument = RandomDocumentPicks.randomIngestDocument(random(), document);
        
        processor.execute(ingestDocument);
        Map<String, Object> data = ingestDocument.getSourceAndMetadata();

        assertThat(data, hasKey("target_field"));
        Map<String, Object> target = (Map<String, Object>) data.get("target_field");
        
        assertThat(target.get("name"), is("EasouSpider"));
        assertNull(target.get("major"));
        assertNull(target.get("minor"));
        assertNull(target.get("patch"));
        assertNull(target.get("build"));
        
        assertThat(target.get("os"), is("Other"));
        assertThat(target.get("os_name"), is("Other"));
        assertNull(target.get("os_major"));
        assertNull(target.get("os_minor"));
        
        assertThat(target.get("device"), is("Spider"));
    }
    
    @SuppressWarnings("unchecked")
    public void testUnknown() throws Exception {
        Map<String, Object> document = new HashMap<>();
        document.put("source_field",
            "Something I made up v42.0.1");
        IngestDocument ingestDocument = RandomDocumentPicks.randomIngestDocument(random(), document);
        
        processor.execute(ingestDocument);
        Map<String, Object> data = ingestDocument.getSourceAndMetadata();

        assertThat(data, hasKey("target_field"));
        Map<String, Object> target = (Map<String, Object>) data.get("target_field");
        
        assertThat(target.get("name"), is("Other"));
        assertNull(target.get("major"));
        assertNull(target.get("minor"));
        assertNull(target.get("patch"));
        assertNull(target.get("build"));
        
        assertThat(target.get("os"), is("Other"));
        assertThat(target.get("os_name"), is("Other"));
        assertNull(target.get("os_major"));
        assertNull(target.get("os_minor"));
        
        assertThat(target.get("device"), is("Other"));
    }
}

