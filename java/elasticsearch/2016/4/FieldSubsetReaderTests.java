/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.shield.authz.accesscontrol;

import org.apache.lucene.analysis.MockAnalyzer;
import org.apache.lucene.document.BinaryDocValuesField;
import org.apache.lucene.document.Document;
import org.apache.lucene.document.Field;
import org.apache.lucene.document.FieldType;
import org.apache.lucene.document.IntPoint;
import org.apache.lucene.document.NumericDocValuesField;
import org.apache.lucene.document.SortedDocValuesField;
import org.apache.lucene.document.SortedNumericDocValuesField;
import org.apache.lucene.document.SortedSetDocValuesField;
import org.apache.lucene.document.StoredField;
import org.apache.lucene.document.StringField;
import org.apache.lucene.document.TextField;
import org.apache.lucene.index.DirectoryReader;
import org.apache.lucene.index.FieldInfos;
import org.apache.lucene.index.Fields;
import org.apache.lucene.index.IndexWriter;
import org.apache.lucene.index.IndexWriterConfig;
import org.apache.lucene.index.LeafReader;
import org.apache.lucene.index.NoMergePolicy;
import org.apache.lucene.index.PointValues;
import org.apache.lucene.index.PointValues.IntersectVisitor;
import org.apache.lucene.index.PointValues.Relation;
import org.apache.lucene.index.SortedNumericDocValues;
import org.apache.lucene.index.SortedSetDocValues;
import org.apache.lucene.index.Term;
import org.apache.lucene.index.Terms;
import org.apache.lucene.index.TermsEnum;
import org.apache.lucene.index.TermsEnum.SeekStatus;
import org.apache.lucene.store.Directory;
import org.apache.lucene.util.BytesRef;
import org.apache.lucene.util.IOUtils;
import org.apache.lucene.util.TestUtil;
import org.elasticsearch.index.mapper.internal.FieldNamesFieldMapper;
import org.elasticsearch.index.mapper.internal.SourceFieldMapper;
import org.elasticsearch.test.ESTestCase;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.hamcrest.Matchers.equalTo;

/** Simple tests for this filterreader */
public class FieldSubsetReaderTests extends ESTestCase {
    
    /**
     * test filtering two string fields
     */
    public void testIndexed() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        Set<String> seenFields = new HashSet<>();
        for (String field : segmentReader.fields()) {
            seenFields.add(field);
        }
        assertEquals(Collections.singleton("fieldA"), seenFields);
        assertNotNull(segmentReader.terms("fieldA"));
        assertNull(segmentReader.terms("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two int points
     */
    public void testPoints() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);

        // add document with 2 points
        Document doc = new Document();
        doc.add(new IntPoint("fieldA", 1));
        doc.add(new IntPoint("fieldB", 2));
        iw.addDocument(doc);

        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);

        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        PointValues points = segmentReader.getPointValues();

        // size statistic
        assertEquals(1, points.size("fieldA"));
        assertEquals(0, points.size("fieldB"));

        // doccount statistic
        assertEquals(1, points.getDocCount("fieldA"));
        assertEquals(0, points.getDocCount("fieldB"));

        // min statistic
        assertNotNull(points.getMinPackedValue("fieldA"));
        assertNull(points.getMinPackedValue("fieldB"));

        // max statistic
        assertNotNull(points.getMaxPackedValue("fieldA"));
        assertNull(points.getMaxPackedValue("fieldB"));

        // bytes per dimension
        assertEquals(Integer.BYTES, points.getBytesPerDimension("fieldA"));
        assertEquals(0, points.getBytesPerDimension("fieldB"));

        // number of dimensions
        assertEquals(1, points.getNumDimensions("fieldA"));
        assertEquals(0, points.getNumDimensions("fieldB"));

        // walk the trees: we should see stuff in fieldA
        AtomicBoolean sawDoc = new AtomicBoolean(false);
        points.intersect("fieldA", new IntersectVisitor() {
            @Override
            public void visit(int docID) throws IOException {
                throw new IllegalStateException("should not get here");
            }

            @Override
            public void visit(int docID, byte[] packedValue) throws IOException {
                sawDoc.set(true);
            }

            @Override
            public Relation compare(byte[] minPackedValue, byte[] maxPackedValue) {
                return Relation.CELL_CROSSES_QUERY;
            }
        });
        assertTrue(sawDoc.get());
        // not in fieldB
        points.intersect("fieldB", new IntersectVisitor() {
            @Override
            public void visit(int docID) throws IOException {
                throw new IllegalStateException("should not get here");
            }

            @Override
            public void visit(int docID, byte[] packedValue) throws IOException {
                throw new IllegalStateException("should not get here");
            }

            @Override
            public Relation compare(byte[] minPackedValue, byte[] maxPackedValue) {
                throw new IllegalStateException("should not get here");
            }
        });

        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }

    /**
     * test filtering two stored fields (string)
     */
    public void testStoredFieldsString() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", "testA"));
        doc.add(new StoredField("fieldB", "testB"));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals("testA", d2.get("fieldA"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two stored fields (binary)
     */
    public void testStoredFieldsBinary() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", new BytesRef("testA")));
        doc.add(new StoredField("fieldB", new BytesRef("testB")));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals(new BytesRef("testA"), d2.getBinaryValue("fieldA"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two stored fields (int)
     */
    public void testStoredFieldsInt() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", 1));
        doc.add(new StoredField("fieldB", 2));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals(1, d2.getField("fieldA").numericValue());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two stored fields (long)
     */
    public void testStoredFieldsLong() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", 1L));
        doc.add(new StoredField("fieldB", 2L));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals(1L, d2.getField("fieldA").numericValue());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two stored fields (float)
     */
    public void testStoredFieldsFloat() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", 1F));
        doc.add(new StoredField("fieldB", 2F));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals(1F, d2.getField("fieldA").numericValue());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two stored fields (double)
     */
    public void testStoredFieldsDouble() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StoredField("fieldA", 1D));
        doc.add(new StoredField("fieldB", 2D));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals(1D, d2.getField("fieldA").numericValue());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two vector fields
     */
    public void testVectors() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        FieldType ft = new FieldType(StringField.TYPE_NOT_STORED);
        ft.setStoreTermVectors(true);
        doc.add(new Field("fieldA", "testA", ft));
        doc.add(new Field("fieldB", "testB", ft));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Fields vectors = ir.getTermVectors(0);
        Set<String> seenFields = new HashSet<>();
        for (String field : vectors) {
            seenFields.add(field);
        }
        assertEquals(Collections.singleton("fieldA"), seenFields);
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two text fields
     */
    public void testNorms() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(new MockAnalyzer(random()));
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new TextField("fieldA", "test", Field.Store.NO));
        doc.add(new TextField("fieldB", "test", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        assertNotNull(segmentReader.getNormValues("fieldA"));
        assertNull(segmentReader.getNormValues("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two numeric dv fields
     */
    public void testNumericDocValues() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new NumericDocValuesField("fieldA", 1));
        doc.add(new NumericDocValuesField("fieldB", 2));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        assertNotNull(segmentReader.getNumericDocValues("fieldA"));
        assertEquals(1, segmentReader.getNumericDocValues("fieldA").get(0));
        assertNull(segmentReader.getNumericDocValues("fieldB"));
        
        // check docs with field
        assertNotNull(segmentReader.getDocsWithField("fieldA"));
        assertNull(segmentReader.getDocsWithField("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two binary dv fields
     */
    public void testBinaryDocValues() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new BinaryDocValuesField("fieldA", new BytesRef("testA")));
        doc.add(new BinaryDocValuesField("fieldB", new BytesRef("testB")));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        assertNotNull(segmentReader.getBinaryDocValues("fieldA"));
        assertEquals(new BytesRef("testA"), segmentReader.getBinaryDocValues("fieldA").get(0));
        assertNull(segmentReader.getBinaryDocValues("fieldB"));
        
        // check docs with field
        assertNotNull(segmentReader.getDocsWithField("fieldA"));
        assertNull(segmentReader.getDocsWithField("fieldB"));

        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two sorted dv fields
     */
    public void testSortedDocValues() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new SortedDocValuesField("fieldA", new BytesRef("testA")));
        doc.add(new SortedDocValuesField("fieldB", new BytesRef("testB")));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        assertNotNull(segmentReader.getSortedDocValues("fieldA"));
        assertEquals(new BytesRef("testA"), segmentReader.getSortedDocValues("fieldA").get(0));
        assertNull(segmentReader.getSortedDocValues("fieldB"));
        
        // check docs with field
        assertNotNull(segmentReader.getDocsWithField("fieldA"));
        assertNull(segmentReader.getDocsWithField("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two sortedset dv fields
     */
    public void testSortedSetDocValues() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new SortedSetDocValuesField("fieldA", new BytesRef("testA")));
        doc.add(new SortedSetDocValuesField("fieldB", new BytesRef("testB")));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        SortedSetDocValues dv = segmentReader.getSortedSetDocValues("fieldA");
        assertNotNull(dv);
        dv.setDocument(0);
        assertEquals(0, dv.nextOrd());
        assertEquals(SortedSetDocValues.NO_MORE_ORDS, dv.nextOrd());
        assertEquals(new BytesRef("testA"), dv.lookupOrd(0));
        assertNull(segmentReader.getSortedSetDocValues("fieldB"));
        
        // check docs with field
        assertNotNull(segmentReader.getDocsWithField("fieldA"));
        assertNull(segmentReader.getDocsWithField("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering two sortednumeric dv fields
     */
    public void testSortedNumericDocValues() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new SortedNumericDocValuesField("fieldA", 1));
        doc.add(new SortedNumericDocValuesField("fieldB", 2));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        SortedNumericDocValues dv = segmentReader.getSortedNumericDocValues("fieldA");
        assertNotNull(dv);
        dv.setDocument(0);
        assertEquals(1, dv.count());
        assertEquals(1, dv.valueAt(0));
        assertNull(segmentReader.getSortedNumericDocValues("fieldB"));
        
        // check docs with field
        assertNotNull(segmentReader.getDocsWithField("fieldA"));
        assertNull(segmentReader.getDocsWithField("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test we have correct fieldinfos metadata
     */
    public void testFieldInfos() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        FieldInfos infos = segmentReader.getFieldInfos();
        assertEquals(1, infos.size());
        assertNotNull(infos.fieldInfo("fieldA"));
        assertNull(infos.fieldInfo("fieldB"));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test special handling for _source field.
     */
    public void testSourceFiltering() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "testA", Field.Store.NO));
        doc.add(new StringField("fieldB", "testB", Field.Store.NO));
        byte bytes[] = "{\"fieldA\":\"testA\", \"fieldB\":\"testB\"}".getBytes(StandardCharsets.UTF_8);
        doc.add(new StoredField(SourceFieldMapper.NAME, bytes, 0, bytes.length));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = new HashSet<>();
        fields.add("fieldA");
        fields.add(SourceFieldMapper.NAME);
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        Document d2 = ir.document(0);
        assertEquals(1, d2.getFields().size());
        assertEquals("{\"fieldA\":\"testA\"}", d2.getBinaryValue(SourceFieldMapper.NAME).utf8ToString());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test special handling for _field_names field.
     */
    public void testFieldNames() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldA", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldB", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = new HashSet<>();
        fields.add("fieldA");
        fields.add(FieldNamesFieldMapper.NAME);
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        Terms terms = segmentReader.terms(FieldNamesFieldMapper.NAME);
        TermsEnum termsEnum = terms.iterator();
        assertEquals(new BytesRef("fieldA"), termsEnum.next());
        assertNull(termsEnum.next());
        
        // seekExact 
        termsEnum = terms.iterator();
        assertTrue(termsEnum.seekExact(new BytesRef("fieldA")));
        assertFalse(termsEnum.seekExact(new BytesRef("fieldB")));
        
        // seekCeil 
        termsEnum = terms.iterator();
        assertEquals(SeekStatus.FOUND, termsEnum.seekCeil(new BytesRef("fieldA")));
        assertEquals(SeekStatus.NOT_FOUND, termsEnum.seekCeil(new BytesRef("field0000")));
        assertEquals(new BytesRef("fieldA"), termsEnum.term());
        assertEquals(SeekStatus.END, termsEnum.seekCeil(new BytesRef("fieldAAA")));
        assertEquals(SeekStatus.END, termsEnum.seekCeil(new BytesRef("fieldB")));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test special handling for _field_names field (three fields, to exercise termsenum better)
     */
    public void testFieldNamesThreeFields() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        doc.add(new StringField("fieldC", "test", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldA", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldB", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldC", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = new HashSet<>();
        fields.add("fieldA");
        fields.add("fieldC");
        fields.add(FieldNamesFieldMapper.NAME);
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only two fields
        LeafReader segmentReader = ir.leaves().get(0).reader();
        Terms terms = segmentReader.terms(FieldNamesFieldMapper.NAME);
        TermsEnum termsEnum = terms.iterator();
        assertEquals(new BytesRef("fieldA"), termsEnum.next());
        assertEquals(new BytesRef("fieldC"), termsEnum.next());
        assertNull(termsEnum.next());
        
        // seekExact 
        termsEnum = terms.iterator();
        assertTrue(termsEnum.seekExact(new BytesRef("fieldA")));
        assertFalse(termsEnum.seekExact(new BytesRef("fieldB")));
        assertTrue(termsEnum.seekExact(new BytesRef("fieldC")));
        
        // seekCeil 
        termsEnum = terms.iterator();
        assertEquals(SeekStatus.FOUND, termsEnum.seekCeil(new BytesRef("fieldA")));
        assertEquals(SeekStatus.NOT_FOUND, termsEnum.seekCeil(new BytesRef("fieldB")));
        assertEquals(new BytesRef("fieldC"), termsEnum.term());
        assertEquals(SeekStatus.END, termsEnum.seekCeil(new BytesRef("fieldD")));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test _field_names where a field is permitted, but doesn't exist in the segment.
     */
    public void testFieldNamesMissing() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldA", Field.Store.NO));
        doc.add(new StringField(FieldNamesFieldMapper.NAME, "fieldB", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = new HashSet<>();
        fields.add("fieldA");
        fields.add("fieldC");
        fields.add(FieldNamesFieldMapper.NAME);
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        Terms terms = segmentReader.terms(FieldNamesFieldMapper.NAME);
        
        // seekExact 
        TermsEnum termsEnum = terms.iterator();
        assertFalse(termsEnum.seekExact(new BytesRef("fieldC")));
        
        // seekCeil 
        termsEnum = terms.iterator();
        assertEquals(SeekStatus.END, termsEnum.seekCeil(new BytesRef("fieldC")));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test where _field_names does not exist
     */
    public void testFieldNamesOldIndex() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        doc.add(new StringField("fieldA", "test", Field.Store.NO));
        doc.add(new StringField("fieldB", "test", Field.Store.NO));
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = new HashSet<>();
        fields.add("fieldA");
        fields.add(FieldNamesFieldMapper.NAME);
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see only one field
        LeafReader segmentReader = ir.leaves().get(0).reader();
        assertNull(segmentReader.terms(FieldNamesFieldMapper.NAME));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /** test that core cache key (needed for NRT) is working */
    public void testCoreCacheKey() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        iwc.setMaxBufferedDocs(100);
        iwc.setMergePolicy(NoMergePolicy.INSTANCE);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add two docs, id:0 and id:1
        Document doc = new Document();
        Field idField = new StringField("id", "", Field.Store.NO);
        doc.add(idField);
        idField.setStringValue("0");
        iw.addDocument(doc);
        idField.setStringValue("1");
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("id");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        assertEquals(2, ir.numDocs());
        assertEquals(1, ir.leaves().size());

        // delete id:0 and reopen
        iw.deleteDocuments(new Term("id", "0"));
        DirectoryReader ir2 = DirectoryReader.openIfChanged(ir);
        
        // we should have the same cache key as before
        assertEquals(1, ir2.numDocs());
        assertEquals(1, ir2.leaves().size());
        assertSame(ir.leaves().get(0).reader().getCoreCacheKey(), ir2.leaves().get(0).reader().getCoreCacheKey());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, ir2, iw, dir);
    }
    
    /**
     * test filtering the only vector fields
     */
    public void testFilterAwayAllVectors() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        
        // add document with 2 fields
        Document doc = new Document();
        FieldType ft = new FieldType(StringField.TYPE_NOT_STORED);
        ft.setStoreTermVectors(true);
        doc.add(new Field("fieldA", "testA", ft));
        doc.add(new StringField("fieldB", "testB", Field.Store.NO)); // no vectors
        iw.addDocument(doc);
        
        // open reader
        Set<String> fields = Collections.singleton("fieldB");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // sees no fields
        assertNull(ir.getTermVectors(0));
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }
    
    /**
     * test filtering an index with no fields
     */
    public void testEmpty() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        iw.addDocument(new Document());
        
        // open reader
        Set<String> fields = Collections.singleton("fieldA");
        DirectoryReader ir = FieldSubsetReader.wrap(DirectoryReader.open(iw), fields);
        
        // see no fields
        LeafReader segmentReader = ir.leaves().get(0).reader();
        Fields f = segmentReader.fields();
        assertNotNull(f); // 5.x contract
        Set<String> seenFields = new HashSet<>();
        for (String field : segmentReader.fields()) {
            seenFields.add(field);
        }
        assertEquals(0, seenFields.size());
        
        // see no vectors
        assertNull(segmentReader.getTermVectors(0));
        
        // see no stored fields
        Document document = segmentReader.document(0);
        assertEquals(0, document.getFields().size());
        
        TestUtil.checkReader(ir);
        IOUtils.close(ir, iw, dir);
    }

    public void testWrapTwice() throws Exception {
        Directory dir = newDirectory();
        IndexWriterConfig iwc = new IndexWriterConfig(null);
        IndexWriter iw = new IndexWriter(dir, iwc);
        iw.close();

        DirectoryReader directoryReader = DirectoryReader.open(dir);
        directoryReader = FieldSubsetReader.wrap(directoryReader, Collections.emptySet());
        try {
            FieldSubsetReader.wrap(directoryReader, Collections.emptySet());
            fail("shouldn't be able to wrap FieldSubsetDirectoryReader twice");
        } catch (IllegalArgumentException e) {
            assertThat(e.getMessage(), equalTo("Can't wrap [class org.elasticsearch.shield.authz.accesscontrol" +
                    ".FieldSubsetReader$FieldSubsetDirectoryReader] twice"));
        }
        directoryReader.close();
        dir.close();
    }
}
