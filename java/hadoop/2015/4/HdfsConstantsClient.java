/**
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
package org.apache.hadoop.hdfs.protocol;

import org.apache.hadoop.classification.InterfaceAudience;

@InterfaceAudience.Private
public interface HdfsConstantsClient {
  /**
   * Generation stamp of blocks that pre-date the introduction
   * of a generation stamp.
   */
  long GRANDFATHER_GENERATION_STAMP = 0;
  /**
   * The inode id validation of lease check will be skipped when the request
   * uses GRANDFATHER_INODE_ID for backward compatibility.
   */
  long GRANDFATHER_INODE_ID = 0;
  byte BLOCK_STORAGE_POLICY_ID_UNSPECIFIED = 0;
  /**
   * A prefix put before the namenode URI inside the "service" field
   * of a delgation token, indicating that the URI is a logical (HA)
   * URI.
   */
  String HA_DT_SERVICE_PREFIX = "ha-";
  // The name of the SafeModeException. FileSystem should retry if it sees
  // the below exception in RPC
  String SAFEMODE_EXCEPTION_CLASS_NAME = "org.apache.hadoop.hdfs.server" +
      ".namenode.SafeModeException";
}
