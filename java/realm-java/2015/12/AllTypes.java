/*
 * Copyright 2014 Realm Inc.
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

import java.util.Date;

import io.realm.RealmList;
import io.realm.RealmObject;
import io.realm.annotations.PrimaryKey;
import io.realm.annotations.Required;

public class AllTypes extends RealmObject {

    public static final String TAG = "AllTypes";

    @PrimaryKey
    private String columnString;
    private long columnLong;
    private float columnFloat;
    private double columnDouble;
    private boolean columnBoolean;
    @Required
    private Date columnDate;
    @Required
    private byte[] columnBinary;
    private AllTypes columnObject;
    private RealmList<AllTypes> columnRealmList;

    public String getColumnString() {
        return columnString;
    }

    public void setColumnString(String columnString) {
        this.columnString = columnString;
    }

    public long getColumnLong() {
        return columnLong;
    }

    public void setColumnLong(long columnLong) {
        this.columnLong = columnLong;
    }

    public float getColumnFloat() {
        return columnFloat;
    }

    public void setColumnFloat(float columnFloat) {
        this.columnFloat = columnFloat;
    }

    public double getColumnDouble() {
        return columnDouble;
    }

    public void setColumnDouble(double columnDouble) {
        this.columnDouble = columnDouble;
    }

    public boolean isColumnBoolean() {
        return columnBoolean;
    }

    public void setColumnBoolean(boolean columnBoolean) {
        this.columnBoolean = columnBoolean;
    }

    public Date getColumnDate() {
        return columnDate;
    }

    public void setColumnDate(Date columnDate) {
        this.columnDate = columnDate;
    }

    public byte[] getColumnBinary() {
        return columnBinary;
    }

    public void setColumnBinary(byte[] columnBinary) {
        this.columnBinary = columnBinary;
    }

    public AllTypes getColumnObject() {
        return columnObject;
    }

    public void setColumnObject(AllTypes columnObject) {
        this.columnObject = columnObject;
    }

    public RealmList<AllTypes> getColumnRealmList() {
        return columnRealmList;
    }

    public void setColumnRealmList(RealmList<AllTypes> columnRealmList) {
        this.columnRealmList = columnRealmList;
    }
}
