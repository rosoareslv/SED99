/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.hadoop.fs.s3a;

import org.apache.hadoop.fs.StorageStatistics.CommonStatisticNames;

import java.util.HashMap;
import java.util.Map;

/**
 * Statistic which are collected in S3A.
 * These statistics are available at a low level in {@link S3AStorageStatistics}
 * and as metrics in {@link S3AInstrumentation}
 */
public enum Statistic {

  DIRECTORIES_CREATED("directories_created",
      "Total number of directories created through the object store."),
  DIRECTORIES_DELETED("directories_deleted",
      "Total number of directories deleted through the object store."),
  FILES_COPIED("files_copied",
      "Total number of files copied within the object store."),
  FILES_COPIED_BYTES("files_copied_bytes",
      "Total number of bytes copied within the object store."),
  FILES_CREATED("files_created",
      "Total number of files created through the object store."),
  FILES_DELETED("files_deleted",
      "Total number of files deleted from the object store."),
  IGNORED_ERRORS("ignored_errors", "Errors caught and ignored"),
  INVOCATION_COPY_FROM_LOCAL_FILE(CommonStatisticNames.OP_COPY_FROM_LOCAL_FILE,
      "Calls of copyFromLocalFile()"),
  INVOCATION_EXISTS(CommonStatisticNames.OP_EXISTS,
      "Calls of exists()"),
  INVOCATION_GET_FILE_STATUS(CommonStatisticNames.OP_GET_FILE_STATUS,
      "Calls of getFileStatus()"),
  INVOCATION_GLOB_STATUS(CommonStatisticNames.OP_GLOB_STATUS,
      "Calls of globStatus()"),
  INVOCATION_IS_DIRECTORY(CommonStatisticNames.OP_IS_DIRECTORY,
      "Calls of isDirectory()"),
  INVOCATION_IS_FILE(CommonStatisticNames.OP_IS_FILE,
      "Calls of isFile()"),
  INVOCATION_LIST_FILES(CommonStatisticNames.OP_LIST_FILES,
      "Calls of listFiles()"),
  INVOCATION_LIST_LOCATED_STATUS(CommonStatisticNames.OP_LIST_LOCATED_STATUS,
      "Calls of listLocatedStatus()"),
  INVOCATION_LIST_STATUS(CommonStatisticNames.OP_LIST_STATUS,
      "Calls of listStatus()"),
  INVOCATION_MKDIRS(CommonStatisticNames.OP_MKDIRS,
      "Calls of mkdirs()"),
  INVOCATION_RENAME(CommonStatisticNames.OP_RENAME,
      "Calls of rename()"),
  OBJECT_COPY_REQUESTS("object_copy_requests", "Object copy requests"),
  OBJECT_DELETE_REQUESTS("object_delete_requests", "Object delete requests"),
  OBJECT_LIST_REQUESTS("object_list_requests",
      "Number of object listings made"),
  OBJECT_CONTINUE_LIST_REQUESTS("object_continue_list_requests",
      "Number of continued object listings made"),
  OBJECT_METADATA_REQUESTS("object_metadata_requests",
      "Number of requests for object metadata"),
  OBJECT_MULTIPART_UPLOAD_ABORTED("object_multipart_aborted",
      "Object multipart upload aborted"),
  OBJECT_PUT_REQUESTS("object_put_requests",
      "Object put/multipart upload count"),
  OBJECT_PUT_BYTES("object_put_bytes", "number of bytes uploaded"),
  STREAM_ABORTED("stream_aborted",
      "Count of times the TCP stream was aborted"),
  STREAM_BACKWARD_SEEK_OPERATIONS("stream_backward_seek_pperations",
      "Number of executed seek operations which went backwards in a stream"),
  STREAM_CLOSED("streamClosed", "Count of times the TCP stream was closed"),
  STREAM_CLOSE_OPERATIONS("stream_close_operations",
      "Total count of times an attempt to close a data stream was made"),
  STREAM_FORWARD_SEEK_OPERATIONS("stream_forward_seek_operations",
      "Number of executed seek operations which went forward in a stream"),
  STREAM_OPENED("streamOpened",
      "Total count of times an input stream to object store was opened"),
  STREAM_READ_EXCEPTIONS("stream_read_exceptions",
      "Number of seek operations invoked on input streams"),
  STREAM_READ_FULLY_OPERATIONS("stream_read_fully_operations",
      "Count of readFully() operations in streams"),
  STREAM_READ_OPERATIONS("stream_read_operations",
      "Count of read() operations in streams"),
  STREAM_READ_OPERATIONS_INCOMPLETE("stream_read_operations_incomplete",
      "Count of incomplete read() operations in streams"),
  STREAM_SEEK_BYTES_BACKWARDS("stream_bytes_backwards_on_seek",
      "Count of bytes moved backwards during seek operations"),
  STREAM_SEEK_BYTES_READ("stream_bytes_read",
      "Count of bytes read during seek() in stream operations"),
  STREAM_SEEK_BYTES_SKIPPED("stream_bytes_skipped_on_seek",
      "Count of bytes skipped during forward seek operation"),
  STREAM_SEEK_OPERATIONS("stream_seek_operations",
      "Number of seek operations during stream IO."),
  STREAM_CLOSE_BYTES_READ("stream_bytes_read_in_close",
      "Count of bytes read when closing streams during seek operations."),
  STREAM_ABORT_BYTES_DISCARDED("stream_bytes_discarded_in_abort",
      "Count of bytes discarded by aborting the stream");

  private static final Map<String, Statistic> SYMBOL_MAP =
      new HashMap<>(Statistic.values().length);
  static {
    for (Statistic stat : values()) {
      SYMBOL_MAP.put(stat.getSymbol(), stat);
    }
  }

  Statistic(String symbol, String description) {
    this.symbol = symbol;
    this.description = description;
  }

  private final String symbol;
  private final String description;

  public String getSymbol() {
    return symbol;
  }

  /**
   * Get a statistic from a symbol.
   * @param symbol statistic to look up
   * @return the value or null.
   */
  public static Statistic fromSymbol(String symbol) {
    return SYMBOL_MAP.get(symbol);
  }

  public String getDescription() {
    return description;
  }

  /**
   * The string value is simply the symbol.
   * This makes this operation very low cost.
   * @return the symbol of this statistic.
   */
  @Override
  public String toString() {
    return symbol;
  }
}
