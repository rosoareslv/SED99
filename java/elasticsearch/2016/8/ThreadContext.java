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
package org.elasticsearch.common.util.concurrent;

import org.apache.lucene.util.CloseableThreadLocal;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Setting.Property;
import org.elasticsearch.common.settings.Settings;

import java.io.Closeable;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A ThreadContext is a map of string headers and a transient map of keyed objects that are associated with
 * a thread. It allows to store and retrieve header information across method calls, network calls as well as threads spawned from a
 * thread that has a {@link ThreadContext} associated with. Threads spawned from a {@link org.elasticsearch.threadpool.ThreadPool} have out of the box
 * support for {@link ThreadContext} and all threads spawned will inherit the {@link ThreadContext} from the thread that it is forking from.".
 * Network calls will also preserve the senders headers automatically.
 * <p>
 * Consumers of ThreadContext usually don't need to interact with adding or stashing contexts. Every elasticsearch thread is managed by a thread pool or executor
 * being responsible for stashing and restoring the threads context. For instance if a network request is received, all headers are deserialized from the network
 * and directly added as the headers of the threads {@link ThreadContext} (see {@link #readHeaders(StreamInput)}. In order to not modify the context that is currently
 * active on this thread the network code uses a try/with pattern to stash it's current context, read headers into a fresh one and once the request is handled or a handler thread
 * is forked (which in turn inherits the context) it restores the previous context. For instance:
 * </p>
 * <pre>
 *     // current context is stashed and replaced with a default context
 *     try (StoredContext context = threadContext.stashContext()) {
 *         threadContext.readHeaders(in); // read headers into current context
 *         if (fork) {
 *             threadPool.execute(() -&gt; request.handle()); // inherits context
 *         } else {
 *             request.handle();
 *         }
 *     }
 *     // previous context is restored on StoredContext#close()
 * </pre>
 *
 */
public final class ThreadContext implements Closeable, Writeable {

    public static final String PREFIX = "request.headers";
    public static final Setting<Settings> DEFAULT_HEADERS_SETTING = Setting.groupSetting(PREFIX + ".", Property.NodeScope);
    private static final ThreadContextStruct DEFAULT_CONTEXT = new ThreadContextStruct();
    private final Map<String, String> defaultHeader;
    private final ContextThreadLocal threadLocal;

    /**
     * Creates a new ThreadContext instance
     * @param settings the settings to read the default request headers from
     */
    public ThreadContext(Settings settings) {
        Settings headers = DEFAULT_HEADERS_SETTING.get(settings);
        if (headers == null) {
            this.defaultHeader = Collections.emptyMap();
        } else {
            Map<String, String> defaultHeader = new HashMap<>();
            for (String key : headers.names()) {
                defaultHeader.put(key, headers.get(key));
            }
            this.defaultHeader = Collections.unmodifiableMap(defaultHeader);
        }
        threadLocal = new ContextThreadLocal();
    }

    @Override
    public void close() throws IOException {
        threadLocal.close();
    }

    /**
     * Removes the current context and resets a default context. The removed context can be
     * restored when closing the returned {@link StoredContext}
     */
    public StoredContext stashContext() {
        final ThreadContextStruct context = threadLocal.get();
        threadLocal.set(null);
        return () -> threadLocal.set(context);
    }

    /**
     * Removes the current context and resets a new context that contains a merge of the current headers and the given headers. The removed context can be
     * restored when closing the returned {@link StoredContext}. The merge strategy is that headers that are already existing are preserved unless they are defaults.
     */
    public StoredContext stashAndMergeHeaders(Map<String, String> headers) {
        final ThreadContextStruct context = threadLocal.get();
        Map<String, String> newHeader = new HashMap<>(headers);
        newHeader.putAll(context.requestHeaders);
        threadLocal.set(DEFAULT_CONTEXT.putHeaders(newHeader));
        return () -> threadLocal.set(context);
    }

    /**
     * Just like {@link #stashContext()} but no default context is set.
     */
    public StoredContext newStoredContext() {
        final ThreadContextStruct context = threadLocal.get();
        return () -> threadLocal.set(context);
    }

    @Override
    public void writeTo(StreamOutput out) throws IOException {
        threadLocal.get().writeTo(out, defaultHeader);
    }

    /**
     * Reads the headers from the stream into the current context
     */
    public void readHeaders(StreamInput in) throws IOException {
        threadLocal.set(new ThreadContext.ThreadContextStruct(in));
    }

    /**
     * Returns the header for the given key or <code>null</code> if not present
     */
    public String getHeader(String key) {
        String value = threadLocal.get().requestHeaders.get(key);
        if (value == null)  {
            return defaultHeader.get(key);
        }
        return value;
    }

    /**
     * Returns all of the request contexts headers
     */
    public Map<String, String> getHeaders() {
        HashMap<String, String> map = new HashMap<>(defaultHeader);
        map.putAll(threadLocal.get().requestHeaders);
        return Collections.unmodifiableMap(map);
    }

    /**
     * Get a copy of all <em>response</em> headers.
     *
     * @return Never {@code null}.
     */
    public Map<String, List<String>> getResponseHeaders() {
        Map<String, List<String>> responseHeaders = threadLocal.get().responseHeaders;
        HashMap<String, List<String>> map = new HashMap<>(responseHeaders.size());

        for (Map.Entry<String, List<String>> entry : responseHeaders.entrySet()) {
            map.put(entry.getKey(), Collections.unmodifiableList(entry.getValue()));
        }

        return Collections.unmodifiableMap(map);
    }

    /**
     * Copies all header key, value pairs into the current context
     */
    public void copyHeaders(Iterable<Map.Entry<String, String>> headers) {
        threadLocal.set(threadLocal.get().copyHeaders(headers));
    }

    /**
     * Puts a header into the context
     */
    public void putHeader(String key, String value) {
        threadLocal.set(threadLocal.get().putRequest(key, value));
    }

    /**
     * Puts all of the given headers into this context
     */
    public void putHeader(Map<String, String> header) {
        threadLocal.set(threadLocal.get().putHeaders(header));
    }

    /**
     * Puts a transient header object into this context
     */
    public void putTransient(String key, Object value) {
        threadLocal.set(threadLocal.get().putTransient(key, value));
    }

    /**
     * Returns a transient header object or <code>null</code> if there is no header for the given key
     */
    @SuppressWarnings("unchecked") // (T)object
    public <T> T getTransient(String key) {
        return (T) threadLocal.get().transientHeaders.get(key);
    }

    /**
     * Add the <em>unique</em> response {@code value} for the specified {@code key}.
     * <p>
     * Any duplicate {@code value} is ignored.
     */
    public void addResponseHeader(String key, String value) {
        threadLocal.set(threadLocal.get().putResponse(key, value));
    }

    /**
     * Saves the current thread context and wraps command in a Runnable that restores that context before running command. If
     * <code>command</code> has already been passed through this method then it is returned unaltered rather than wrapped twice.
     */
    public Runnable preserveContext(Runnable command) {
        if (command instanceof ContextPreservingAbstractRunnable) {
            return command;
        }
        if (command instanceof ContextPreservingRunnable) {
            return command;
        }
        if (command instanceof AbstractRunnable) {
            return new ContextPreservingAbstractRunnable((AbstractRunnable) command);
        }
        return new ContextPreservingRunnable(command);
    }

    /**
     * Unwraps a command that was previously wrapped by {@link #preserveContext(Runnable)}.
     */
    public Runnable unwrap(Runnable command) {
        if (command instanceof ContextPreservingAbstractRunnable) {
            return ((ContextPreservingAbstractRunnable) command).unwrap();
        }
        if (command instanceof ContextPreservingRunnable) {
            return ((ContextPreservingRunnable) command).unwrap();
        }
        return command;
    }

    @FunctionalInterface
    public interface StoredContext extends AutoCloseable {
        @Override
        void close();

        default void restore() {
            close();
        }
    }

    private static final class ThreadContextStruct {
        private final Map<String, String> requestHeaders;
        private final Map<String, Object> transientHeaders;
        private final Map<String, List<String>> responseHeaders;

        private ThreadContextStruct(StreamInput in) throws IOException {
            final int numRequest = in.readVInt();
            Map<String, String> requestHeaders = numRequest == 0 ? Collections.emptyMap() : new HashMap<>(numRequest);
            for (int i = 0; i < numRequest; i++) {
                requestHeaders.put(in.readString(), in.readString());
            }

            this.requestHeaders = requestHeaders;
            this.responseHeaders = in.readMapOfLists(StreamInput::readString, StreamInput::readString);
            this.transientHeaders = Collections.emptyMap();
        }

        private ThreadContextStruct(Map<String, String> requestHeaders,
                                    Map<String, List<String>> responseHeaders,
                                    Map<String, Object> transientHeaders) {
            this.requestHeaders = requestHeaders;
            this.responseHeaders = responseHeaders;
            this.transientHeaders = transientHeaders;
        }

        /**
         * This represents the default context and it should only ever be called by {@link #DEFAULT_CONTEXT}.
         */
        private ThreadContextStruct() {
            this(Collections.emptyMap(), Collections.emptyMap(), Collections.emptyMap());
        }

        private ThreadContextStruct putRequest(String key, String value) {
            Map<String, String> newRequestHeaders = new HashMap<>(this.requestHeaders);
            putSingleHeader(key, value, newRequestHeaders);
            return new ThreadContextStruct(newRequestHeaders, responseHeaders, transientHeaders);
        }

        private void putSingleHeader(String key, String value, Map<String, String> newHeaders) {
            if (newHeaders.putIfAbsent(key, value) != null) {
                throw new IllegalArgumentException("value for key [" + key + "] already present");
            }
        }

        private ThreadContextStruct putHeaders(Map<String, String> headers) {
            if (headers.isEmpty()) {
                return this;
            } else {
                final Map<String, String> newHeaders = new HashMap<>();
                for (Map.Entry<String, String> entry : headers.entrySet()) {
                    putSingleHeader(entry.getKey(), entry.getValue(), newHeaders);
                }
                newHeaders.putAll(this.requestHeaders);
                return new ThreadContextStruct(newHeaders, responseHeaders, transientHeaders);
            }
        }

        private ThreadContextStruct putResponse(String key, String value) {
            assert value != null;

            final Map<String, List<String>> newResponseHeaders = new HashMap<>(this.responseHeaders);
            final List<String> existingValues = newResponseHeaders.get(key);

            if (existingValues != null) {
                if (existingValues.contains(value)) {
                    return this;
                }

                final List<String> newValues = new ArrayList<>(existingValues);
                newValues.add(value);

                newResponseHeaders.put(key, Collections.unmodifiableList(newValues));
            } else {
                newResponseHeaders.put(key, Collections.singletonList(value));
            }

            return new ThreadContextStruct(requestHeaders, newResponseHeaders, transientHeaders);
        }

        private ThreadContextStruct putTransient(String key, Object value) {
            Map<String, Object> newTransient = new HashMap<>(this.transientHeaders);
            if (newTransient.putIfAbsent(key, value) != null) {
                throw new IllegalArgumentException("value for key [" + key + "] already present");
            }
            return new ThreadContextStruct(requestHeaders, responseHeaders, newTransient);
        }

        boolean isEmpty() {
            return requestHeaders.isEmpty() && responseHeaders.isEmpty() && transientHeaders.isEmpty();
        }

        private ThreadContextStruct copyHeaders(Iterable<Map.Entry<String, String>> headers) {
            Map<String, String> newHeaders = new HashMap<>();
            for (Map.Entry<String, String> header : headers) {
                newHeaders.put(header.getKey(), header.getValue());
            }
            return putHeaders(newHeaders);
        }

        private void writeTo(StreamOutput out, Map<String, String> defaultHeaders) throws IOException {
            final Map<String, String> requestHeaders;
            if (defaultHeaders.isEmpty()) {
                requestHeaders = this.requestHeaders;
            } else {
                requestHeaders = new HashMap<>(defaultHeaders);
                requestHeaders.putAll(this.requestHeaders);
            }

            out.writeVInt(requestHeaders.size());
            for (Map.Entry<String, String> entry : requestHeaders.entrySet()) {
                out.writeString(entry.getKey());
                out.writeString(entry.getValue());
            }

            out.writeMapOfLists(responseHeaders, StreamOutput::writeString, StreamOutput::writeString);
        }
    }

    private static class ContextThreadLocal extends CloseableThreadLocal<ThreadContextStruct> {
        private final AtomicBoolean closed = new AtomicBoolean(false);

        @Override
        public void set(ThreadContextStruct object) {
            try {
                if (object == DEFAULT_CONTEXT) {
                    super.set(null);
                } else {
                    super.set(object);
                }
            } catch (NullPointerException ex) {
                /* This is odd but CloseableThreadLocal throws a NPE if it was closed but still accessed.
                   to get a real exception we call ensureOpen() to tell the user we are already closed.*/
                ensureOpen();
                throw ex;
            }
        }

        @Override
        public ThreadContextStruct get() {
            try {
                ThreadContextStruct threadContextStruct = super.get();
                if (threadContextStruct != null) {
                    return threadContextStruct;
                }
                return DEFAULT_CONTEXT;
            } catch (NullPointerException ex) {
                /* This is odd but CloseableThreadLocal throws a NPE if it was closed but still accessed.
                   to get a real exception we call ensureOpen() to tell the user we are already closed.*/
                ensureOpen();
                throw ex;
            }
        }

        private void ensureOpen() {
            if (closed.get()) {
                throw new IllegalStateException("threadcontext is already closed");
            }
        }

        @Override
        public void close() {
            if (closed.compareAndSet(false, true)) {
                super.close();
            }
        }
    }

    /**
     * Wraps a Runnable to preserve the thread context.
     */
    private class ContextPreservingRunnable implements Runnable {
        private final Runnable in;
        private final ThreadContext.StoredContext ctx;

        private ContextPreservingRunnable(Runnable in) {
            ctx = newStoredContext();
            this.in = in;
        }

        @Override
        public void run() {
            boolean whileRunning = false;
            try (ThreadContext.StoredContext ignore = stashContext()){
                ctx.restore();
                whileRunning = true;
                in.run();
                whileRunning = false;
            } catch (IllegalStateException ex) {
                if (whileRunning || threadLocal.closed.get() == false) {
                    throw ex;
                }
                // if we hit an ISE here we have been shutting down
                // this comes from the threadcontext and barfs if
                // our threadpool has been shutting down
            }
        }

        @Override
        public String toString() {
            return in.toString();
        }

        public Runnable unwrap() {
            return in;
        }
    }

    /**
     * Wraps an AbstractRunnable to preserve the thread context.
     */
    private class ContextPreservingAbstractRunnable extends AbstractRunnable {
        private final AbstractRunnable in;
        private final ThreadContext.StoredContext ctx;

        private ContextPreservingAbstractRunnable(AbstractRunnable in) {
            ctx = newStoredContext();
            this.in = in;
        }

        @Override
        public boolean isForceExecution() {
            return in.isForceExecution();
        }

        @Override
        public void onAfter() {
            in.onAfter();
        }

        @Override
        public void onFailure(Exception e) {
            in.onFailure(e);
        }

        @Override
        public void onRejection(Exception e) {
            in.onRejection(e);
        }

        @Override
        protected void doRun() throws Exception {
            boolean whileRunning = false;
            try (ThreadContext.StoredContext ignore = stashContext()){
                ctx.restore();
                whileRunning = true;
                in.doRun();
                whileRunning = false;
            } catch (IllegalStateException ex) {
                if (whileRunning || threadLocal.closed.get() == false) {
                    throw ex;
                }
                // if we hit an ISE here we have been shutting down
                // this comes from the threadcontext and barfs if
                // our threadpool has been shutting down
            }
        }

        @Override
        public String toString() {
            return in.toString();
        }

        public AbstractRunnable unwrap() {
            return in;
        }
    }
}
