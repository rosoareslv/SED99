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

package org.elasticsearch.index.mapper;

import org.apache.lucene.document.FieldType;
import org.apache.lucene.index.DocValuesType;
import org.apache.lucene.index.IndexOptions;
import org.apache.lucene.spatial.geopoint.document.GeoPointField;
import org.elasticsearch.Version;
import org.elasticsearch.common.Explicit;
import org.elasticsearch.common.geo.GeoPoint;
import org.elasticsearch.common.geo.GeoUtils;
import org.elasticsearch.common.settings.Settings;

import java.io.IOException;
import java.util.Map;

/**
 * Parsing: We handle:
 * <p>
 * - "field" : "geo_hash"
 * - "field" : "lat,lon"
 * - "field" : {
 * "lat" : 1.1,
 * "lon" : 2.1
 * }
 */
public class GeoPointFieldMapper extends BaseGeoPointFieldMapper  {

    public static final String CONTENT_TYPE = "geo_point";

    public static class Defaults extends BaseGeoPointFieldMapper.Defaults {

        public static final GeoPointFieldType FIELD_TYPE = new GeoPointFieldType();

        static {
            FIELD_TYPE.setIndexOptions(IndexOptions.DOCS);
            FIELD_TYPE.setTokenized(false);
            FIELD_TYPE.setOmitNorms(true);
            FIELD_TYPE.setDocValuesType(DocValuesType.SORTED_NUMERIC);
            FIELD_TYPE.setHasDocValues(true);
            FIELD_TYPE.freeze();
        }
    }

    /**
     * Concrete builder for indexed GeoPointField type
     */
    public static class Builder extends BaseGeoPointFieldMapper.Builder<Builder, GeoPointFieldMapper> {

        public Builder(String name) {
            super(name, Defaults.FIELD_TYPE);
            this.builder = this;
        }

        @Override
        public GeoPointFieldMapper build(BuilderContext context, String simpleName, MappedFieldType fieldType,
                                         MappedFieldType defaultFieldType, Settings indexSettings, FieldMapper latMapper,
                                         FieldMapper lonMapper, FieldMapper geoHashMapper, MultiFields multiFields, Explicit<Boolean> ignoreMalformed,
                                         CopyTo copyTo) {
            fieldType.setTokenized(false);
            if (context.indexCreatedVersion().before(Version.V_2_3_0)) {
                fieldType.setNumericPrecisionStep(GeoPointField.PRECISION_STEP);
                fieldType.setNumericType(FieldType.LegacyNumericType.LONG);
            }
            setupFieldType(context);
            return new GeoPointFieldMapper(simpleName, fieldType, defaultFieldType, indexSettings, latMapper, lonMapper,
                    geoHashMapper, multiFields, ignoreMalformed, copyTo);
        }

        @Override
        public GeoPointFieldMapper build(BuilderContext context) {
            if (context.indexCreatedVersion().before(Version.V_2_3_0)) {
                fieldType.setNumericPrecisionStep(GeoPointField.PRECISION_STEP);
                fieldType.setNumericType(FieldType.LegacyNumericType.LONG);
            }
            return super.build(context);
        }
    }

    public static class TypeParser extends BaseGeoPointFieldMapper.TypeParser {
        @Override
        public Mapper.Builder<?, ?> parse(String name, Map<String, Object> node, ParserContext parserContext) throws MapperParsingException {
            return super.parse(name, node, parserContext);
        }
    }

    public GeoPointFieldMapper(String simpleName, MappedFieldType fieldType, MappedFieldType defaultFieldType, Settings indexSettings,
                               FieldMapper latMapper, FieldMapper lonMapper,
                               FieldMapper geoHashMapper, MultiFields multiFields, Explicit<Boolean> ignoreMalformed, CopyTo copyTo) {
        super(simpleName, fieldType, defaultFieldType, indexSettings, latMapper, lonMapper, geoHashMapper, multiFields,
                ignoreMalformed, copyTo);
    }

    @Override
    protected void parse(ParseContext context, GeoPoint point, String geoHash) throws IOException {
        if (ignoreMalformed.value() == false) {
            if (point.lat() > 90.0 || point.lat() < -90.0) {
                throw new IllegalArgumentException("illegal latitude value [" + point.lat() + "] for " + name());
            }
            if (point.lon() > 180.0 || point.lon() < -180) {
                throw new IllegalArgumentException("illegal longitude value [" + point.lon() + "] for " + name());
            }
        } else {
            // LUCENE WATCH: This will be folded back into Lucene's GeoPointField
            GeoUtils.normalizePoint(point);
        }
        if (fieldType().indexOptions() != IndexOptions.NONE || fieldType().stored()) {
            context.doc().add(new GeoPointField(fieldType().name(), point.lat(), point.lon(), fieldType()));
        }
        super.parse(context, point, geoHash);
    }
}
