/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.ml.job.results;

import org.elasticsearch.common.io.stream.Writeable.Reader;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.test.AbstractSerializingTestCase;
import org.elasticsearch.xpack.ml.job.process.autodetect.output.FlushAcknowledgement;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSizeStats;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSnapshot;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.ModelSnapshotTests;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.Quantiles;
import org.elasticsearch.xpack.ml.job.process.autodetect.state.QuantilesTests;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;

public class AutodetectResultTests extends AbstractSerializingTestCase<AutodetectResult> {

    @Override
    protected AutodetectResult doParseInstance(XContentParser parser) {
        return AutodetectResult.PARSER.apply(parser, null);
    }

    @Override
    protected AutodetectResult createTestInstance() {
        Bucket bucket;
        List<AnomalyRecord> records = null;
        List<Influencer> influencers = null;
        Quantiles quantiles;
        ModelSnapshot modelSnapshot;
        ModelSizeStats.Builder modelSizeStats;
        ModelPlot modelPlot;
        CategoryDefinition categoryDefinition;
        FlushAcknowledgement flushAcknowledgement;
        String jobId = "foo";
        if (randomBoolean()) {
            bucket = new Bucket(jobId, new Date(randomNonNegativeLong()), randomNonNegativeLong());
        } else {
            bucket = null;
        }
        if (randomBoolean()) {
            int size = randomInt(10);
            records = new ArrayList<>(size);
            for (int i = 0; i < size; i++) {
                AnomalyRecord record = new AnomalyRecord(jobId, new Date(randomLong()), randomNonNegativeLong());
                record.setProbability(randomDoubleBetween(0.0, 1.0, true));
                records.add(record);
            }

        }
        if (randomBoolean()) {
            int size = randomInt(10);
            influencers = new ArrayList<>(size);
            for (int i = 0; i < size; i++) {
                Influencer influencer = new Influencer(jobId, randomAlphaOfLength(10), randomAlphaOfLength(10),
                        new Date(randomNonNegativeLong()), randomNonNegativeLong());
                influencer.setProbability(randomDoubleBetween(0.0, 1.0, true));
                influencers.add(influencer);
            }
        }
        if (randomBoolean()) {
            quantiles = QuantilesTests.createRandomized();
        } else {
            quantiles = null;
        }
        if (randomBoolean()) {
            modelSnapshot = ModelSnapshotTests.createRandomized();
        } else {
            modelSnapshot = null;
        }
        if (randomBoolean()) {
            modelSizeStats = new ModelSizeStats.Builder(jobId);
            modelSizeStats.setModelBytes(randomNonNegativeLong());
        } else {
            modelSizeStats = null;
        }
        if (randomBoolean()) {
            modelPlot = new ModelPlot(jobId, new Date(randomLong()), randomNonNegativeLong());
        } else {
            modelPlot = null;
        }
        if (randomBoolean()) {
            categoryDefinition = new CategoryDefinition(jobId);
            categoryDefinition.setCategoryId(randomLong());
        } else {
            categoryDefinition = null;
        }
        if (randomBoolean()) {
            flushAcknowledgement = new FlushAcknowledgement(randomAlphaOfLengthBetween(1, 20),  new Date(randomNonNegativeLong()));
        } else {
            flushAcknowledgement = null;
        }
        return new AutodetectResult(bucket, records, influencers, quantiles, modelSnapshot,
                modelSizeStats == null ? null : modelSizeStats.build(), modelPlot, categoryDefinition, flushAcknowledgement);
    }

    @Override
    protected Reader<AutodetectResult> instanceReader() {
        return AutodetectResult::new;
    }

}
