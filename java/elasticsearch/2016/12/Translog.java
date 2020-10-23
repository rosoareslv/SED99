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

package org.elasticsearch.index.translog;

import org.apache.logging.log4j.message.ParameterizedMessage;
import org.apache.logging.log4j.util.Supplier;
import org.apache.lucene.index.Term;
import org.apache.lucene.index.TwoPhaseCommit;
import org.apache.lucene.store.AlreadyClosedException;
import org.apache.lucene.util.IOUtils;
import org.elasticsearch.action.index.IndexRequest;
import org.elasticsearch.common.UUIDs;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.common.bytes.ReleasablePagedBytesReference;
import org.elasticsearch.common.io.stream.ReleasableBytesStreamOutput;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.lease.Releasables;
import org.elasticsearch.common.lucene.uid.Versions;
import org.elasticsearch.common.util.BigArrays;
import org.elasticsearch.common.util.concurrent.ConcurrentCollections;
import org.elasticsearch.common.util.concurrent.ReleasableLock;
import org.elasticsearch.index.VersionType;
import org.elasticsearch.index.engine.Engine;
import org.elasticsearch.index.seqno.SequenceNumbersService;
import org.elasticsearch.index.shard.AbstractIndexShardComponent;
import org.elasticsearch.index.shard.IndexShardComponent;

import java.io.Closeable;
import java.io.EOFException;
import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.function.LongSupplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * A Translog is a per index shard component that records all non-committed index operations in a durable manner.
 * In Elasticsearch there is one Translog instance per {@link org.elasticsearch.index.engine.InternalEngine}. The engine
 * records the current translog generation {@link Translog#getGeneration()} in it's commit metadata using {@link #TRANSLOG_GENERATION_KEY}
 * to reference the generation that contains all operations that have not yet successfully been committed to the engines lucene index.
 * Additionally, since Elasticsearch 2.0 the engine also records a {@link #TRANSLOG_UUID_KEY} with each commit to ensure a strong association
 * between the lucene index an the transaction log file. This UUID is used to prevent accidential recovery from a transaction log that belongs to a
 * different engine.
 * <p>
 * Each Translog has only one translog file open at any time referenced by a translog generation ID. This ID is written to a <tt>translog.ckp</tt> file that is designed
 * to fit in a single disk block such that a write of the file is atomic. The checkpoint file is written on each fsync operation of the translog and records the number of operations
 * written, the current tranlogs file generation and it's fsynced offset in bytes.
 * </p>
 * <p>
 * When a translog is opened the checkpoint is use to retrieve the latest translog file generation and subsequently to open the last written file to recovery operations.
 * The {@link org.elasticsearch.index.translog.Translog.TranslogGeneration}, given when the translog is opened / constructed is compared against
 * the latest generation and all consecutive translog files singe the given generation and the last generation in the checkpoint will be recovered and preserved until the next
 * generation is committed using {@link Translog#commit()}. In the common case the translog file generation in the checkpoint and the generation passed to the translog on creation are
 * the same. The only situation when they can be different is when an actual translog commit fails in between {@link Translog#prepareCommit()} and {@link Translog#commit()}. In such a case
 * the currently being committed translog file will not be deleted since it's commit was not successful. Yet, a new/current translog file is already opened at that point such that there is more than
 * one translog file present. Such an uncommitted translog file always has a <tt>translog-${gen}.ckp</tt> associated with it which is an fsynced copy of the it's last <tt>translog.ckp</tt> such that in
 * disaster recovery last fsynced offsets, number of operation etc. are still preserved.
 * </p>
 */
public class Translog extends AbstractIndexShardComponent implements IndexShardComponent, Closeable, TwoPhaseCommit {

    /*
     * TODO
     *  - we might need something like a deletion policy to hold on to more than one translog eventually (I think sequence IDs needs this) but we can refactor as we go
     *  - use a simple BufferedOutputStream to write stuff and fold BufferedTranslogWriter into it's super class... the tricky bit is we need to be able to do random access reads even from the buffer
     *  - we need random exception on the FileSystem API tests for all this.
     *  - we need to page align the last write before we sync, we can take advantage of ensureSynced for this since we might have already fsynced far enough
     */
    public static final String TRANSLOG_GENERATION_KEY = "translog_generation";
    public static final String TRANSLOG_UUID_KEY = "translog_uuid";
    public static final String TRANSLOG_FILE_PREFIX = "translog-";
    public static final String TRANSLOG_FILE_SUFFIX = ".tlog";
    public static final String CHECKPOINT_SUFFIX = ".ckp";
    public static final String CHECKPOINT_FILE_NAME = "translog" + CHECKPOINT_SUFFIX;

    static final Pattern PARSE_STRICT_ID_PATTERN = Pattern.compile("^" + TRANSLOG_FILE_PREFIX + "(\\d+)(\\.tlog)$");

    // the list of translog readers is guaranteed to be in order of translog generation
    private final List<TranslogReader> readers = new ArrayList<>();
    // this is a concurrent set and is not protected by any of the locks. The main reason
    // is that is being accessed by two separate classes (additions & reading are done by Translog, remove by View when closed)
    private final Set<View> outstandingViews = ConcurrentCollections.newConcurrentSet();
    private BigArrays bigArrays;
    protected final ReleasableLock readLock;
    protected final ReleasableLock writeLock;
    private final Path location;
    private TranslogWriter current;

    private static final long NOT_SET_GENERATION = -1; // -1 is safe as it will not cause a translog deletion.

    private volatile long currentCommittingGeneration = NOT_SET_GENERATION;
    private volatile long lastCommittedTranslogFileGeneration = NOT_SET_GENERATION;
    private final AtomicBoolean closed = new AtomicBoolean();
    private final TranslogConfig config;
    private final LongSupplier globalCheckpointSupplier;
    private final String translogUUID;

    /**
     * Creates a new Translog instance. This method will create a new transaction log unless the given {@link TranslogGeneration} is
     * {@code null}. If the generation is {@code null} this method is destructive and will delete all files in the translog path given. If
     * the generation is not {@code null}, this method tries to open the given translog generation. The generation is treated as the last
     * generation referenced from already committed data. This means all operations that have not yet been committed should be in the
     * translog file referenced by this generation. The translog creation will fail if this generation can't be opened.
     *
     * @param config                   the configuration of this translog
     * @param translogGeneration       the translog generation to open
     * @param globalCheckpointSupplier a supplier for the global checkpoint
     */
    public Translog(
        final TranslogConfig config,
        final TranslogGeneration translogGeneration,
        final LongSupplier globalCheckpointSupplier) throws IOException {
        super(config.getShardId(), config.getIndexSettings());
        this.config = config;
        this.globalCheckpointSupplier = globalCheckpointSupplier;
        if (translogGeneration == null || translogGeneration.translogUUID == null) { // legacy case
            translogUUID = UUIDs.randomBase64UUID();
        } else {
            translogUUID = translogGeneration.translogUUID;
        }
        bigArrays = config.getBigArrays();
        ReadWriteLock rwl = new ReentrantReadWriteLock();
        readLock = new ReleasableLock(rwl.readLock());
        writeLock = new ReleasableLock(rwl.writeLock());
        this.location = config.getTranslogPath();
        Files.createDirectories(this.location);

        try {
            if (translogGeneration != null) {
                final Checkpoint checkpoint = readCheckpoint(location);
                final Path nextTranslogFile = location.resolve(getFilename(checkpoint.generation + 1));
                final Path currentCheckpointFile = location.resolve(getCommitCheckpointFileName(checkpoint.generation));
                // this is special handling for error condition when we create a new writer but we fail to bake
                // the newly written file (generation+1) into the checkpoint. This is still a valid state
                // we just need to cleanup before we continue
                // we hit this before and then blindly deleted the new generation even though we managed to bake it in and then hit this:
                // https://discuss.elastic.co/t/cannot-recover-index-because-of-missing-tanslog-files/38336 as an example
                //
                // For this to happen we must have already copied the translog.ckp file into translog-gen.ckp so we first check if that file exists
                // if not we don't even try to clean it up and wait until we fail creating it
                assert Files.exists(nextTranslogFile) == false || Files.size(nextTranslogFile) <= TranslogWriter.getHeaderLength(translogUUID) : "unexpected translog file: [" + nextTranslogFile + "]";
                if (Files.exists(currentCheckpointFile) // current checkpoint is already copied
                        && Files.deleteIfExists(nextTranslogFile)) { // delete it and log a warning
                    logger.warn("deleted previously created, but not yet committed, next generation [{}]. This can happen due to a tragic exception when creating a new generation", nextTranslogFile.getFileName());
                }
                this.readers.addAll(recoverFromFiles(translogGeneration, checkpoint));
                if (readers.isEmpty()) {
                    throw new IllegalStateException("at least one reader must be recovered");
                }
                boolean success = false;
                try {
                    current = createWriter(checkpoint.generation + 1);
                    this.lastCommittedTranslogFileGeneration = translogGeneration.translogFileGeneration;
                    success = true;
                } finally {
                    // we have to close all the recovered ones otherwise we leak file handles here
                    // for instance if we have a lot of tlog and we can't create the writer we keep on holding
                    // on to all the uncommitted tlog files if we don't close
                    if (success == false) {
                        IOUtils.closeWhileHandlingException(readers);
                    }
                }
            } else {
                IOUtils.rm(location);
                logger.debug("wipe translog location - creating new translog");
                Files.createDirectories(location);
                final long generation = 1;
                Checkpoint checkpoint = new Checkpoint(0, 0, generation, globalCheckpointSupplier.getAsLong());
                final Path checkpointFile = location.resolve(CHECKPOINT_FILE_NAME);
                Checkpoint.write(getChannelFactory(), checkpointFile, checkpoint, StandardOpenOption.WRITE, StandardOpenOption.CREATE_NEW);
                IOUtils.fsync(checkpointFile, false);
                current = createWriter(generation);
                this.lastCommittedTranslogFileGeneration = NOT_SET_GENERATION;

            }
            // now that we know which files are there, create a new current one.
        } catch (Exception e) {
            // close the opened translog files if we fail to create a new translog...
            IOUtils.closeWhileHandlingException(current);
            IOUtils.closeWhileHandlingException(readers);
            throw e;
        }
    }

    /** recover all translog files found on disk */
    private ArrayList<TranslogReader> recoverFromFiles(TranslogGeneration translogGeneration, Checkpoint checkpoint) throws IOException {
        boolean success = false;
        ArrayList<TranslogReader> foundTranslogs = new ArrayList<>();
        final Path tempFile = Files.createTempFile(location, TRANSLOG_FILE_PREFIX, TRANSLOG_FILE_SUFFIX); // a temp file to copy checkpoint to - note it must be in on the same FS otherwise atomic move won't work
        boolean tempFileRenamed = false;
        try (ReleasableLock lock = writeLock.acquire()) {
            logger.debug("open uncommitted translog checkpoint {}", checkpoint);
            final String checkpointTranslogFile = getFilename(checkpoint.generation);
            for (long i = translogGeneration.translogFileGeneration; i < checkpoint.generation; i++) {
                Path committedTranslogFile = location.resolve(getFilename(i));
                if (Files.exists(committedTranslogFile) == false) {
                    throw new IllegalStateException("translog file doesn't exist with generation: " + i + " lastCommitted: " + lastCommittedTranslogFileGeneration + " checkpoint: " + checkpoint.generation + " - translog ids must be consecutive");
                }
                final TranslogReader reader = openReader(committedTranslogFile, Checkpoint.read(location.resolve(getCommitCheckpointFileName(i))));
                foundTranslogs.add(reader);
                logger.debug("recovered local translog from checkpoint {}", checkpoint);
            }
            foundTranslogs.add(openReader(location.resolve(checkpointTranslogFile), checkpoint));
            Path commitCheckpoint = location.resolve(getCommitCheckpointFileName(checkpoint.generation));
            if (Files.exists(commitCheckpoint)) {
                Checkpoint checkpointFromDisk = Checkpoint.read(commitCheckpoint);
                if (checkpoint.equals(checkpointFromDisk) == false) {
                    throw new IllegalStateException("Checkpoint file " + commitCheckpoint.getFileName() + " already exists but has corrupted content expected: " + checkpoint + " but got: " + checkpointFromDisk);
                }
            } else {
                // we first copy this into the temp-file and then fsync it followed by an atomic move into the target file
                // that way if we hit a disk-full here we are still in an consistent state.
                Files.copy(location.resolve(CHECKPOINT_FILE_NAME), tempFile, StandardCopyOption.REPLACE_EXISTING);
                IOUtils.fsync(tempFile, false);
                Files.move(tempFile, commitCheckpoint, StandardCopyOption.ATOMIC_MOVE);
                tempFileRenamed = true;
                // we only fsync the directory the tempFile was already fsynced
                IOUtils.fsync(commitCheckpoint.getParent(), true);
            }
            success = true;
        } finally {
            if (success == false) {
                IOUtils.closeWhileHandlingException(foundTranslogs);
            }
            if (tempFileRenamed == false) {
                try {
                    Files.delete(tempFile);
                } catch (IOException ex) {
                    logger.warn((Supplier<?>) () -> new ParameterizedMessage("failed to delete temp file {}", tempFile), ex);
                }
            }
        }
        return foundTranslogs;
    }

    TranslogReader openReader(Path path, Checkpoint checkpoint) throws IOException {
        FileChannel channel = FileChannel.open(path, StandardOpenOption.READ);
        try {
            assert Translog.parseIdFromFileName(path) == checkpoint.generation : "expected generation: " + Translog.parseIdFromFileName(path) + " but got: " + checkpoint.generation;
            TranslogReader reader = TranslogReader.open(channel, path, checkpoint, translogUUID);
            channel = null;
            return reader;
        } finally {
            IOUtils.close(channel);
        }
    }

    /**
     * Extracts the translog generation from a file name.
     *
     * @throws IllegalArgumentException if the path doesn't match the expected pattern.
     */
    public static long parseIdFromFileName(Path translogFile) {
        final String fileName = translogFile.getFileName().toString();
        final Matcher matcher = PARSE_STRICT_ID_PATTERN.matcher(fileName);
        if (matcher.matches()) {
            try {
                return Long.parseLong(matcher.group(1));
            } catch (NumberFormatException e) {
                throw new IllegalStateException("number formatting issue in a file that passed PARSE_STRICT_ID_PATTERN: " + fileName + "]", e);
            }
        }
        throw new IllegalArgumentException("can't parse id from file: " + fileName);
    }

    /** Returns {@code true} if this {@code Translog} is still open. */
    public boolean isOpen() {
        return closed.get() == false;
    }

    @Override
    public void close() throws IOException {
        if (closed.compareAndSet(false, true)) {
            try (ReleasableLock lock = writeLock.acquire()) {
                try {
                    current.sync();
                } finally {
                    closeFilesIfNoPendingViews();
                }
            } finally {
                logger.debug("translog closed");
            }
        }
    }

    /**
     * Returns all translog locations as absolute paths.
     * These paths don't contain actual translog files they are
     * directories holding the transaction logs.
     */
    public Path location() {
        return location;
    }

    /**
     * Returns the generation of the current transaction log.
     */
    public long currentFileGeneration() {
        try (ReleasableLock lock = readLock.acquire()) {
            return current.getGeneration();
        }
    }

    /**
     * Returns the number of operations in the transaction files that aren't committed to lucene..
     */
    public int totalOperations() {
        return totalOperations(lastCommittedTranslogFileGeneration);
    }

    /**
     * Returns the size in bytes of the translog files that aren't committed to lucene.
     */
    public long sizeInBytes() {
        return sizeInBytes(lastCommittedTranslogFileGeneration);
    }

    /**
     * Returns the number of operations in the transaction files that aren't committed to lucene..
     */
    private int totalOperations(long minGeneration) {
        try (ReleasableLock ignored = readLock.acquire()) {
            ensureOpen();
            return Stream.concat(readers.stream(), Stream.of(current))
                    .filter(r -> r.getGeneration() >= minGeneration)
                    .mapToInt(BaseTranslogReader::totalOperations)
                    .sum();
        }
    }

    /**
     * Returns the size in bytes of the translog files that aren't committed to lucene.
     */
    private long sizeInBytes(long minGeneration) {
        try (ReleasableLock ignored = readLock.acquire()) {
            ensureOpen();
            return Stream.concat(readers.stream(), Stream.of(current))
                    .filter(r -> r.getGeneration() >= minGeneration)
                    .mapToLong(BaseTranslogReader::sizeInBytes)
                    .sum();
        }
    }


    /**
     * Creates a new translog for the specified generation.
     *
     * @param fileGeneration the translog generation
     * @return a writer for the new translog
     * @throws IOException if creating the translog failed
     */
    TranslogWriter createWriter(long fileGeneration) throws IOException {
        final TranslogWriter newFile;
        try {
            newFile = TranslogWriter.create(
                shardId,
                translogUUID,
                fileGeneration,
                location.resolve(getFilename(fileGeneration)),
                getChannelFactory(),
                config.getBufferSize(),
                globalCheckpointSupplier);
        } catch (final IOException e) {
            throw new TranslogException(shardId, "failed to create new translog file", e);
        }
        return newFile;
    }

    /**
     * Adds a delete / index operations to the transaction log.
     *
     * @see org.elasticsearch.index.translog.Translog.Operation
     * @see Index
     * @see org.elasticsearch.index.translog.Translog.Delete
     */
    public Location add(Operation operation) throws IOException {
        final ReleasableBytesStreamOutput out = new ReleasableBytesStreamOutput(bigArrays);
        try {
            final BufferedChecksumStreamOutput checksumStreamOutput = new BufferedChecksumStreamOutput(out);
            final long start = out.position();
            out.skip(Integer.BYTES);
            writeOperationNoSize(checksumStreamOutput, operation);
            final long end = out.position();
            final int operationSize = (int) (end - Integer.BYTES - start);
            out.seek(start);
            out.writeInt(operationSize);
            out.seek(end);
            final ReleasablePagedBytesReference bytes = out.bytes();
            try (ReleasableLock lock = readLock.acquire()) {
                ensureOpen();
                Location location = current.add(bytes);
                return location;
            }
        } catch (AlreadyClosedException | IOException ex) {
            try {
                closeOnTragicEvent(ex);
            } catch (Exception inner) {
                ex.addSuppressed(inner);
            }
            throw ex;
        } catch (Exception e) {
            try {
                closeOnTragicEvent(e);
            } catch (Exception inner) {
                e.addSuppressed(inner);
            }
            throw new TranslogException(shardId, "Failed to write operation [" + operation + "]", e);
        } finally {
            Releasables.close(out.bytes());
        }
    }

    /**
     * The a {@linkplain Location} that will sort after the {@linkplain Location} returned by the last write but before any locations which
     * can be returned by the next write.
     */
    public Location getLastWriteLocation() {
        try (ReleasableLock lock = readLock.acquire()) {
            /*
             * We use position = current - 1 and size = Integer.MAX_VALUE here instead of position current and size = 0 for two reasons:
             * 1. Translog.Location's compareTo doesn't actually pay attention to size even though it's equals method does.
             * 2. It feels more right to return a *position* that is before the next write's position rather than rely on the size.
             */
            return new Location(current.generation, current.sizeInBytes() - 1, Integer.MAX_VALUE);
        }
    }

    /**
     * The last synced checkpoint for this translog.
     *
     * @return the last synced checkpoint
     */
    public long getLastSyncedGlobalCheckpoint() {
        try (final ReleasableLock ignored = readLock.acquire()) {
            return current.getLastSyncedCheckpoint().globalCheckpoint;
        }
    }

    /**
     * Snapshots the current transaction log allowing to safely iterate over the snapshot.
     * Snapshots are fixed in time and will not be updated with future operations.
     */
    public Snapshot newSnapshot() {
        return createSnapshot(Long.MIN_VALUE);
    }

    private Snapshot createSnapshot(long minGeneration) {
        try (ReleasableLock ignored = readLock.acquire()) {
            ensureOpen();
            Snapshot[] snapshots = Stream.concat(readers.stream(), Stream.of(current))
                    .filter(reader -> reader.getGeneration() >= minGeneration)
                    .map(BaseTranslogReader::newSnapshot).toArray(Snapshot[]::new);
            return new MultiSnapshot(snapshots);
        }
    }

    /**
     * Returns a view into the current translog that is guaranteed to retain all current operations
     * while receiving future ones as well
     */
    public Translog.View newView() {
        try (ReleasableLock lock = readLock.acquire()) {
            ensureOpen();
            View view = new View(lastCommittedTranslogFileGeneration);
            outstandingViews.add(view);
            return view;
        }
    }

    /**
     * Sync's the translog.
     */
    public void sync() throws IOException {
        try (ReleasableLock lock = readLock.acquire()) {
            if (closed.get() == false) {
                current.sync();
            }
        } catch (Exception ex) {
            try {
                closeOnTragicEvent(ex);
            } catch (Exception inner) {
                ex.addSuppressed(inner);
            }
            throw ex;
        }
    }

    public boolean syncNeeded() {
        try (ReleasableLock lock = readLock.acquire()) {
            return current.syncNeeded();
        }
    }

    /** package private for testing */
    public static String getFilename(long generation) {
        return TRANSLOG_FILE_PREFIX + generation + TRANSLOG_FILE_SUFFIX;
    }

    static String getCommitCheckpointFileName(long generation) {
        return TRANSLOG_FILE_PREFIX + generation + CHECKPOINT_SUFFIX;
    }


    /**
     * Ensures that the given location has be synced / written to the underlying storage.
     *
     * @return Returns <code>true</code> iff this call caused an actual sync operation otherwise <code>false</code>
     */
    public boolean ensureSynced(Location location) throws IOException {
        try (ReleasableLock lock = readLock.acquire()) {
            if (location.generation == current.getGeneration()) { // if we have a new one it's already synced
                ensureOpen();
                return current.syncUpTo(location.translogLocation + location.size);
            }
        } catch (Exception ex) {
            try {
                closeOnTragicEvent(ex);
            } catch (Exception inner) {
                ex.addSuppressed(inner);
            }
            throw ex;
        }
        return false;
    }

    /**
     * Ensures that all locations in the given stream have been synced / written to the underlying storage.
     * This method allows for internal optimization to minimize the amount of fsync operations if multiple
     * locations must be synced.
     *
     * @return Returns <code>true</code> iff this call caused an actual sync operation otherwise <code>false</code>
     */
    public boolean ensureSynced(Stream<Location> locations) throws IOException {
        final Optional<Location> max = locations.max(Location::compareTo);
        // we only need to sync the max location since it will sync all other
        // locations implicitly
        if (max.isPresent()) {
            return ensureSynced(max.get());
        } else {
            return false;
        }
    }

    private void closeOnTragicEvent(Exception ex) {
        if (current.getTragicException() != null) {
            try {
                close();
            } catch (AlreadyClosedException inner) {
                // don't do anything in this case. The AlreadyClosedException comes from TranslogWriter and we should not add it as suppressed because
                // will contain the Exception ex as cause. See also https://github.com/elastic/elasticsearch/issues/15941
            } catch (Exception inner) {
                assert (ex != inner.getCause());
                ex.addSuppressed(inner);
            }
        }
    }

    /**
     * return stats
     */
    public TranslogStats stats() {
        // acquire lock to make the two numbers roughly consistent (no file change half way)
        try (ReleasableLock lock = readLock.acquire()) {
            return new TranslogStats(totalOperations(), sizeInBytes());
        }
    }

    public TranslogConfig getConfig() {
        return config;
    }

    /**
     * a view into the translog, capturing all translog file at the moment of creation
     * and updated with any future translog.
     */
    /**
     * a view into the translog, capturing all translog file at the moment of creation
     * and updated with any future translog.
     */
    public class View implements Closeable {

        AtomicBoolean closed = new AtomicBoolean();
        final long minGeneration;

        View(long minGeneration) {
            this.minGeneration = minGeneration;
        }

        /** this smallest translog generation in this view */
        public long minTranslogGeneration() {
            return minGeneration;
        }

        /**
         * The total number of operations in the view.
         */
        public int totalOperations() {
            return Translog.this.totalOperations(minGeneration);
        }

        /**
         * Returns the size in bytes of the files behind the view.
         */
        public long sizeInBytes() {
            return Translog.this.sizeInBytes(minGeneration);
        }

        /** create a snapshot from this view */
        public Snapshot snapshot() {
            ensureOpen();
            return Translog.this.createSnapshot(minGeneration);
        }

        void ensureOpen() {
            if (closed.get()) {
                throw new AlreadyClosedException("View is already closed");
            }
        }

        @Override
        public void close() throws IOException {
            if (closed.getAndSet(true) == false) {
                logger.trace("closing view starting at translog [{}]", minTranslogGeneration());
                boolean removed = outstandingViews.remove(this);
                assert removed : "View was never set but was supposed to be removed";
                trimUnreferencedReaders();
                closeFilesIfNoPendingViews();
            }
        }
    }


    public static class Location implements Comparable<Location> {

        public final long generation;
        public final long translogLocation;
        public final int size;

        Location(long generation, long translogLocation, int size) {
            this.generation = generation;
            this.translogLocation = translogLocation;
            this.size = size;
        }

        public String toString() {
            return "[generation: " + generation + ", location: " + translogLocation + ", size: " + size + "]";
        }

        @Override
        public int compareTo(Location o) {
            if (generation == o.generation) {
                return Long.compare(translogLocation, o.translogLocation);
            }
            return Long.compare(generation, o.generation);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o == null || getClass() != o.getClass()) {
                return false;
            }

            Location location = (Location) o;

            if (generation != location.generation) {
                return false;
            }
            if (translogLocation != location.translogLocation) {
                return false;
            }
            return size == location.size;

        }

        @Override
        public int hashCode() {
            int result = Long.hashCode(generation);
            result = 31 * result + Long.hashCode(translogLocation);
            result = 31 * result + size;
            return result;
        }
    }

    /**
     * A snapshot of the transaction log, allows to iterate over all the transaction log operations.
     */
    public interface Snapshot {

        /**
         * The total number of operations in the translog.
         */
        int totalOperations();

        /**
         * Returns the next operation in the snapshot or <code>null</code> if we reached the end.
         */
        Translog.Operation next() throws IOException;

    }

    /**
     * A generic interface representing an operation performed on the transaction log.
     * Each is associated with a type.
     */
    public interface Operation extends Writeable {
        enum Type {
            @Deprecated
            CREATE((byte) 1),
            INDEX((byte) 2),
            DELETE((byte) 3),
            NO_OP((byte) 4);

            private final byte id;

            Type(byte id) {
                this.id = id;
            }

            public byte id() {
                return this.id;
            }

            public static Type fromId(byte id) {
                switch (id) {
                    case 1:
                        return CREATE;
                    case 2:
                        return INDEX;
                    case 3:
                        return DELETE;
                    case 4:
                        return NO_OP;
                    default:
                        throw new IllegalArgumentException("No type mapped for [" + id + "]");
                }
            }
        }

        Type opType();

        long estimateSize();

        Source getSource();

        /**
         * Reads the type and the operation from the given stream. The operatino must be written with
         * {@link Operation#writeType(Operation, StreamOutput)}
         */
        static Operation readType(StreamInput input) throws IOException {
            Translog.Operation.Type type = Translog.Operation.Type.fromId(input.readByte());
            switch (type) {
                case CREATE:
                    // the deserialization logic in Index was identical to that of Create when create was deprecated
                    return new Index(input);
                case DELETE:
                    return new Delete(input);
                case INDEX:
                    return new Index(input);
                case NO_OP:
                    return new NoOp(input);
                default:
                    throw new IOException("No type for [" + type + "]");
            }
        }

        /**
         * Writes the type and translog operation to the given stream
         */
        static void writeType(Translog.Operation operation, StreamOutput output) throws IOException {
            output.writeByte(operation.opType().id());
            operation.writeTo(output);
        }

    }

    public static class Source {

        public final BytesReference source;
        public final String routing;
        public final String parent;

        public Source(BytesReference source, String routing, String parent) {
            this.source = source;
            this.routing = routing;
            this.parent = parent;
        }

    }

    public static class Index implements Operation {

        public static final int FORMAT_2_X = 6; // since 2.0-beta1 and 1.1
        public static final int FORMAT_AUTO_GENERATED_IDS = FORMAT_2_X + 1; // since 5.0.0-beta1
        public static final int FORMAT_SEQ_NO = FORMAT_AUTO_GENERATED_IDS + 1; // since 6.0.0
        public static final int SERIALIZATION_FORMAT = FORMAT_SEQ_NO;

        private final String id;
        private final long autoGeneratedIdTimestamp;
        private final String type;
        private long seqNo = SequenceNumbersService.UNASSIGNED_SEQ_NO;
        private long primaryTerm = 0;
        private final long version;
        private final VersionType versionType;
        private final BytesReference source;
        private final String routing;
        private final String parent;

        public Index(StreamInput in) throws IOException {
            final int format = in.readVInt(); // SERIALIZATION_FORMAT
            assert format >= FORMAT_2_X : "format was: " + format;
            id = in.readString();
            type = in.readString();
            source = in.readBytesReference();
            routing = in.readOptionalString();
            parent = in.readOptionalString();
            this.version = in.readLong();
            if (format < FORMAT_SEQ_NO) {
                in.readLong(); // timestamp
                in.readLong(); // ttl
            }
            this.versionType = VersionType.fromValue(in.readByte());
            assert versionType.validateVersionForWrites(this.version);
            if (format >= FORMAT_AUTO_GENERATED_IDS) {
                this.autoGeneratedIdTimestamp = in.readLong();
            } else {
                this.autoGeneratedIdTimestamp = IndexRequest.UNSET_AUTO_GENERATED_TIMESTAMP;
            }
            if (format >= FORMAT_SEQ_NO) {
                seqNo = in.readLong();
                primaryTerm = in.readLong();
            }
        }

        public Index(Engine.Index index, Engine.IndexResult indexResult) {
            this.id = index.id();
            this.type = index.type();
            this.source = index.source();
            this.routing = index.routing();
            this.parent = index.parent();
            this.seqNo = indexResult.getSeqNo();
            this.primaryTerm = index.primaryTerm();
            this.version = indexResult.getVersion();
            this.versionType = index.versionType();
            this.autoGeneratedIdTimestamp = index.getAutoGeneratedIdTimestamp();
        }

        public Index(String type, String id, byte[] source) {
            this.type = type;
            this.id = id;
            this.source = new BytesArray(source);
            this.seqNo = 0;
            version = Versions.MATCH_ANY;
            versionType = VersionType.INTERNAL;
            routing = null;
            parent = null;
            autoGeneratedIdTimestamp = -1;
        }

        @Override
        public Type opType() {
            return Type.INDEX;
        }

        @Override
        public long estimateSize() {
            return ((id.length() + type.length()) * 2) + source.length() + 12;
        }

        public String type() {
            return this.type;
        }

        public String id() {
            return this.id;
        }

        public String routing() {
            return this.routing;
        }

        public String parent() {
            return this.parent;
        }

        public BytesReference source() {
            return this.source;
        }

        public long seqNo() {
            return seqNo;
        }

        public long primaryTerm() {
            return primaryTerm;
        }

        public long version() {
            return this.version;
        }

        public VersionType versionType() {
            return versionType;
        }

        @Override
        public Source getSource() {
            return new Source(source, routing, parent);
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            out.writeVInt(SERIALIZATION_FORMAT);
            out.writeString(id);
            out.writeString(type);
            out.writeBytesReference(source);
            out.writeOptionalString(routing);
            out.writeOptionalString(parent);
            out.writeLong(version);

            out.writeByte(versionType.getValue());
            out.writeLong(autoGeneratedIdTimestamp);
            out.writeLong(seqNo);
            out.writeLong(primaryTerm);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o == null || getClass() != o.getClass()) {
                return false;
            }

            Index index = (Index) o;

            if (version != index.version ||
                    seqNo != index.seqNo ||
                    primaryTerm != index.primaryTerm ||
                    id.equals(index.id) == false ||
                    type.equals(index.type) == false ||
                    versionType != index.versionType ||
                    autoGeneratedIdTimestamp != index.autoGeneratedIdTimestamp ||
                    source.equals(index.source) == false) {
                    return false;
            }
            if (routing != null ? !routing.equals(index.routing) : index.routing != null) {
                return false;
            }
            return !(parent != null ? !parent.equals(index.parent) : index.parent != null);

        }

        @Override
        public int hashCode() {
            int result = id.hashCode();
            result = 31 * result + type.hashCode();
            result = 31 * result + Long.hashCode(seqNo);
            result = 31 * result + Long.hashCode(primaryTerm);
            result = 31 * result + Long.hashCode(version);
            result = 31 * result + versionType.hashCode();
            result = 31 * result + source.hashCode();
            result = 31 * result + (routing != null ? routing.hashCode() : 0);
            result = 31 * result + (parent != null ? parent.hashCode() : 0);
            result = 31 * result + Long.hashCode(autoGeneratedIdTimestamp);
            return result;
        }

        @Override
        public String toString() {
            return "Index{" +
                    "id='" + id + '\'' +
                    ", type='" + type + '\'' +
                    '}';
        }

        public long getAutoGeneratedIdTimestamp() {
            return autoGeneratedIdTimestamp;
        }

    }

    public static class Delete implements Operation {

        private static final int FORMAT_5_X = 2;
        private static final int FORMAT_SEQ_NO = FORMAT_5_X + 1;
        public static final int SERIALIZATION_FORMAT = FORMAT_SEQ_NO;

        private Term uid;
        private long seqNo = SequenceNumbersService.UNASSIGNED_SEQ_NO;
        private long primaryTerm = 0;
        private long version = Versions.MATCH_ANY;
        private VersionType versionType = VersionType.INTERNAL;

        public Delete(StreamInput in) throws IOException {
            final int format = in.readVInt();// SERIALIZATION_FORMAT
            assert format >= FORMAT_5_X : "format was: " + format;
            uid = new Term(in.readString(), in.readString());
            this.version = in.readLong();
            this.versionType = VersionType.fromValue(in.readByte());
            assert versionType.validateVersionForWrites(this.version);
            if (format >= FORMAT_SEQ_NO) {
                seqNo = in.readVLong();
                primaryTerm = in.readVLong();
            }
        }

        public Delete(Engine.Delete delete, Engine.DeleteResult deleteResult) {
            this(delete.uid(), deleteResult.getSeqNo(), delete.primaryTerm(), deleteResult.getVersion(), delete.versionType());
        }

        /** utility for testing */
        public Delete(Term uid) {
            this(uid, 0, 0, Versions.MATCH_ANY, VersionType.INTERNAL);
        }

        public Delete(Term uid, long seqNo, long primaryTerm, long version, VersionType versionType) {
            this.uid = uid;
            this.seqNo = seqNo;
            this.primaryTerm = primaryTerm;
            this.version = version;
            this.versionType = versionType;
        }

        @Override
        public Type opType() {
            return Type.DELETE;
        }

        @Override
        public long estimateSize() {
            return ((uid.field().length() + uid.text().length()) * 2) + 20;
        }

        public Term uid() {
            return this.uid;
        }

        public long seqNo() {
            return seqNo;
        }

        public long primaryTerm() {
            return primaryTerm;
        }

        public long version() {
            return this.version;
        }

        public VersionType versionType() {
            return this.versionType;
        }

        @Override
        public Source getSource() {
            throw new IllegalStateException("trying to read doc source from delete operation");
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            out.writeVInt(SERIALIZATION_FORMAT);
            out.writeString(uid.field());
            out.writeString(uid.text());
            out.writeLong(version);
            out.writeByte(versionType.getValue());
            out.writeVLong(seqNo);
            out.writeVLong(primaryTerm);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o == null || getClass() != o.getClass()) {
                return false;
            }

            Delete delete = (Delete) o;

            return version == delete.version &&
                    seqNo == delete.seqNo &&
                    primaryTerm == delete.primaryTerm &&
                    uid.equals(delete.uid) &&
                    versionType == delete.versionType;
        }

        @Override
        public int hashCode() {
            int result = uid.hashCode();
            result = 31 * result + Long.hashCode(seqNo);
            result = 31 * result + Long.hashCode(primaryTerm);
            result = 31 * result + Long.hashCode(version);
            result = 31 * result + versionType.hashCode();
            return result;
        }

        @Override
        public String toString() {
            return "Delete{" +
                    "uid=" + uid +
                    '}';
        }

    }

    public static class NoOp implements Operation {

        private final long seqNo;
        private final long primaryTerm;
        private final String reason;

        public long seqNo() {
            return seqNo;
        }

        public long primaryTerm() {
            return primaryTerm;
        }

        public String reason() {
            return reason;
        }

        NoOp(final StreamInput in) throws IOException {
            seqNo = in.readLong();
            primaryTerm = in.readLong();
            reason = in.readString();
        }

        public NoOp(final long seqNo, final long primaryTerm, final String reason) {
            assert seqNo > SequenceNumbersService.NO_OPS_PERFORMED;
            assert primaryTerm >= 0;
            assert reason != null;
            this.seqNo = seqNo;
            this.primaryTerm = primaryTerm;
            this.reason = reason;
        }

        @Override
        public void writeTo(StreamOutput out) throws IOException {
            out.writeLong(seqNo);
            out.writeLong(primaryTerm);
            out.writeString(reason);
        }

        @Override
        public Type opType() {
            return Type.NO_OP;
        }

        @Override
        public long estimateSize() {
            return 2 * reason.length() + 2 * Long.BYTES;
        }

        @Override
        public Source getSource() {
            throw new UnsupportedOperationException("source does not exist for a no-op");
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) {
                return true;
            }
            if (obj == null || getClass() != obj.getClass()) {
                return false;
            }
            final NoOp that = (NoOp) obj;
            return seqNo == that.seqNo && primaryTerm == that.primaryTerm && reason.equals(that.reason);
        }

        @Override
        public int hashCode() {
            return 31 * 31 * 31 + 31 * 31 * Long.hashCode(seqNo) + 31 * Long.hashCode(primaryTerm) + reason().hashCode();
        }

    }


    public enum Durability {
        /**
         * Async durability - translogs are synced based on a time interval.
         */
        ASYNC,
        /**
         * Request durability - translogs are synced for each high levle request (bulk, index, delete)
         */
        REQUEST;

    }

    private static void verifyChecksum(BufferedChecksumStreamInput in) throws IOException {
        // This absolutely must come first, or else reading the checksum becomes part of the checksum
        long expectedChecksum = in.getChecksum();
        long readChecksum = in.readInt() & 0xFFFF_FFFFL;
        if (readChecksum != expectedChecksum) {
            throw new TranslogCorruptedException("translog stream is corrupted, expected: 0x" +
                    Long.toHexString(expectedChecksum) + ", got: 0x" + Long.toHexString(readChecksum));
        }
    }

    /**
     * Reads a list of operations written with {@link #writeOperations(StreamOutput, List)}
     */
    public static List<Operation> readOperations(StreamInput input) throws IOException {
        ArrayList<Operation> operations = new ArrayList<>();
        int numOps = input.readInt();
        final BufferedChecksumStreamInput checksumStreamInput = new BufferedChecksumStreamInput(input);
        for (int i = 0; i < numOps; i++) {
            operations.add(readOperation(checksumStreamInput));
        }
        return operations;
    }

    static Translog.Operation readOperation(BufferedChecksumStreamInput in) throws IOException {
        final Translog.Operation operation;
        try {
            final int opSize = in.readInt();
            if (opSize < 4) { // 4byte for the checksum
                throw new TranslogCorruptedException("operation size must be at least 4 but was: " + opSize);
            }
            in.resetDigest(); // size is not part of the checksum!
            if (in.markSupported()) { // if we can we validate the checksum first
                // we are sometimes called when mark is not supported this is the case when
                // we are sending translogs across the network with LZ4 compression enabled - currently there is no way s
                // to prevent this unfortunately.
                in.mark(opSize);

                in.skip(opSize - 4);
                verifyChecksum(in);
                in.reset();
            }
            operation = Translog.Operation.readType(in);
            verifyChecksum(in);
        } catch (TranslogCorruptedException e) {
            throw e;
        } catch (EOFException e) {
            throw new TruncatedTranslogException("reached premature end of file, translog is truncated", e);
        }
        return operation;
    }

    /**
     * Writes all operations in the given iterable to the given output stream including the size of the array
     * use {@link #readOperations(StreamInput)} to read it back.
     */
    public static void writeOperations(StreamOutput outStream, List<Operation> toWrite) throws IOException {
        final ReleasableBytesStreamOutput out = new ReleasableBytesStreamOutput(BigArrays.NON_RECYCLING_INSTANCE);
        try {
            outStream.writeInt(toWrite.size());
            final BufferedChecksumStreamOutput checksumStreamOutput = new BufferedChecksumStreamOutput(out);
            for (Operation op : toWrite) {
                out.reset();
                final long start = out.position();
                out.skip(Integer.BYTES);
                writeOperationNoSize(checksumStreamOutput, op);
                long end = out.position();
                int operationSize = (int) (out.position() - Integer.BYTES - start);
                out.seek(start);
                out.writeInt(operationSize);
                out.seek(end);
                ReleasablePagedBytesReference bytes = out.bytes();
                bytes.writeTo(outStream);
            }
        } finally {
            Releasables.close(out.bytes());
        }

    }

    public static void writeOperationNoSize(BufferedChecksumStreamOutput out, Translog.Operation op) throws IOException {
        // This BufferedChecksumStreamOutput remains unclosed on purpose,
        // because closing it closes the underlying stream, which we don't
        // want to do here.
        out.resetDigest();
        Translog.Operation.writeType(op, out);
        long checksum = out.getChecksum();
        out.writeInt((int) checksum);
    }

    @Override
    public long prepareCommit() throws IOException {
        try (ReleasableLock lock = writeLock.acquire()) {
            ensureOpen();
            if (currentCommittingGeneration != NOT_SET_GENERATION) {
                throw new IllegalStateException("already committing a translog with generation: " + currentCommittingGeneration);
            }
            currentCommittingGeneration = current.getGeneration();
            TranslogReader currentCommittingTranslog = current.closeIntoReader();
            readers.add(currentCommittingTranslog);
            Path checkpoint = location.resolve(CHECKPOINT_FILE_NAME);
            assert Checkpoint.read(checkpoint).generation == currentCommittingTranslog.getGeneration();
            Path commitCheckpoint = location.resolve(getCommitCheckpointFileName(currentCommittingTranslog.getGeneration()));
            Files.copy(checkpoint, commitCheckpoint);
            IOUtils.fsync(commitCheckpoint, false);
            IOUtils.fsync(commitCheckpoint.getParent(), true);
            // create a new translog file - this will sync it and update the checkpoint data;
            current = createWriter(current.getGeneration() + 1);
            logger.trace("current translog set to [{}]", current.getGeneration());

        } catch (Exception e) {
            IOUtils.closeWhileHandlingException(this); // tragic event
            throw e;
        }
        return 0L;
    }

    @Override
    public long commit() throws IOException {
        try (ReleasableLock lock = writeLock.acquire()) {
            ensureOpen();
            if (currentCommittingGeneration == NOT_SET_GENERATION) {
                prepareCommit();
            }
            assert currentCommittingGeneration != NOT_SET_GENERATION;
            assert readers.stream().filter(r -> r.getGeneration() == currentCommittingGeneration).findFirst().isPresent()
                    : "reader list doesn't contain committing generation [" + currentCommittingGeneration + "]";
            lastCommittedTranslogFileGeneration = current.getGeneration(); // this is important - otherwise old files will not be cleaned up
            currentCommittingGeneration = NOT_SET_GENERATION;
            trimUnreferencedReaders();
        }
        return 0;
    }

    void trimUnreferencedReaders() {
        try (ReleasableLock ignored = writeLock.acquire()) {
            if (closed.get()) {
                // we're shutdown potentially on some tragic event - don't delete anything
                return;
            }
            long minReferencedGen = outstandingViews.stream().mapToLong(View::minTranslogGeneration).min().orElse(Long.MAX_VALUE);
            minReferencedGen = Math.min(lastCommittedTranslogFileGeneration, minReferencedGen);
            final long finalMinReferencedGen = minReferencedGen;
            List<TranslogReader> unreferenced = readers.stream().filter(r -> r.getGeneration() < finalMinReferencedGen).collect(Collectors.toList());
            for (final TranslogReader unreferencedReader : unreferenced) {
                Path translogPath = unreferencedReader.path();
                logger.trace("delete translog file - not referenced and not current anymore {}", translogPath);
                IOUtils.closeWhileHandlingException(unreferencedReader);
                IOUtils.deleteFilesIgnoringExceptions(translogPath,
                        translogPath.resolveSibling(getCommitCheckpointFileName(unreferencedReader.getGeneration())));
            }
            readers.removeAll(unreferenced);
        }
    }

    void closeFilesIfNoPendingViews() throws IOException {
        try (ReleasableLock ignored = writeLock.acquire()) {
            if (closed.get() && outstandingViews.isEmpty()) {
                logger.trace("closing files. translog is closed and there are no pending views");
                ArrayList<Closeable> toClose = new ArrayList<>(readers);
                toClose.add(current);
                IOUtils.close(toClose);
            }
        }
    }


    @Override
    public void rollback() throws IOException {
        ensureOpen();
        close();
    }

    /**
     * References a transaction log generation
     */
    public static final class TranslogGeneration {
        public final String translogUUID;
        public final long translogFileGeneration;

        public TranslogGeneration(String translogUUID, long translogFileGeneration) {
            this.translogUUID = translogUUID;
            this.translogFileGeneration = translogFileGeneration;
        }

    }

    /**
     * Returns the current generation of this translog. This corresponds to the latest uncommitted translog generation
     */
    public TranslogGeneration getGeneration() {
        try (ReleasableLock lock = writeLock.acquire()) {
            return new TranslogGeneration(translogUUID, currentFileGeneration());
        }
    }

    /**
     * Returns <code>true</code> iff the given generation is the current gbeneration of this translog
     */
    public boolean isCurrent(TranslogGeneration generation) {
        try (ReleasableLock lock = writeLock.acquire()) {
            if (generation != null) {
                if (generation.translogUUID.equals(translogUUID) == false) {
                    throw new IllegalArgumentException("commit belongs to a different translog: " + generation.translogUUID + " vs. " + translogUUID);
                }
                return generation.translogFileGeneration == currentFileGeneration();
            }
        }
        return false;
    }

    long getFirstOperationPosition() { // for testing
        return current.getFirstOperationOffset();
    }

    private void ensureOpen() {
        if (closed.get()) {
            throw new AlreadyClosedException("translog is already closed", current.getTragicException());
        }
    }

    /**
     * The number of currently open views
     */
    int getNumOpenViews() {
        return outstandingViews.size();
    }

    ChannelFactory getChannelFactory() {
        return FileChannel::open;
    }

    /**
     * If this {@code Translog} was closed as a side-effect of a tragic exception,
     * e.g. disk full while flushing a new segment, this returns the root cause exception.
     * Otherwise (no tragic exception has occurred) it returns null.
     */
    public Exception getTragicException() {
        return current.getTragicException();
    }

    /** Reads and returns the current checkpoint */
    static final Checkpoint readCheckpoint(final Path location) throws IOException {
        return Checkpoint.read(location.resolve(CHECKPOINT_FILE_NAME));
    }

    /**
     * Reads the sequence numbers global checkpoint from the translog checkpoint.
     *
     * @param location the location of the translog
     * @return the global checkpoint
     * @throws IOException if an I/O exception occurred reading the checkpoint
     */
    public static final long readGlobalCheckpoint(final Path location) throws IOException {
        return readCheckpoint(location).globalCheckpoint;
    }

    /**
     * Returns the translog uuid used to associate a lucene index with a translog.
     */
    public String getTranslogUUID() {
        return translogUUID;
    }

}
