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

package io.realm.internal.test;

import java.nio.ByteBuffer;
import java.util.Date;

import io.realm.internal.Mixed;

public class EmployeeData {

    public String firstName;
    public String lastName;
    public int salary;
    public boolean driver;
    public byte[] photo;
    public Date birthdate;
    public Object extra;
    public PhoneData[] phones;

    public EmployeeData(String firstName, String lastName, int salary, boolean driver, byte[] photo, Date birthdate, Object extra, PhoneData[] phones) {
        this.firstName = firstName;
        this.lastName = lastName;
        this.salary = salary;
        this.driver = driver;
        this.photo = photo;
        this.birthdate = birthdate;
        this.extra = extra;
        this.phones = phones;
    }

    public Object get(int index) {
        switch (index) {
        case 0:
            return firstName;
        case 1:
            return lastName;
        case 2:
            return new Long(salary);
        case 3:
            return new Boolean(driver);
        case 4:
            return ByteBuffer.wrap(photo);
        case 5:
            return birthdate;
        case 6:
            return Mixed.mixedValue(extra);
        case 7:
            return phones;
        default:
            throw new IllegalArgumentException("Incorrect index: " + index);
        }
    }

}
