/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.license.licensor.tools;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import org.elasticsearch.cli.Command;
import org.elasticsearch.cli.CommandTestCase;
import org.elasticsearch.cli.ExitCodes;
import org.elasticsearch.cli.UserError;
import org.elasticsearch.cli.MockTerminal;
import org.elasticsearch.cli.Terminal;
import org.elasticsearch.license.core.License;
import org.elasticsearch.license.licensor.TestUtils;
import org.elasticsearch.test.ESTestCase;
import org.junit.Before;

public class LicenseGenerationToolTests extends CommandTestCase {
    protected Path pubKeyPath = null;
    protected Path priKeyPath = null;

    @Before
    public void setup() throws Exception {
        pubKeyPath = getDataPath(TestUtils.PUBLIC_KEY_RESOURCE);
        priKeyPath = getDataPath(TestUtils.PRIVATE_KEY_RESOURCE);
    }

    @Override
    protected Command newCommand() {
        return new LicenseGeneratorTool();
    }

    public void testMissingKeyPaths() throws Exception {
        Path pub = createTempDir().resolve("pub");
        Path pri = createTempDir().resolve("pri");
        UserError e = expectThrows(UserError.class, () -> {
            execute("--publicKeyPath", pub.toString(), "--privateKeyPath", pri.toString());
        });
        assertTrue(e.getMessage(), e.getMessage().contains("pri does not exist"));
        assertEquals(ExitCodes.USAGE, e.exitCode);

        Files.createFile(pri);
        e = expectThrows(UserError.class, () -> {
            execute("--publicKeyPath", pub.toString(), "--privateKeyPath", pri.toString());
        });
        assertTrue(e.getMessage(), e.getMessage().contains("pub does not exist"));
        assertEquals(ExitCodes.USAGE, e.exitCode);
    }

    public void testMissingLicenseSpec() throws Exception {
        UserError e = expectThrows(UserError.class, () -> {
            execute("--publicKeyPath", pubKeyPath.toString(), "--privateKeyPath", priKeyPath.toString());
        });
        assertTrue(e.getMessage(), e.getMessage().contains("Must specify either --license or --licenseFile"));
        assertEquals(ExitCodes.USAGE, e.exitCode);
    }

    public void testLicenseSpecString() throws Exception {
        TestUtils.LicenseSpec inputLicenseSpec = TestUtils.generateRandomLicenseSpec(License.VERSION_CURRENT);
        String licenseSpecString = TestUtils.generateLicenseSpecString(inputLicenseSpec);
        String output = execute("--publicKeyPath", pubKeyPath.toString(),
                                "--privateKeyPath", priKeyPath.toString(),
                                "--license", licenseSpecString);
        License outputLicense = License.fromSource(output.getBytes(StandardCharsets.UTF_8));
        TestUtils.assertLicenseSpec(inputLicenseSpec, outputLicense);
    }

    public void testLicenseSpecFile() throws Exception {
        TestUtils.LicenseSpec inputLicenseSpec = TestUtils.generateRandomLicenseSpec(License.VERSION_CURRENT);
        String licenseSpecString = TestUtils.generateLicenseSpecString(inputLicenseSpec);
        Path licenseSpecFile = createTempFile();
        Files.write(licenseSpecFile, licenseSpecString.getBytes(StandardCharsets.UTF_8));
        String output = execute("--publicKeyPath", pubKeyPath.toString(),
                                "--privateKeyPath", priKeyPath.toString(),
                                "--licenseFile", licenseSpecFile.toString());
        License outputLicense = License.fromSource(output.getBytes(StandardCharsets.UTF_8));
        TestUtils.assertLicenseSpec(inputLicenseSpec, outputLicense);
    }
}
