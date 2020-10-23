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

package org.elasticsearch.index.seqno;

import java.util.Map;

/**
 * A utility class for handling sequence numbers.
 */
public class SequenceNumbers {

    public static final String LOCAL_CHECKPOINT_KEY = "local_checkpoint";
    public static final String MAX_SEQ_NO = "max_seq_no";

    /**
     * Reads the sequence number stats from the commit data (maximum sequence number and local checkpoint) and uses the specified global
     * checkpoint.
     *
     * @param globalCheckpoint the global checkpoint to use
     * @param commitData       the commit data
     * @return the sequence number stats
     */
    public static SeqNoStats loadSeqNoStatsFromLuceneCommit(
        final long globalCheckpoint,
        final Iterable<Map.Entry<String, String>> commitData) {
        long maxSeqNo = SequenceNumbersService.NO_OPS_PERFORMED;
        long localCheckpoint = SequenceNumbersService.NO_OPS_PERFORMED;

        for (final Map.Entry<String, String> entry : commitData) {
            final String key = entry.getKey();
            if (key.equals(SequenceNumbers.LOCAL_CHECKPOINT_KEY)) {
                assert localCheckpoint == SequenceNumbersService.NO_OPS_PERFORMED : localCheckpoint;
                localCheckpoint = Long.parseLong(entry.getValue());
            } else if (key.equals(SequenceNumbers.MAX_SEQ_NO)) {
                assert maxSeqNo == SequenceNumbersService.NO_OPS_PERFORMED : maxSeqNo;
                maxSeqNo = Long.parseLong(entry.getValue());
            }
        }

        return new SeqNoStats(maxSeqNo, localCheckpoint, globalCheckpoint);
    }

    /**
     * Compute the minimum of the given current minimum sequence number and the specified sequence number, accounting for the fact that the
     * current minimum sequence number could be {@link SequenceNumbersService#NO_OPS_PERFORMED} or
     * {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}. When the current minimum sequence number is not
     * {@link SequenceNumbersService#NO_OPS_PERFORMED} nor {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}, the specified sequence number
     * must not be {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}.
     *
     * @param minSeqNo the current minimum sequence number
     * @param seqNo the specified sequence number
     * @return the new minimum sequence number
     */
    public static long min(final long minSeqNo, final long seqNo) {
        if (minSeqNo == SequenceNumbersService.NO_OPS_PERFORMED) {
            return seqNo;
        } else if (minSeqNo == SequenceNumbersService.UNASSIGNED_SEQ_NO) {
            return seqNo;
        } else {
            if (seqNo == SequenceNumbersService.UNASSIGNED_SEQ_NO) {
                throw new IllegalArgumentException("sequence number must be assigned");
            }
            return Math.min(minSeqNo, seqNo);
        }
    }

    /**
     * Compute the maximum of the given current maximum sequence number and the specified sequence number, accounting for the fact that the
     * current maximum sequence number could be {@link SequenceNumbersService#NO_OPS_PERFORMED} or
     * {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}. When the current maximum sequence number is not
     * {@link SequenceNumbersService#NO_OPS_PERFORMED} nor {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}, the specified sequence number
     * must not be {@link SequenceNumbersService#UNASSIGNED_SEQ_NO}.
     *
     * @param maxSeqNo the current maximum sequence number
     * @param seqNo the specified sequence number
     * @return the new maximum sequence number
     */
    public static long max(final long maxSeqNo, final long seqNo) {
        if (maxSeqNo == SequenceNumbersService.NO_OPS_PERFORMED) {
            return seqNo;
        } else if (maxSeqNo == SequenceNumbersService.UNASSIGNED_SEQ_NO) {
            return seqNo;
        } else {
            if (seqNo == SequenceNumbersService.UNASSIGNED_SEQ_NO) {
                throw new IllegalArgumentException("sequence number must be assigned");
            }
            return Math.max(maxSeqNo, seqNo);
        }
    }

}
