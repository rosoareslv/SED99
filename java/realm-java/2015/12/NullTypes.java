/*
 * Copyright 2015 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package some.test;

import java.lang.String;
import java.util.Date;

import io.realm.RealmObject;
import io.realm.annotations.Required;

public class NullTypes extends RealmObject {
    @Required
    private String fieldStringNotNull;
    private String fieldStringNull;

    @Required
    private Boolean fieldBooleanNotNull;
    private Boolean fieldBooleanNull;

    @Required
    private byte[] fieldBytesNotNull;
    private byte[] fieldBytesNull;

    @Required
    private Byte fieldByteNotNull;
    private Byte fieldByteNull;

    @Required
    private Short fieldShortNotNull;
    private Short fieldShortNull;

    @Required
    private Integer fieldIntegerNotNull;
    private Integer fieldIntegerNull;

    @Required
    private Long fieldLongNotNull;
    private Long fieldLongNull;

    @Required
    private Float fieldFloatNotNull;
    private Float fieldFloatNull;

    @Required
    private Double fieldDoubleNotNull;
    private Double fieldDoubleNull;

    @Required
    private Date fieldDateNotNull;
    private Date fieldDateNull;

    private NullTypes fieldObjectNull;


    public String getFieldStringNotNull() {
        return fieldStringNotNull;
    }

    public void setFieldStringNotNull(String fieldStringNotNull) {
        this.fieldStringNotNull = fieldStringNotNull;
    }

    public String getFieldStringNull() {
        return fieldStringNull;
    }

    public void setFieldStringNull(String fieldStringNull) {
        this.fieldStringNull = fieldStringNull;
    }

    public Boolean getFieldBooleanNotNull() {
        return fieldBooleanNotNull;
    }

    public void setFieldBooleanNotNull(Boolean fieldBooleanNotNull) {
        this.fieldBooleanNotNull = fieldBooleanNotNull;
    }

    public Boolean getFieldBooleanNull() {
        return fieldBooleanNull;
    }

    public void setFieldBooleanNull(Boolean fieldBooleanNull) {
        this.fieldBooleanNull = fieldBooleanNull;
    }

    public byte[] getFieldBytesNotNull() {
        return fieldBytesNotNull;
    }

    public void setFieldBytesNotNull(byte[] fieldBytesNotNull) {
        this.fieldBytesNotNull = fieldBytesNotNull;
    }

    public byte[] getFieldBytesNull() {
        return fieldBytesNull;
    }

    public void setFieldBytesNull(byte[] fieldBytesNull) {
        this.fieldBytesNull = fieldBytesNull;
    }

    public Byte getFieldByteNotNull() {
        return fieldByteNotNull;
    }

    public void setFieldByteNotNull(Byte fieldByteNotNull) {
        this.fieldByteNotNull = fieldByteNotNull;
    }

    public Byte getFieldByteNull() {
        return fieldByteNull;
    }

    public void setFieldByteNull(Byte fieldByteNull) {
        this.fieldByteNull = fieldByteNull;
    }

    public Short getFieldShortNotNull() {
        return fieldShortNotNull;
    }

    public void setFieldShortNotNull(Short fieldShortNotNull) {
        this.fieldShortNotNull = fieldShortNotNull;
    }

    public Short getFieldShortNull() {
        return fieldShortNull;
    }

    public void setFieldShortNull(Short fieldShortNull) {
        this.fieldShortNull = fieldShortNull;
    }

    public Integer getFieldIntegerNotNull() {
        return fieldIntegerNotNull;
    }

    public void setFieldIntegerNotNull(Integer fieldIntegerNotNull) {
        this.fieldIntegerNotNull = fieldIntegerNotNull;
    }

    public Integer getFieldIntegerNull() {
        return fieldIntegerNull;
    }

    public void setFieldIntegerNull(Integer fieldIntegerNull) {
        this.fieldIntegerNull = fieldIntegerNull;
    }

    public Long getFieldLongNotNull() {
        return fieldLongNotNull;
    }

    public void setFieldLongNotNull(Long fieldLongNotNull) {
        this.fieldLongNotNull = fieldLongNotNull;
    }

    public Long getFieldLongNull() {
        return fieldLongNull;
    }

    public void setFieldLongNull(Long fieldLongNull) {
        this.fieldLongNull = fieldLongNull;
    }

    public Float getFieldFloatNotNull() {
        return fieldFloatNotNull;
    }

    public void setFieldFloatNotNull(Float fieldFloatNotNull) {
        this.fieldFloatNotNull = fieldFloatNotNull;
    }

    public Float getFieldFloatNull() {
        return fieldFloatNull;
    }

    public void setFieldFloatNull(Float fieldFloatNull) {
        this.fieldFloatNull = fieldFloatNull;
    }

    public Double getFieldDoubleNotNull() {
        return fieldDoubleNotNull;
    }

    public void setFieldDoubleNotNull(Double fieldDoubleNotNull) {
        this.fieldDoubleNotNull = fieldDoubleNotNull;
    }

    public Double getFieldDoubleNull() {
        return fieldDoubleNull;
    }

    public void setFieldDoubleNull(Double fieldDoubleNull) {
        this.fieldDoubleNull = fieldDoubleNull;
    }

    public Date getFieldDateNotNull() {
        return fieldDateNotNull;
    }

    public void setFieldDateNotNull(Date fieldDateNotNull) {
        this.fieldDateNotNull = fieldDateNotNull;
    }

    public Date getFieldDateNull() {
        return fieldDateNull;
    }

    public void setFieldDateNull(Date fieldDateNull) {
        this.fieldDateNull = fieldDateNull;
    }

    public NullTypes getFieldObjectNull() {
        return fieldObjectNull;
    }

    public void setFieldObjectNull(NullTypes fieldObjectNull) {
        this.fieldObjectNull = fieldObjectNull;
    }
}
