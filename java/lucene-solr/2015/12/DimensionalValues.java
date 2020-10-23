package org.apache.lucene.index;

import java.io.IOException;

import org.apache.lucene.document.DimensionalBinaryField;
import org.apache.lucene.document.DimensionalDoubleField;
import org.apache.lucene.document.DimensionalFloatField;
import org.apache.lucene.document.DimensionalIntField;
import org.apache.lucene.document.DimensionalLongField;
import org.apache.lucene.util.bkd.BKDWriter;

/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** Allows recursively visiting dimensional values indexed with {@link DimensionalIntField},
 *  {@link DimensionalFloatField}, {@link DimensionalLongField}, {@link DimensionalDoubleField}
 *  or {@link DimensionalBinaryField}.
 *
 *  @lucene.experimental */
public abstract class DimensionalValues {

  /** Maximum number of bytes for each dimension */
  public static final int MAX_NUM_BYTES = 16;

  /** Maximum number of dimensions */
  public static final int MAX_DIMENSIONS = BKDWriter.MAX_DIMS;

  /** Defautl constructor */
  protected DimensionalValues() {
  }

  /** Used by {@link #intersect} to check how each recursive cell corresponds to the query. */
  public enum Relation {
    /** Return this if the cell is fully contained by the query */
    CELL_INSIDE_QUERY,
    /** Return this if the cell and query do not overlap */
    CELL_OUTSIDE_QUERY,
    /** Return this if the cell partially overlapps the query */
    CELL_CROSSES_QUERY
  };

  /** We recurse the BKD tree, using a provided instance of this to guide the recursion.
   *
   * @lucene.experimental */
  public interface IntersectVisitor {
    /** Called for all docs in a leaf cell that's fully contained by the query.  The
     *  consumer should blindly accept the docID. */
    void visit(int docID) throws IOException;

    /** Called for all docs in a leaf cell that crosses the query.  The consumer
     *  should scrutinize the packedValue to decide whether to accept it. */
    void visit(int docID, byte[] packedValue) throws IOException;

    /** Called for non-leaf cells to test how the cell relates to the query, to
     *  determine how to further recurse down the treer. */
    Relation compare(byte[] minPackedValue, byte[] maxPackedValue);

    /** Notifies the caller that this many documents (from one block) are about
     *  to be visited */
    default void grow(int count) {};
  }

  /** Finds all documents and points matching the provided visitor.
   *  This method does not enforce live docs, so it's up to the caller
   *  to test whether each document is deleted, if necessary. */
  public abstract void intersect(String fieldName, IntersectVisitor visitor) throws IOException;
}
