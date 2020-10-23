package jenkins.model;

import hudson.Functions;
import hudson.init.InitMilestone;
import hudson.maven.MavenModuleSet;
import hudson.maven.MavenModuleSetBuild;
import hudson.model.FreeStyleBuild;
import hudson.model.FreeStyleProject;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Ignore;
import org.junit.Test;
import org.jvnet.hudson.test.ExtractResourceSCM;
import org.jvnet.hudson.test.Issue;
import org.jvnet.hudson.test.LoggerRule;
import org.jvnet.hudson.test.MockFolder;
import org.jvnet.hudson.test.RestartableJenkinsRule;
import org.jvnet.hudson.test.recipes.LocalData;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.logging.Level;
import java.util.stream.Stream;

import static org.hamcrest.CoreMatchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.junit.Assume.assumeFalse;

/**
 * Since JENKINS-50164, Jenkins#workspacesDir and Jenkins#buildsDir had their associated UI deleted.
 * So instead of configuring through the UI, we now have to use sysprops for this.
 * <p>
 * So this test class uses a {@link RestartableJenkinsRule} to check the behaviour of this sysprop being
 * present or not between two restarts.
 */
public class JenkinsBuildsAndWorkspacesDirectoriesTest {

    private static final String LOG_WHEN_CHANGING_BUILDS_DIR = "Changing builds directories from ";
    private static final String LOG_WHEN_CHANGING_WORKSPACES_DIR = "Changing workspaces directories from ";

    @Rule
    public RestartableJenkinsRule story = new RestartableJenkinsRule();

    @Rule
    public LoggerRule loggerRule = new LoggerRule();

    @Before
    public void before() {
        clearSystemProperties();
    }

    @After
    public void after() {
        clearSystemProperties();
    }

    private void clearSystemProperties() {
        Stream.of(Jenkins.BUILDS_DIR_PROP, Jenkins.WORKSPACES_DIR_PROP)
                .forEach(System::clearProperty);
    }

    @Issue("JENKINS-53284")
    @Test
    public void changeWorkspacesDirLog() throws Exception {
        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO).capture(1000);

        story.then(step -> {
            assertFalse(logWasFound(LOG_WHEN_CHANGING_WORKSPACES_DIR));
            setWorkspacesDirProperty("testdir1");
        });

        story.then(step -> {
            assertTrue(logWasFoundAtLevel(LOG_WHEN_CHANGING_WORKSPACES_DIR,
                                          Level.WARNING));
            setWorkspacesDirProperty("testdir2");
        });

        story.then(step -> {
            assertTrue(logWasFoundAtLevel(LOG_WHEN_CHANGING_WORKSPACES_DIR,
                                          Level.WARNING));
        });
    }

    @Issue("JENKINS-50164")
    @Test
    public void badValueForBuildsDir() {
        story.then((rule) -> {
            final List<String> badValues = new ArrayList<>(Arrays.asList(
                    "blah",
                    "$JENKINS_HOME",
                    "$JENKINS_HOME/builds",
                    "$ITEM_FULL_NAME",
                    "/path/to/builds",
                    "/invalid/$JENKINS_HOME",
                    "relative/ITEM_FULL_NAME"));
            if (!new File("/").canWrite()) {
                badValues.add("/foo/$ITEM_FULL_NAME");
                badValues.add("/$ITEM_FULLNAME");
            } // else perhaps running as root

            for (String badValue : badValues) {
                try {
                    Jenkins.checkRawBuildsDir(badValue);
                    fail(badValue + " should have been rejected");
                } catch (InvalidBuildsDir invalidBuildsDir) {
                    // expected failure
                }
            }
        });
    }

    @Issue("JENKINS-50164")
    @Test
    public void goodValueForBuildsDir() {
        story.then((rule) -> {
            final List<String> badValues = Arrays.asList(
                    "$JENKINS_HOME/foo/$ITEM_FULL_NAME",
                    "${ITEM_ROOTDIR}/builds");

            for (String goodValue : badValues) {
                Jenkins.checkRawBuildsDir(goodValue);
            }
        });
    }

    @Issue("JENKINS-50164")
    @Test
    public void jenkinsDoesNotStartWithBadSysProp() {

        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO)
                .capture(100);

        story.then(rule -> {
            assertTrue(story.j.getInstance().isDefaultBuildDir());
            setBuildsDirProperty("/bluh");
        });

        story.thenDoesNotStart();
    }

    @Issue("JENKINS-50164")
    @Test
    public void jenkinsDoesNotStartWithScrewedUpConfigXml() {

        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO)
                .capture(100);

        story.then(rule -> {

            assertTrue(story.j.getInstance().isDefaultBuildDir());

            // Now screw up the value by writing into the file directly, like one could do using external XML manipulation tools
            final File configFile = new File(rule.jenkins.getRootDir(), "config.xml");
            final String screwedUp = FileUtils.readFileToString(configFile).
                    replaceFirst("<buildsDir>.*</buildsDir>", "<buildsDir>eeeeeeeeek</buildsDir>");
            FileUtils.write(configFile, screwedUp);
        });

        story.thenDoesNotStart();
    }

    @Issue("JENKINS-50164")
    @Test
    public void buildsDir() throws Exception {
        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO)
                .capture(100);

        story.then(step -> {
                       assertFalse(logWasFound("Using non default builds directories"));
                   }
        );

        story.then(steps -> {
            assertTrue(story.j.getInstance().isDefaultBuildDir());
            setBuildsDirProperty("$JENKINS_HOME/plouf/$ITEM_FULL_NAME/bluh");
            assertFalse(JenkinsBuildsAndWorkspacesDirectoriesTest.this.logWasFound(LOG_WHEN_CHANGING_BUILDS_DIR));
        });

        story.then(step -> {
                       assertFalse(story.j.getInstance().isDefaultBuildDir());
                       assertEquals("$JENKINS_HOME/plouf/$ITEM_FULL_NAME/bluh", story.j.getInstance().getRawBuildsDir());
                       assertTrue(logWasFound("Changing builds directories from "));
                   }
        );

        story.then(step -> assertTrue(logWasFound("Using non default builds directories"))
        );
    }

    @Issue("JENKINS-50164")
    @Test
    public void workspacesDir() throws Exception {
        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO)
                .capture(1000);

        story.then(step -> assertFalse(logWasFound("Using non default workspaces directories")));

        story.then(step -> {
            assertTrue(story.j.getInstance().isDefaultWorkspaceDir());
            final String workspacesDir = "bluh";
            setWorkspacesDirProperty(workspacesDir);
            assertFalse(logWasFound("Changing workspaces directories from "));
        });

        story.then(step -> {
            assertFalse(story.j.getInstance().isDefaultWorkspaceDir());
            assertEquals("bluh", story.j.getInstance().getRawWorkspaceDir());
            assertTrue(logWasFound("Changing workspaces directories from "));
        });


        story.then(step -> {
                       assertFalse(story.j.getInstance().isDefaultWorkspaceDir());
                       assertTrue(logWasFound("Using non default workspaces directories"));
                   }
        );
    }

    @Ignore("TODO calling restart seems to break Surefire")
    @Issue("JENKINS-50164")
    @LocalData
    @Test
    public void fromPreviousCustomSetup() {

        assumeFalse("Default Windows lifecycle does not support restart.", Functions.isWindows());

        // check starting point and change config for next run
        final String newBuildsDirValueBySysprop = "/tmp/${ITEM_ROOTDIR}/bluh";
        story.then(j -> {
            assertEquals("${ITEM_ROOTDIR}/ze-previous-custom-builds", j.jenkins.getRawBuildsDir());
            setBuildsDirProperty(newBuildsDirValueBySysprop);
        });

        // Check the sysprop setting was taken in account
        story.then(j -> {
            assertEquals(newBuildsDirValueBySysprop, j.jenkins.getRawBuildsDir());

            // ** HACK AROUND JENKINS-50422: manually restarting ** //
            // Check the disk (cannot just restart normally with the rule, )
            assertThat(FileUtils.readFileToString(new File(j.jenkins.getRootDir(), "config.xml")),
                       containsString("<buildsDir>" + newBuildsDirValueBySysprop + "</buildsDir>"));

            String rootDirBeforeRestart = j.jenkins.getRootDir().toString();
            clearSystemProperties();
            j.jenkins.restart();

            int maxLoops = 50;
            while (j.jenkins.getInitLevel() != InitMilestone.COMPLETED && maxLoops-- > 0) {
                Thread.sleep(300);
            }

            assertEquals(rootDirBeforeRestart, j.jenkins.getRootDir().toString());
            assertThat(FileUtils.readFileToString(new File(j.jenkins.getRootDir(), "config.xml")),
                       containsString("<buildsDir>" + newBuildsDirValueBySysprop + "</buildsDir>"));
            assertEquals(newBuildsDirValueBySysprop, j.jenkins.getRawBuildsDir());
            // ** END HACK ** //
        });

    }

    private void setWorkspacesDirProperty(String workspacesDir) {
        System.setProperty(Jenkins.WORKSPACES_DIR_PROP, workspacesDir);
    }

    private void setBuildsDirProperty(String buildsDir) {
        System.setProperty(Jenkins.BUILDS_DIR_PROP, buildsDir);
    }

    private boolean logWasFound(String searched) {
        return loggerRule.getRecords().stream()
                .anyMatch(record -> record.getMessage().contains(searched));
    }

	private boolean logWasFoundAtLevel(String searched, Level level) {
		return loggerRule.getRecords().stream()
                .filter(record -> record.getMessage().contains(searched))
                .filter(record -> record.getLevel().equals(level)).count() > 0;
	}

    @Test
    @Issue("JENKINS-12251")
    public void testItemFullNameExpansion() throws Exception {
        loggerRule.record(Jenkins.class, Level.WARNING)
                .record(Jenkins.class, Level.INFO)
                .capture(1000);

        story.then(steps -> {
            assertTrue(story.j.getInstance().isDefaultBuildDir());
            assertTrue(story.j.getInstance().isDefaultWorkspaceDir());
            setBuildsDirProperty("${JENKINS_HOME}/test12251_builds/${ITEM_FULL_NAME}");
            setWorkspacesDirProperty("${JENKINS_HOME}/test12251_ws/${ITEM_FULL_NAME}");
        });

        story.then(steps -> {
            assertTrue(JenkinsBuildsAndWorkspacesDirectoriesTest.this.logWasFound("Changing builds directories from "));
            assertFalse(story.j.getInstance().isDefaultBuildDir());
            assertFalse(story.j.getInstance().isDefaultWorkspaceDir());

            // build a dummy project
            MavenModuleSet m = story.j.jenkins.createProject(MavenModuleSet.class, "p");
            m.setScm(new ExtractResourceSCM(getClass().getResource("/simple-projects.zip")));
            MavenModuleSetBuild b = m.scheduleBuild2(0).get();

            // make sure these changes are effective
            assertTrue(b.getWorkspace().getRemote().contains("test12251_ws"));
            assertTrue(b.getRootDir().toString().contains("test12251_builds"));
        });

    }

    @Test
    @Issue("JENKINS-17138")
    public void externalBuildDirectoryRenameDelete() throws Exception {

        // Hack to get String builds usable in lambda below
        final List<String> builds = new ArrayList<>();

        story.then(steps -> {
            builds.add(story.j.createTmpDir().toString());
            assertTrue(story.j.getInstance().isDefaultBuildDir());
            setBuildsDirProperty(builds.get(0) + "/${ITEM_FULL_NAME}");
        });

        story.then(steps -> {
            assertEquals(builds.get(0) + "/${ITEM_FULL_NAME}", story.j.jenkins.getRawBuildsDir());
            FreeStyleProject p = story.j.jenkins.createProject(MockFolder.class, "d").createProject(FreeStyleProject.class, "prj");
            FreeStyleBuild b = p.scheduleBuild2(0).get();
            File oldBuildDir = new File(builds.get(0), "d/prj");
            assertEquals(new File(oldBuildDir, b.getId()), b.getRootDir());
            assertTrue(b.getRootDir().isDirectory());
            p.renameTo("proj");
            File newBuildDir = new File(builds.get(0), "d/proj");
            assertEquals(new File(newBuildDir, b.getId()), b.getRootDir());
            assertTrue(b.getRootDir().isDirectory());
            p.delete();
            assertFalse(b.getRootDir().isDirectory());
        });
    }

}
