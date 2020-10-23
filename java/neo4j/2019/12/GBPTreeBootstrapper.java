/*
 * Copyright (c) 2002-2019 "Neo4j,"
 * Neo4j Sweden AB [http://neo4j.com]
 *
 * This file is part of Neo4j.
 *
 * Neo4j is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package org.neo4j.index.internal.gbptree;

import org.apache.commons.lang3.tuple.Pair;

import java.io.File;

import org.neo4j.io.fs.DefaultFileSystemAbstraction;
import org.neo4j.io.pagecache.PageCache;
import org.neo4j.io.pagecache.impl.SingleFilePageSwapperFactory;
import org.neo4j.io.pagecache.impl.muninn.MuninnPageCache;
import org.neo4j.io.pagecache.tracing.PageCacheTracer;
import org.neo4j.io.pagecache.tracing.cursor.context.EmptyVersionContextSupplier;
import org.neo4j.scheduler.JobScheduler;

import static org.neo4j.index.internal.gbptree.GBPTree.NO_HEADER_READER;
import static org.neo4j.index.internal.gbptree.GBPTree.NO_HEADER_WRITER;
import static org.neo4j.index.internal.gbptree.GBPTree.NO_MONITOR;
import static org.neo4j.index.internal.gbptree.RecoveryCleanupWorkCollector.ignore;
import static org.neo4j.io.pagecache.tracing.PageCacheTracer.NULL;

public class GBPTreeBootstrapper
{
    private final PageCache pageCache;
    private final LayoutBootstrapper layoutBootstrapper;
    private final boolean readOnly;
    private final PageCacheTracer pageCacheTracer;

    public GBPTreeBootstrapper( PageCache pageCache, LayoutBootstrapper layoutBootstrapper, boolean readOnly, PageCacheTracer pageCacheTracer )
    {
        this.pageCache = pageCache;
        this.layoutBootstrapper = layoutBootstrapper;
        this.readOnly = readOnly;
        this.pageCacheTracer = pageCacheTracer;
    }

    public Bootstrap bootstrapTree( File file )
    {
        try
        {
            // Get meta information about the tree
            MetaVisitor<?,?> metaVisitor = new MetaVisitor();
            GBPTreeStructure.visitHeader( pageCache, file, metaVisitor );
            Meta meta = metaVisitor.meta;
            Pair<TreeState,TreeState> statePair = metaVisitor.statePair;
            TreeState state = TreeStatePair.selectNewestValidState( statePair );

            // Create layout and treeNode from meta
            Layout<?,?> layout = layoutBootstrapper.create( file, pageCache, meta );
            GBPTree<?,?> tree = new GBPTree<>( pageCache, file, layout, meta.getPageSize(), NO_MONITOR, NO_HEADER_READER, NO_HEADER_WRITER, ignore(), readOnly,
                    pageCacheTracer );
            return new SuccessfulBootstrap( tree, layout, state, meta );
        }
        catch ( Exception e )
        {
            return new FailedBootstrap( e );
        }
    }

    public static PageCache pageCache( JobScheduler jobScheduler )
    {
        DefaultFileSystemAbstraction fs = new DefaultFileSystemAbstraction();
        SingleFilePageSwapperFactory swapper = new SingleFilePageSwapperFactory( fs );
        return new MuninnPageCache( swapper, 100, NULL, EmptyVersionContextSupplier.EMPTY, jobScheduler );
    }

    public interface Bootstrap
    {
        boolean isTree();
        GBPTree<?,?> getTree();
        Layout<?,?> getLayout();
        TreeState getState();
        Meta getMeta();
    }

    private static class FailedBootstrap implements Bootstrap
    {
        private final Throwable cause;

        FailedBootstrap( Throwable cause )
        {
            this.cause = cause;
        }

        @Override
        public boolean isTree()
        {
            return false;
        }

        @Override
        public GBPTree<?,?> getTree()
        {
            throw new IllegalStateException( "Bootstrap failed", cause );
        }

        @Override
        public Layout<?,?> getLayout()
        {
            throw new IllegalStateException( "Bootstrap failed", cause );
        }

        @Override
        public TreeState getState()
        {
            throw new IllegalStateException( "Bootstrap failed", cause );
        }

        @Override
        public Meta getMeta()
        {
            throw new IllegalStateException( "Bootstrap failed", cause );
        }
    }

    private static class SuccessfulBootstrap implements Bootstrap
    {
        private final GBPTree<?,?> tree;
        private final Layout<?,?> layout;
        private final TreeState state;
        private final Meta meta;

        SuccessfulBootstrap( GBPTree<?,?> tree, Layout<?,?> layout, TreeState state, Meta meta )
        {
            this.tree = tree;
            this.layout = layout;
            this.state = state;
            this.meta = meta;
        }

        @Override
        public boolean isTree()
        {
            return true;
        }

        @Override
        public GBPTree<?,?> getTree()
        {
            return tree;
        }

        @Override
        public Layout<?,?> getLayout()
        {
            return layout;
        }

        @Override
        public TreeState getState()
        {
            return state;
        }

        @Override
        public Meta getMeta()
        {
            return meta;
        }
    }

    private static class MetaVisitor<KEY,VALUE> extends GBPTreeVisitor.Adaptor<KEY,VALUE>
    {
        private Meta meta;
        private Pair<TreeState,TreeState> statePair;

        @Override
        public void meta( Meta meta )
        {
            this.meta = meta;
        }

        @Override
        public void treeState( Pair<TreeState,TreeState> statePair )
        {
            this.statePair = statePair;
        }
    }
}
