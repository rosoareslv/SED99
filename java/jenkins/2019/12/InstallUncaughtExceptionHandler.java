package hudson.init.impl;

import hudson.init.Initializer;
import jenkins.model.Jenkins;
import jenkins.telemetry.impl.java11.MissingClassTelemetry;
import org.kohsuke.stapler.Stapler;
import org.kohsuke.stapler.WebApp;
import org.kohsuke.stapler.compression.CompressionFilter;

import javax.servlet.ServletException;
import java.io.EOFException;
import java.io.IOException;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Deals with exceptions that get thrown all the way up to the Stapler rendering layer.
 */
public class InstallUncaughtExceptionHandler {

    private static final Logger LOGGER = Logger.getLogger(InstallUncaughtExceptionHandler.class.getName());

    @Initializer
    public static void init(final Jenkins j) throws IOException {
        CompressionFilter.setUncaughtExceptionHandler(j.servletContext, (e, context, req, rsp) -> {
                if (rsp.isCommitted()) {
                    LOGGER.log(isEOFException(e) ? Level.FINE : Level.WARNING, null, e);
                    return;
                }
                req.setAttribute("javax.servlet.error.exception",e);
                try {
                    // If we have an exception, let's see if it's related with missing classes on Java 11. We reach
                    // here with a ClassNotFoundException in an action, for example. Setting the report here is the only
                    // way to catch the missing classes when the plugin uses Thread.currentThread().getContextClassLoader().loadClass
                    MissingClassTelemetry.reportExceptionInside(e);
                    WebApp.get(j.servletContext).getSomeStapler().invoke(req, rsp, j, "/oops");
                } catch (ServletException | IOException x) {
                    if (!Stapler.isSocketException(x)) {
                        throw x;
                    }
                }
        });
        try {
            Thread.setDefaultUncaughtExceptionHandler(new DefaultUncaughtExceptionHandler());
            LOGGER.log(Level.FINE, "Successfully installed a global UncaughtExceptionHandler.");
        }
        catch (SecurityException ex) {
            LOGGER.log(Level.SEVERE,
                                                       "Failed to set the default UncaughtExceptionHandler.  " +
                                                       "If any threads die due to unhandled coding errors then there will be no logging of this information.  " +
                                                       "The lack of this diagnostic information will make it harder to track down issues which will reduce the supportability of Jenkins.  " +
                                                       "It is highly recommended that you consult the documentation that comes with you servlet container on how to allow the " +
                                                       "`setDefaultUncaughtExceptionHandler` permission and enable it.", ex);
        }
    }

    private static boolean isEOFException(Throwable e) {
        if (e == null) {
            return false;
        } else if (e instanceof EOFException) {
            return true;
        } else {
            return isEOFException(e.getCause());
        }
    }

    /** An UncaughtExceptionHandler that just logs the exception */
    private static class DefaultUncaughtExceptionHandler implements Thread.UncaughtExceptionHandler {

        @Override
        public void uncaughtException(Thread t, Throwable ex) {
            // if this was an OutOfMemoryError then all bets about logging are off - but in the absence of anything else...
            LOGGER.log(Level.SEVERE,
                       "A thread (" + t.getName() + '/' + t.getId()
                                     + ") died unexpectedly due to an uncaught exception, this may leave your Jenkins in a bad way and is usually indicative of a bug in the code.",
                       ex);

            // If we have an exception, let's see if it's related with missing classes on Java 11. We reach
            // here with a ClassNotFoundException in an action, for example. Setting the report here is the only
            // way to catch the missing classes when the plugin uses Thread.currentThread().getContextClassLoader().loadClass
            MissingClassTelemetry.reportExceptionInside(ex);
        }

    }

    private InstallUncaughtExceptionHandler() {}
}
