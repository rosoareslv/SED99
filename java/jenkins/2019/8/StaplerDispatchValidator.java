/*
 * The MIT License
 *
 * Copyright (c) 2019 CloudBees, Inc.
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

package jenkins.security.stapler;

import com.google.common.annotations.VisibleForTesting;
import jenkins.model.Jenkins;
import jenkins.util.SystemProperties;
import org.apache.commons.io.IOUtils;
import org.kohsuke.accmod.Restricted;
import org.kohsuke.accmod.restrictions.NoExternalUse;
import org.kohsuke.stapler.CancelRequestHandlingException;
import org.kohsuke.stapler.DispatchValidator;
import org.kohsuke.stapler.StaplerRequest;
import org.kohsuke.stapler.StaplerResponse;
import org.kohsuke.stapler.WebApp;

import javax.annotation.CheckForNull;
import javax.annotation.Nonnull;
import javax.servlet.ServletContext;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Validates views dispatched by Stapler. This validation consists of two phases:
 * <ul>
 *     <li>Before views are loaded, the model class is checked for {@link StaplerViews}/{@link StaplerFragments} along
 *     with whitelist entries specified by the default views whitelist and the optionally defined whitelist specified
 *     by the system property {@code jenkins.security.stapler.StaplerDispatchValidator.whitelist}. Then,
 *     the model class's superclass and interfaces are recursively inspected adding views and fragments that do not
 *     conflict with the views and fragments already declared. This effectively allows model classes to override
 *     parent classes.</li>
 *     <li>Before views write any response output, this validator is checked to see if the view has declared itself
 *     dispatchable using the {@code l:view} Jelly tag. As this validation comes later, annotations will take
 *     precedence over the use or lack of a layout tag.</li>
 * </ul>
 * <p>Validation can be disabled by setting the system property
 * {@code jenkins.security.stapler.StaplerDispatchValidator.disabled=true} or setting {@link #DISABLED} to
 * {@code true} in the script console.</p>
 *
 * @since TODO
 */
@Restricted(NoExternalUse.class)
public class StaplerDispatchValidator implements DispatchValidator {

    private static final Logger LOGGER = Logger.getLogger(StaplerDispatchValidator.class.getName());
    private static final String ATTRIBUTE_NAME = StaplerDispatchValidator.class.getName() + ".status";
    private static final String ESCAPE_HATCH = StaplerDispatchValidator.class.getName() + ".disabled";
    /**
     * Escape hatch to disable dispatch validation.
     */
    public static /* script-console editable */ boolean DISABLED = SystemProperties.getBoolean(ESCAPE_HATCH);

    private static @CheckForNull Boolean setStatus(@Nonnull StaplerRequest req, @CheckForNull Boolean status) {
        if (status == null) {
            return null;
        }
        LOGGER.fine(() -> "Request dispatch set status to " + status + " for URL " + req.getPathInfo());
        req.setAttribute(ATTRIBUTE_NAME, status);
        return status;
    }

    private static @CheckForNull Boolean computeStatusIfNull(@Nonnull StaplerRequest req, @Nonnull Supplier<Boolean> statusIfNull) {
        Object requestStatus = req.getAttribute(ATTRIBUTE_NAME);
        return requestStatus instanceof Boolean ? (Boolean) requestStatus : setStatus(req, statusIfNull.get());
    }

    private final ValidatorCache cache;

    public StaplerDispatchValidator() {
        cache = new ValidatorCache();
        cache.load();
    }

    @Override
    public @CheckForNull Boolean isDispatchAllowed(@Nonnull StaplerRequest req, @Nonnull StaplerResponse rsp) {
        if (DISABLED) {
            return true;
        }
        Boolean status = computeStatusIfNull(req, () -> {
            if (rsp.getContentType() != null) {
                return true;
            }
            if (rsp.getStatus() >= 300) {
                return true;
            }
            return null;
        });
        LOGGER.finer(() -> req.getRequestURI() + " -> " + status);
        return status;
    }

    @Override
    public @CheckForNull Boolean isDispatchAllowed(@Nonnull StaplerRequest req, @Nonnull StaplerResponse rsp, @Nonnull String viewName, @CheckForNull Object node) {
        if (DISABLED) {
            return true;
        }
        Boolean status = computeStatusIfNull(req, () -> {
            if (viewName.equals("index")) {
                return true;
            }
            if (node == null) {
                return null;
            }
            return cache.find(node.getClass()).isViewValid(viewName);
        });
        LOGGER.finer(() -> "<" + req.getRequestURI() + ", " + viewName + ", " + node + "> -> " + status);
        return status;
    }

    @Override
    public void allowDispatch(@Nonnull StaplerRequest req, @Nonnull StaplerResponse rsp) {
        if (DISABLED) {
            return;
        }
        setStatus(req, true);
    }

    @Override
    public void requireDispatchAllowed(@Nonnull StaplerRequest req, @Nonnull StaplerResponse rsp) throws CancelRequestHandlingException {
        if (DISABLED) {
            return;
        }
        Boolean status = isDispatchAllowed(req, rsp);
        if (status == null || !status) {
            LOGGER.fine(() -> "Cancelling dispatch for " + req.getRequestURI());
            throw new CancelRequestHandlingException();
        }
    }

    @VisibleForTesting
    static StaplerDispatchValidator getInstance(@Nonnull ServletContext context) {
        return (StaplerDispatchValidator) WebApp.get(context).getDispatchValidator();
    }

    @VisibleForTesting
    void loadWhitelist(@Nonnull InputStream in) throws IOException {
        cache.loadWhitelist(IOUtils.readLines(in));
    }

    private static class ValidatorCache {
        private final Map<String, Validator> validators = new HashMap<>();
        private final ReadWriteLock lock = new ReentrantReadWriteLock();

        // provided as alternative to ConcurrentHashMap.computeIfAbsent which doesn't allow for recursion in the supplier
        // see https://stackoverflow.com/q/28840047
        private @Nonnull Validator computeIfAbsent(@Nonnull String className, @Nonnull Function<String, Validator> constructor) {
            lock.readLock().lock();
            try {
                if (validators.containsKey(className)) {
                    // cached value
                    return validators.get(className);
                }
            } finally {
                lock.readLock().unlock();
            }
            lock.writeLock().lock();
            try {
                if (validators.containsKey(className)) {
                    // cached between readLock.unlock and writeLock.lock
                    return validators.get(className);
                }
                Validator value = constructor.apply(className);
                validators.put(className, value);
                return value;
            } finally {
                lock.writeLock().unlock();
            }
        }

        private @Nonnull Validator find(@Nonnull Class<?> clazz) {
            return computeIfAbsent(clazz.getName(), name -> create(clazz));
        }

        private @Nonnull Validator find(@Nonnull String className) {
            return computeIfAbsent(className, this::create);
        }

        private @Nonnull Collection<Validator> findParents(@Nonnull Class<?> clazz) {
            List<Validator> parents = new ArrayList<>();
            Class<?> superclass = clazz.getSuperclass();
            if (superclass != null) {
                parents.add(find(superclass));
            }
            for (Class<?> iface : clazz.getInterfaces()) {
                parents.add(find(iface));
            }
            return parents;
        }

        private @Nonnull Validator create(@Nonnull Class<?> clazz) {
            Set<String> allowed = new HashSet<>();
            StaplerViews views = clazz.getDeclaredAnnotation(StaplerViews.class);
            if (views != null) {
                allowed.addAll(Arrays.asList(views.value()));
            }
            Set<String> denied = new HashSet<>();
            StaplerFragments fragments = clazz.getDeclaredAnnotation(StaplerFragments.class);
            if (fragments != null) {
                denied.addAll(Arrays.asList(fragments.value()));
            }
            return new Validator(() -> findParents(clazz), allowed, denied);
        }

        private @Nonnull Validator create(@Nonnull String className) {
            ClassLoader loader = Jenkins.get().pluginManager.uberClassLoader;
            return new Validator(() -> {
                try {
                    return findParents(loader.loadClass(className));
                } catch (ClassNotFoundException e) {
                    LOGGER.log(Level.WARNING, e, () -> "Could not load class " + className + " to validate views");
                    return Collections.emptySet();
                }
            });
        }

        private void load() {
            try {
                try (InputStream is = Validator.class.getResourceAsStream("default-views-whitelist.txt")) {
                    loadWhitelist(IOUtils.readLines(is, StandardCharsets.UTF_8));
                }
            } catch (IOException e) {
                LOGGER.log(Level.WARNING, "Could not load default views whitelist", e);
            }
            String whitelist = SystemProperties.getString(StaplerDispatchValidator.class.getName() + ".whitelist");
            Path configFile = whitelist != null ? Paths.get(whitelist) : Jenkins.get().getRootDir().toPath().resolve("stapler-views-whitelist.txt");
            if (Files.exists(configFile)) {
                try {
                    loadWhitelist(Files.readAllLines(configFile));
                } catch (IOException e) {
                    LOGGER.log(Level.WARNING, e, () -> "Could not load user defined whitelist from " + configFile);
                }
            }
        }

        private void loadWhitelist(@Nonnull List<String> whitelistLines) {
            for (String line : whitelistLines) {
                if (line.matches("#.*|\\s*")) {
                    // commented line
                    continue;
                }
                String[] parts = line.split("\\s+");
                if (parts.length < 2) {
                    // invalid input format
                    LOGGER.warning(() -> "Cannot update validator with malformed line: " + line);
                    continue;
                }
                Validator validator = find(parts[0]);
                for (int i = 1; i < parts.length; i++) {
                    String view = parts[i];
                    if (view.startsWith("!")) {
                        validator.denyView(view.substring(1));
                    } else {
                        validator.allowView(view);
                    }
                }
            }
        }

        private class Validator {
            // lazy load parents to avoid trying to load potentially unavailable classes
            private final Supplier<Collection<Validator>> parentsSupplier;
            private volatile Collection<Validator> parents;
            private final Set<String> allowed = ConcurrentHashMap.newKeySet();
            private final Set<String> denied = ConcurrentHashMap.newKeySet();

            private Validator(@Nonnull Supplier<Collection<Validator>> parentsSupplier) {
                this.parentsSupplier = parentsSupplier;
            }

            private Validator(@Nonnull Supplier<Collection<Validator>> parentsSupplier, @Nonnull Collection<String> allowed, @Nonnull Collection<String> denied) {
                this(parentsSupplier);
                this.allowed.addAll(allowed);
                this.denied.addAll(denied);
            }

            private @Nonnull Collection<Validator> getParents() {
                if (parents == null) {
                    synchronized (this) {
                        if (parents == null) {
                            parents = parentsSupplier.get();
                        }
                    }
                }
                return parents;
            }

            private @CheckForNull Boolean isViewValid(@Nonnull String viewName) {
                if (allowed.contains(viewName)) {
                    return Boolean.TRUE;
                }
                if (denied.contains(viewName)) {
                    return Boolean.FALSE;
                }
                for (Validator parent : getParents()) {
                    Boolean result = parent.isViewValid(viewName);
                    if (result != null) {
                        return result;
                    }
                }
                return null;
            }

            private void allowView(@Nonnull String viewName) {
                allowed.add(viewName);
            }

            private void denyView(@Nonnull String viewName) {
                denied.add(viewName);
            }
        }
    }
}
