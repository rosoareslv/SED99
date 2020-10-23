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

package org.apache.hadoop.fs.s3a;

import java.io.IOException;
import java.net.URI;
import java.nio.file.AccessDeniedException;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.rules.Timeout;

import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSCredentialsProviderChain;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.auth.InstanceProfileCredentialsProvider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.apache.hadoop.fs.s3a.Constants.*;
import static org.apache.hadoop.fs.s3a.S3ATestConstants.*;
import static org.apache.hadoop.fs.s3a.S3AUtils.*;
import static org.junit.Assert.*;

/**
 * Tests for {@link Constants#AWS_CREDENTIALS_PROVIDER} logic.
 *
 */
public class ITestS3AAWSCredentialsProvider {
  private static final Logger LOG =
      LoggerFactory.getLogger(ITestS3AAWSCredentialsProvider.class);

  @Rule
  public Timeout testTimeout = new Timeout(1 * 60 * 1000);

  @Rule
  public ExpectedException exception = ExpectedException.none();

  /**
   * Declare what exception to raise, and the text which must be found
   * in it.
   * @param exceptionClass class of exception
   * @param text text in exception
   */
  private void expectException(Class<? extends Throwable> exceptionClass,
      String text) {
    exception.expect(exceptionClass);
    exception.expectMessage(text);
  }

  @Test
  public void testBadConfiguration() throws IOException {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER, "no.such.class");
    try {
      createFailingFS(conf);
    } catch (IOException e) {
      if (!(e.getCause() instanceof ClassNotFoundException)) {
        LOG.error("Unexpected nested cause: {} in {}", e.getCause(), e, e);
        throw e;
      }
    }
  }

  /**
   * Create a filesystem, expect it to fail by raising an IOException.
   * Raises an assertion exception if in fact the FS does get instantiated.
   * @param conf configuration
   * @throws IOException an expected exception.
   */
  private void createFailingFS(Configuration conf) throws IOException {
    S3AFileSystem fs = S3ATestUtils.createTestFileSystem(conf);
    fs.listStatus(new Path("/"));
    fail("Expected exception - got " + fs);
  }

  static class BadCredentialsProvider implements AWSCredentialsProvider {

    @SuppressWarnings("unused")
    public BadCredentialsProvider(URI name, Configuration conf) {
    }

    @Override
    public AWSCredentials getCredentials() {
      return new BasicAWSCredentials("bad_key", "bad_secret");
    }

    @Override
    public void refresh() {
    }
  }

  @Test
  public void testBadCredentials() throws Exception {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER, BadCredentialsProvider.class.getName());
    try {
      createFailingFS(conf);
    } catch (AccessDeniedException e) {
      // expected
    }
  }

  static class GoodCredentialsProvider extends AWSCredentialsProviderChain {

    @SuppressWarnings("unused")
    public GoodCredentialsProvider(URI name, Configuration conf) {
      super(new BasicAWSCredentialsProvider(conf.get(ACCESS_KEY),
          conf.get(SECRET_KEY)), new InstanceProfileCredentialsProvider());
    }
  }

  @Test
  public void testGoodProvider() throws Exception {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER, GoodCredentialsProvider.class.getName());
    S3ATestUtils.createTestFileSystem(conf);
  }

  @Test
  public void testAnonymousProvider() throws Exception {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER,
        AnonymousAWSCredentialsProvider.class.getName());
    Path testFile = new Path(
        conf.getTrimmed(KEY_CSVTEST_FILE, DEFAULT_CSVTEST_FILE));
    S3ATestUtils.useCSVDataEndpoint(conf);
    FileSystem fs = FileSystem.newInstance(testFile.toUri(), conf);
    assertNotNull(fs);
    assertTrue(fs instanceof S3AFileSystem);
    FileStatus stat = fs.getFileStatus(testFile);
    assertNotNull(stat);
    assertEquals(testFile, stat.getPath());
  }

  /**
   * A credential provider whose constructor signature doesn't match.
   */
  static class ConstructorSignatureErrorProvider
      implements AWSCredentialsProvider {

    @SuppressWarnings("unused")
    public ConstructorSignatureErrorProvider(String str) {
    }

    @Override
    public AWSCredentials getCredentials() {
      return null;
    }

    @Override
    public void refresh() {
    }
  }

  /**
   * A credential provider whose constructor raises an NPE.
   */
  static class ConstructorFailureProvider
      implements AWSCredentialsProvider {

    @SuppressWarnings("unused")
    public ConstructorFailureProvider() {
      throw new NullPointerException("oops");
    }

    @Override
    public AWSCredentials getCredentials() {
      return null;
    }

    @Override
    public void refresh() {
    }
  }

  @Test
  public void testProviderWrongClass() throws Exception {
    expectProviderInstantiationFailure(this.getClass().getName(),
        NOT_AWS_PROVIDER);
  }

  @Test
  public void testProviderNotAClass() throws Exception {
    expectProviderInstantiationFailure("NoSuchClass",
        "ClassNotFoundException");
  }

  private void expectProviderInstantiationFailure(String option,
      String expectedErrorText) throws IOException {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER, option);
    Path testFile = new Path(
        conf.getTrimmed(KEY_CSVTEST_FILE, DEFAULT_CSVTEST_FILE));
    expectException(IOException.class, expectedErrorText);
    URI uri = testFile.toUri();
    S3AUtils.createAWSCredentialProviderSet(uri, conf, uri);
  }

  @Test
  public void testProviderConstructorError() throws Exception {
    expectProviderInstantiationFailure(
        ConstructorSignatureErrorProvider.class.getName(),
        CONSTRUCTOR_EXCEPTION);
  }

  @Test
  public void testProviderFailureError() throws Exception {
    expectProviderInstantiationFailure(
        ConstructorFailureProvider.class.getName(),
        INSTANTIATION_EXCEPTION);
  }

  @Test
  public void testInstantiationChain() throws Throwable {
    Configuration conf = new Configuration();
    conf.set(AWS_CREDENTIALS_PROVIDER,
        TemporaryAWSCredentialsProvider.NAME
            + ", \t" + SimpleAWSCredentialsProvider.NAME
            + " ,\n " + AnonymousAWSCredentialsProvider.NAME);
    Path testFile = new Path(
        conf.getTrimmed(KEY_CSVTEST_FILE, DEFAULT_CSVTEST_FILE));

    URI uri = testFile.toUri();
    S3AUtils.createAWSCredentialProviderSet(uri, conf, uri);
  }

}
