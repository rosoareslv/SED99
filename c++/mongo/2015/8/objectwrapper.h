/**
 * Copyright (C) 2015 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <jsapi.h>
#include <string>

#include "mongo/platform/decimal128.h"
#include "mongo/scripting/mozjs/exception.h"

namespace mongo {

class BSONObj;
class BSONObjBuilder;
class BSONElement;

namespace mozjs {

class MozJSImplScope;

/**
 * Wraps JSObject's with helpers for accessing their properties
 *
 * This wraps a RootedObject, so should only be allocated on the stack and is
 * not movable or copyable
 */
class ObjectWrapper {
public:
    /**
     * Helper subclass that provides some easy boilerplate for accessing
     * properties by string, index or id.
     */
    class Key {
        friend class ObjectWrapper;

        enum class Type : char {
            Field,
            Index,
            Id,
        };

    public:
        Key(const char* field) : _field(field), _type(Type::Field) {}
        Key(uint32_t idx) : _idx(idx), _type(Type::Index) {}
        Key(JS::HandleId id) : _id(id), _type(Type::Id) {}

    private:
        void get(JSContext* cx, JS::HandleObject o, JS::MutableHandleValue value);
        void set(JSContext* cx, JS::HandleObject o, JS::HandleValue value);
        bool has(JSContext* cx, JS::HandleObject o);
        void define(JSContext* cx, JS::HandleObject o, JS::HandleValue value, unsigned attrs);
        void del(JSContext* cx, JS::HandleObject o);
        std::string toString(JSContext* cx);

        union {
            const char* _field;
            uint32_t _idx;
            jsid _id;
        };
        Type _type;
    };

    /**
     * The depth parameter here allows us to detect overly nested or circular
     * objects and bail without blowing the stack.
     */
    ObjectWrapper(JSContext* cx, JS::HandleObject obj, int depth = 0);
    ObjectWrapper(JSContext* cx, JS::HandleValue value, int depth = 0);

    double getNumber(Key key);
    int getNumberInt(Key key);
    long long getNumberLongLong(Key key);
    Decimal128 getNumberDecimal(Key key);
    std::string getString(Key key);
    bool getBoolean(Key key);
    BSONObj getObject(Key key);
    void getValue(Key key, JS::MutableHandleValue value);

    void setNumber(Key key, double val);
    void setString(Key key, StringData val);
    void setBoolean(Key key, bool val);
    void setBSONElement(Key key, const BSONElement& elem, bool readOnly);
    void setBSON(Key key, const BSONObj& obj, bool readOnly);
    void setValue(Key key, JS::HandleValue value);
    void setObject(Key key, JS::HandleObject value);

    /**
     * See JS_DefineProperty for what sort of attributes might be useful
     */
    void defineProperty(Key key, JS::HandleValue value, unsigned attrs);

    void deleteProperty(Key key);

    /**
     * Returns the bson type of the property
     */
    int type(Key key);

    void rename(Key key, const char* to);

    bool hasField(Key key);

    void callMethod(const char* name, const JS::HandleValueArray& args, JS::MutableHandleValue out);
    void callMethod(const char* name, JS::MutableHandleValue out);
    void callMethod(JS::HandleValue fun,
                    const JS::HandleValueArray& args,
                    JS::MutableHandleValue out);
    void callMethod(JS::HandleValue fun, JS::MutableHandleValue out);

    /**
     * Safely enumerates fields in the object, invoking a callback for each id
     */
    template <typename T>
    void enumerate(T&& callback) {
        JS::AutoIdArray ids(_context, JS_Enumerate(_context, _object));

        if (!ids)
            throwCurrentJSException(
                _context, ErrorCodes::JSInterpreterFailure, "Failure to enumerate object");

        JS::RootedId rid(_context);
        for (size_t i = 0; i < ids.length(); ++i) {
            rid.set(ids[i]);
            callback(rid);
        }
    }

    /**
     * concatenates all of the fields in the object into the associated builder
     */
    void writeThis(BSONObjBuilder* b);

    JS::HandleObject thisv() {
        return _object;
    }

private:
    /**
     * writes the field "key" into the associated builder
     *
     * optional originalBSON is used to track updates to types (NumberInt
     * overwritten by a float, but coercible to the original type, etc.)
     */
    void _writeField(BSONObjBuilder* b, Key key, BSONObj* originalBSON);

    JSContext* _context;
    JS::RootedObject _object;

    /**
     * The depth of an object wrapper has to do with how many parents it has.
     * Used to avoid circular object graphs and associate stack smashing.
     */
    int _depth;
};

}  // namespace mozjs
}  // namespace mongo
