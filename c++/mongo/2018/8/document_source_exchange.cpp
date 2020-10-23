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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include <algorithm>
#include <iterator>
#include <set>

#include "mongo/db/hasher.h"
#include "mongo/db/pipeline/document_source_exchange.h"
#include "mongo/db/storage/key_string.h"
#include "mongo/util/log.h"

namespace mongo {

const char* DocumentSourceExchange::getSourceName() const {
    return "$_internalExchange";
}

Value DocumentSourceExchange::serialize(boost::optional<ExplainOptions::Verbosity> explain) const {
    return Value(DOC(getSourceName() << _exchange->getSpec().toBSON()));
}

DocumentSourceExchange::DocumentSourceExchange(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    const boost::intrusive_ptr<Exchange> exchange,
    size_t consumerId)
    : DocumentSource(expCtx), _exchange(exchange), _consumerId(consumerId) {}

DocumentSource::GetNextResult DocumentSourceExchange::getNext() {
    return _exchange->getNext(pExpCtx->opCtx, _consumerId);
}

Exchange::Exchange(ExchangeSpec spec, std::unique_ptr<Pipeline, PipelineDeleter> pipeline)
    : _spec(std::move(spec)),
      _keyPattern(_spec.getKey().getOwned()),
      _ordering(extractOrdering(_keyPattern)),
      _boundaries(extractBoundaries(_spec.getBoundaries())),
      _consumerIds(extractConsumerIds(_spec.getConsumerids(), _spec.getConsumers())),
      _policy(_spec.getPolicy()),
      _orderPreserving(_spec.getOrderPreserving()),
      _maxBufferSize(_spec.getBufferSize()),
      _pipeline(std::move(pipeline)) {
    uassert(50901, "Exchange must have at least one consumer", _spec.getConsumers() > 0);

    for (int idx = 0; idx < _spec.getConsumers(); ++idx) {
        _consumers.emplace_back(std::make_unique<ExchangeBuffer>());
    }

    if (_policy == ExchangePolicyEnum::kRange || _policy == ExchangePolicyEnum::kHash) {
        uassert(50900,
                "Exchange boundaries do not match number of consumers.",
                _boundaries.size() == _consumerIds.size() + 1);
    } else {
        uassert(50899, "Exchange boundaries must not be specified.", _boundaries.empty());
    }

    // We will manually detach and reattach when iterating '_pipeline', we expect it to start in the
    // detached state.
    _pipeline->detachFromOperationContext();
}

std::vector<std::string> Exchange::extractBoundaries(
    const boost::optional<std::vector<BSONObj>>& obj) {
    std::vector<std::string> ret;

    if (!obj) {
        return ret;
    }

    for (auto& b : *obj) {
        // Build the key.
        BSONObjBuilder kb;
        for (auto elem : b) {
            kb << "" << elem;
        }

        KeyString key{KeyString::Version::V1, kb.obj(), Ordering::make(BSONObj())};
        std::string keyStr{key.getBuffer(), key.getSize()};

        ret.emplace_back(std::move(keyStr));
    }

    for (size_t idx = 1; idx < ret.size(); ++idx) {
        uassert(50893,
                str::stream() << "Exchange range boundaries are not in ascending order.",
                ret[idx - 1] < ret[idx]);
    }
    return ret;
}

std::vector<size_t> Exchange::extractConsumerIds(
    const boost::optional<std::vector<std::int32_t>>& consumerIds, size_t nConsumers) {

    std::vector<size_t> ret;

    if (!consumerIds) {
        // If the ids are not specified than we generate a simple sequence 0,1,2,3,...
        for (size_t idx = 0; idx < nConsumers; ++idx) {
            ret.push_back(idx);
        }
    } else {
        // Validate that the ids are dense (no hole) and in the range [0,nConsumers)
        std::set<size_t> validation;

        for (auto cid : *consumerIds) {
            validation.insert(cid);
            ret.push_back(cid);
        }

        uassert(50894,
                str::stream() << "Exchange consumers ids are invalid.",
                nConsumers > 0 && validation.size() == nConsumers && *validation.begin() == 0 &&
                    *validation.rbegin() == nConsumers - 1);
    }
    return ret;
}

Ordering Exchange::extractOrdering(const BSONObj& obj) {
    bool hasHashKey = false;
    bool hasOrderKey = false;

    for (const auto& element : obj) {
        if (element.type() == BSONType::String) {
            uassert(50895,
                    str::stream() << "Exchange key description is invalid: " << element,
                    element.valueStringData() == "hashed"_sd);
            hasHashKey = true;
        } else if (element.isNumber()) {
            auto num = element.number();
            if (!(num == 1 || num == -1)) {
                uasserted(50896,
                          str::stream() << "Exchange key description is invalid: " << element);
            }
            hasOrderKey = true;
        } else {
            uasserted(50897, str::stream() << "Exchange key description is invalid: " << element);
        }
    }

    uassert(50898,
            str::stream() << "Exchange hash and order keys cannot be mixed together: " << obj,
            !(hasHashKey && hasOrderKey));

    return hasHashKey ? Ordering::make(BSONObj()) : Ordering::make(obj);
}

DocumentSource::GetNextResult Exchange::getNext(OperationContext* opCtx, size_t consumerId) {
    // Grab a lock.
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    for (;;) {
        // Check if we have a document.
        if (!_consumers[consumerId]->isEmpty()) {
            auto doc = _consumers[consumerId]->getNext();

            // See if the loading is blocked on this consumer and if so unblock it.
            if (_loadingThreadId == consumerId) {
                _loadingThreadId = kInvalidThreadId;
                _haveBufferSpace.notify_all();
            }

            return doc;
        }

        // There is not any document so try to load more from the source.
        if (_loadingThreadId == kInvalidThreadId) {
            LOG(3) << "A consumer " << consumerId << " begins loading";

            // This consumer won the race and will fill the buffers.
            _loadingThreadId = consumerId;

            _pipeline->reattachToOperationContext(opCtx);

            // This will return when some exchange buffer is full and we cannot make any forward
            // progress anymore.
            // The return value is an index of a full consumer buffer.
            size_t fullConsumerId = loadNextBatch();

            _pipeline->detachFromOperationContext();

            // The loading cannot continue until the consumer with the full buffer consumes some
            // documents.
            _loadingThreadId = fullConsumerId;

            // Wake up everybody and try to make some progress.
            _haveBufferSpace.notify_all();
        } else {
            // Some other consumer is already loading the buffers. There is nothing else we can do
            // but wait.
            _haveBufferSpace.wait(lk);
        }
    }
}

size_t Exchange::loadNextBatch() {
    auto input = _pipeline->getSources().back()->getNext();

    for (; input.isAdvanced(); input = _pipeline->getSources().back()->getNext()) {
        // We have a document and we will deliver it to a consumer(s) based on the policy.
        switch (_policy) {
            case ExchangePolicyEnum::kBroadcast: {
                bool full = false;
                // The document is sent to all consumers.
                for (auto& c : _consumers) {
                    full = c->appendDocument(input, _maxBufferSize);
                }

                if (full)
                    return 0;
            } break;
            case ExchangePolicyEnum::kRoundRobin: {
                size_t target = _roundRobinCounter;
                _roundRobinCounter = (_roundRobinCounter + 1) % _consumers.size();

                if (_consumers[target]->appendDocument(std::move(input), _maxBufferSize))
                    return target;
            } break;
            case ExchangePolicyEnum::kRange: {
                size_t target = getTargetConsumer(input.getDocument());
                bool full = _consumers[target]->appendDocument(std::move(input), _maxBufferSize);
                if (full && _orderPreserving) {
                    // TODO send the high watermark here.
                }
                if (full)
                    return target;
            } break;
            case ExchangePolicyEnum::kHash: {
                size_t target = getTargetConsumer(input.getDocument());
                bool full = _consumers[target]->appendDocument(std::move(input), _maxBufferSize);
                if (full && _orderPreserving) {
                    // TODO send the high watermark here.
                }
                if (full)
                    return target;
            } break;
            default:
                MONGO_UNREACHABLE;
        }
    }

    invariant(input.isEOF());

    // We have reached the end so send EOS to all consumers.
    for (auto& c : _consumers) {
        c->appendDocument(input, _maxBufferSize);
    }

    return kInvalidThreadId;
}

size_t Exchange::getTargetConsumer(const Document& input) {
    // Build the key.
    BSONObjBuilder kb;
    for (auto elem : _keyPattern) {
        auto value = input[elem.fieldName()];
        if (elem.type() == BSONType::String && elem.str() == "hashed") {
            kb << "" << BSONElementHasher::hash64(BSON("" << value).firstElement(),
                                                  BSONElementHasher::DEFAULT_HASH_SEED);
        } else {
            kb << "" << value;
        }
    }

    // TODO implement hash keys for the hash policy.
    KeyString key{KeyString::Version::V1, kb.obj(), _ordering};
    std::string keyStr{key.getBuffer(), key.getSize()};

    // Binary search for the consumer id.
    auto it = std::upper_bound(_boundaries.begin(), _boundaries.end(), keyStr);
    invariant(it != _boundaries.end());

    size_t distance = std::distance(_boundaries.begin(), it) - 1;
    invariant(distance < _consumerIds.size());

    size_t cid = _consumerIds[distance];
    invariant(cid < _consumers.size());

    return cid;
}

void Exchange::dispose(OperationContext* opCtx) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    invariant(_disposeRunDown < getConsumers());

    ++_disposeRunDown;

    if (_disposeRunDown == getConsumers()) {
        _pipeline->dispose(opCtx);
    }
}

DocumentSource::GetNextResult Exchange::ExchangeBuffer::getNext() {
    invariant(!_buffer.empty());

    auto result = std::move(_buffer.front());
    _buffer.pop_front();

    if (result.isAdvanced()) {
        _bytesInBuffer -= result.getDocument().getApproximateSize();
    }

    return result;
}

bool Exchange::ExchangeBuffer::appendDocument(DocumentSource::GetNextResult input, size_t limit) {
    if (input.isAdvanced()) {
        _bytesInBuffer += input.getDocument().getApproximateSize();
    }
    _buffer.push_back(std::move(input));

    // The buffer is full.
    return _bytesInBuffer >= limit;
}

}  // namespace mongo
