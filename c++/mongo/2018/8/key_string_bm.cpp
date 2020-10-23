/**
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include <benchmark/benchmark.h>
#include <random>
#include <vector>

#include "mongo/db/storage/key_string.h"
#include "mongo/platform/decimal128.h"
#include "mongo/util/bufreader.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

std::mt19937_64 seedGen(1234);
const int kSampleSize = 500;
const int kStrLenMultiplier = 100;
const int kArrLenMultiplier = 40;

const Ordering ALL_ASCENDING = Ordering::make(BSONObj());

struct BsonsAndKeyStrings {
    int bsonSize = 0;
    int keystringSize = 0;
    BSONObj bsons[kSampleSize];
    SharedBuffer keystrings[kSampleSize];
    size_t keystringLens[kSampleSize];
    SharedBuffer typebits[kSampleSize];
    size_t typebitsLens[kSampleSize];
};

enum BsonValueType {
    INT,
    DOUBLE,
    STRING,
    ARRAY,
    DECIMAL,
};

BSONObj generateBson(BsonValueType bsonValueType) {
    std::mt19937 gen(seedGen());
    std::exponential_distribution<double> expReal(1e-3);
    std::exponential_distribution<double> expDist(1.0);

    switch (bsonValueType) {
        case INT:
            return BSON("" << static_cast<int>(expReal(gen)));
        case DOUBLE:
            return BSON("" << expReal(gen));
        case STRING:
            return BSON("" << std::string(expDist(gen) * kStrLenMultiplier, 'x'));
        case ARRAY: {
            const int arrLen = expDist(gen) * kArrLenMultiplier;
            BSONArrayBuilder bab;
            for (int i = 0; i < arrLen; i++) {
                bab.append(expReal(gen));
            }
            return BSON("" << BSON("a" << bab.arr()));
        }
        case DECIMAL:
            return BSON("" << Decimal128(expReal(gen),
                                         Decimal128::kRoundTo34Digits,
                                         Decimal128::kRoundTiesToAway)
                                  .quantize(Decimal128("0.01", Decimal128::kRoundTiesToAway)));
    }
    MONGO_UNREACHABLE;
}

static BsonsAndKeyStrings generateBsonsAndKeyStrings(BsonValueType bsonValueType,
                                                     KeyString::Version version) {
    BsonsAndKeyStrings result;
    result.bsonSize = 0;
    result.keystringSize = 0;
    for (int i = 0; i < kSampleSize; i++) {
        BSONObj bson = generateBson(bsonValueType);
        KeyString ks(version, bson, ALL_ASCENDING);
        result.bsonSize += bson.objsize();
        result.keystringSize += ks.getSize();
        result.bsons[i] = bson;

        result.keystrings[i] = SharedBuffer::allocate(ks.getSize());
        memcpy(result.keystrings[i].get(), ks.getBuffer(), ks.getSize());
        result.keystringLens[i] = ks.getSize();

        result.typebits[i] = SharedBuffer::allocate(ks.getTypeBits().getSize());
        memcpy(result.typebits[i].get(), ks.getTypeBits().getBuffer(), ks.getTypeBits().getSize());
        result.typebitsLens[i] = ks.getSize();
    }
    return result;
}

void BM_BSONToKeyString(benchmark::State& state,
                        const KeyString::Version version,
                        BsonValueType bsonType) {
    const BsonsAndKeyStrings bsonsAndKeyStrings = generateBsonsAndKeyStrings(bsonType, version);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (auto bson : bsonsAndKeyStrings.bsons) {
            benchmark::DoNotOptimize(KeyString(version, bson, ALL_ASCENDING));
        }
    }
    state.SetBytesProcessed(state.iterations() * bsonsAndKeyStrings.bsonSize);
    state.SetItemsProcessed(state.iterations() * kSampleSize);
}

void BM_KeyStringToBSON(benchmark::State& state,
                        const KeyString::Version version,
                        BsonValueType bsonType) {
    const BsonsAndKeyStrings bsonsAndKeyStrings = generateBsonsAndKeyStrings(bsonType, version);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (size_t i = 0; i < kSampleSize; i++) {
            BufReader buf(bsonsAndKeyStrings.typebits[i].get(), bsonsAndKeyStrings.typebitsLens[i]);
            benchmark::DoNotOptimize(
                KeyString::toBson(bsonsAndKeyStrings.keystrings[i].get(),
                                  bsonsAndKeyStrings.keystringLens[i],
                                  ALL_ASCENDING,
                                  KeyString::TypeBits::fromBuffer(version, &buf)));
        }
    }
    state.SetBytesProcessed(state.iterations() * bsonsAndKeyStrings.bsonSize);
    state.SetItemsProcessed(state.iterations() * kSampleSize);
}

BENCHMARK_CAPTURE(BM_BSONToKeyString, V0_Int, KeyString::Version::V0, INT);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V1_Int, KeyString::Version::V1, INT);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V0_Double, KeyString::Version::V0, DOUBLE);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V1_Double, KeyString::Version::V1, DOUBLE);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V1_Decimal, KeyString::Version::V1, DECIMAL);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V0_String, KeyString::Version::V0, STRING);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V1_String, KeyString::Version::V1, STRING);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V0_Array, KeyString::Version::V0, ARRAY);
BENCHMARK_CAPTURE(BM_BSONToKeyString, V1_Array, KeyString::Version::V1, ARRAY);

BENCHMARK_CAPTURE(BM_KeyStringToBSON, V0_Int, KeyString::Version::V0, INT);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V1_Int, KeyString::Version::V1, INT);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V0_Double, KeyString::Version::V0, DOUBLE);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V1_Double, KeyString::Version::V1, DOUBLE);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V1_Decimal, KeyString::Version::V1, DECIMAL);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V0_String, KeyString::Version::V0, STRING);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V1_String, KeyString::Version::V1, STRING);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V0_Array, KeyString::Version::V0, ARRAY);
BENCHMARK_CAPTURE(BM_KeyStringToBSON, V1_Array, KeyString::Version::V1, ARRAY);
}  // namespace
}  // namespace mongo
