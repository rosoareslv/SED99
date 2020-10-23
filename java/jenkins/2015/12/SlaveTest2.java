/*
 * The MIT License
 *
 * Copyright (c) 2015 CloudBees, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
package hudson.model;

import java.net.MalformedURLException;
import java.net.URLEncoder;
import javax.annotation.Nonnull;
import org.junit.Rule;
import org.junit.Test;
import org.jvnet.hudson.test.JenkinsRule;

import static org.junit.Assert.*;
import org.jvnet.hudson.test.Issue;

/**
 * Tests for the {@link Slave} class.
 * There is also a Groovy implementation of such test file, hence the class name 
 * has an index.
 * @author Oleg Nenashev
 */
public class SlaveTest2 {
    
    @Rule
    public JenkinsRule rule = new JenkinsRule();
    
    @Test
    @Issue("SECURITY-195")
    public void shouldNotEscapeJnlpSlavesResources() throws Exception {
        Slave slave = rule.createSlave();
        
        // Spot-check correct requests
        assertJnlpJarUrlIsAllowed(slave, "slave.jar");
        assertJnlpJarUrlIsAllowed(slave, "remoting.jar");
        assertJnlpJarUrlIsAllowed(slave, "jenkins-cli.jar");
        assertJnlpJarUrlIsAllowed(slave, "hudson-cli.jar");
        
        // Check that requests to other WEB-INF contents fail
        assertJnlpJarUrlFails(slave, "web.xml");
        assertJnlpJarUrlFails(slave, "web.xml");
        assertJnlpJarUrlFails(slave, "classes/bundled-plugins.txt");
        assertJnlpJarUrlFails(slave, "classes/dependencies.txt");
        assertJnlpJarUrlFails(slave, "plugins/ant.hpi");
        assertJnlpJarUrlFails(slave, "nonexistentfolder/something.txt");
        
        // Try various kinds of folder escaping (SECURITY-195)
        assertJnlpJarUrlFails(slave, "../");
        assertJnlpJarUrlFails(slave, "..");
        assertJnlpJarUrlFails(slave, "..\\");
        assertJnlpJarUrlFails(slave, "../foo/bar");
        assertJnlpJarUrlFails(slave, "..\\foo\\bar");
        assertJnlpJarUrlFails(slave, "foo/../../bar");
        assertJnlpJarUrlFails(slave, "./../foo/bar");
    }
    
    private void assertJnlpJarUrlFails(@Nonnull Slave slave, @Nonnull String url) throws Exception {
        // Raw access to API
        Slave.JnlpJar jnlpJar = slave.getComputer().getJnlpJars(url);
        try {
            jnlpJar.getURL();
        } catch (MalformedURLException ex) {
            // we expect the exception here
            return;
        }
        fail("Expected the MalformedURLException for " + url);
    }
    
    private void assertJnlpJarUrlIsAllowed(@Nonnull Slave slave, @Nonnull String url) throws Exception {
        // Raw access to API
        Slave.JnlpJar jnlpJar = slave.getComputer().getJnlpJars(url);
        assertNotNull(jnlpJar.getURL());
 
        
        // Access from a Web client
        JenkinsRule.WebClient client = rule.createWebClient();
        assertEquals(200, client.getPage(client.getContextPath() + "jnlpJars/" + URLEncoder.encode(url, "UTF-8")).getWebResponse().getStatusCode());
        assertEquals(200, client.getPage(jnlpJar.getURL()).getWebResponse().getStatusCode());
    }
}
