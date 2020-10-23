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

package org.elasticsearch.index.reindex;

import org.elasticsearch.action.index.IndexRequest;

import static org.elasticsearch.common.unit.TimeValue.timeValueMillis;

public abstract class AbstractAsyncBulkIndexbyScrollActionMetadataTestCase<
                Request extends AbstractBulkIndexByScrollRequest<Request>,
                Response extends BulkIndexByScrollResponse>
        extends AbstractAsyncBulkIndexByScrollActionTestCase<Request, Response> {

    protected ScrollableHitSource.BasicHit doc() {
        return new ScrollableHitSource.BasicHit("index", "type", "id", 0);
    }

    public void testTimestampIsCopied() {
        IndexRequest index = new IndexRequest();
        action().copyMetadata(AbstractAsyncBulkIndexByScrollAction.wrap(index), doc().setTimestamp(10L));
        assertEquals("10", index.timestamp());
    }

    public void testTTL() throws Exception {
        IndexRequest index = new IndexRequest();
        action().copyMetadata(AbstractAsyncBulkIndexByScrollAction.wrap(index), doc().setTTL(10L));
        assertEquals(timeValueMillis(10), index.ttl());
    }

    protected abstract AbstractAsyncBulkIndexByScrollAction<Request> action();
}
