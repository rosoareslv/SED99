
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "stitch_support/stitch_support.h"

#include "api_common.h"
#include "mongo/base/initializer.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/client.h"
#include "mongo/db/matcher/matcher.h"
#include "mongo/db/query/collation/collator_factory_interface.h"
#include "mongo/db/service_context.h"
#include "mongo/util/assert_util.h"

#include <iostream>
#include <string>

#if defined(_WIN32)
#define MONGO_API_CALL __cdecl
#else
#define MONGO_API_CALL
#endif

namespace mongo {

using StitchSupportStatusImpl = StatusForAPI<stitch_support_v1_error>;

/**
 * C interfaces that use enterCXX() must provide a translateException() function that converts any
 * possible exception into a StatusForAPI<> object.
 */
static StitchSupportStatusImpl translateException(
    stdx::type_identity<StitchSupportStatusImpl>) try {
    throw;
} catch (const ExceptionFor<ErrorCodes::ReentrancyNotAllowed>& ex) {
    return {STITCH_SUPPORT_V1_ERROR_REENTRANCY_NOT_ALLOWED, ex.code(), ex.what()};
} catch (const DBException& ex) {
    return {STITCH_SUPPORT_V1_ERROR_EXCEPTION, ex.code(), ex.what()};
} catch (const ExceptionForAPI<stitch_support_v1_error>& ex) {
    return {ex.statusCode(), mongo::ErrorCodes::InternalError, ex.what()};
} catch (const std::bad_alloc& ex) {
    return {STITCH_SUPPORT_V1_ERROR_ENOMEM, mongo::ErrorCodes::InternalError, ex.what()};
} catch (const std::exception& ex) {
    return {STITCH_SUPPORT_V1_ERROR_UNKNOWN, mongo::ErrorCodes::InternalError, ex.what()};
} catch (...) {
    return {STITCH_SUPPORT_V1_ERROR_UNKNOWN,
            mongo::ErrorCodes::InternalError,
            "Unknown error encountered in performing requested stitch_support_v1 operation"};
}

/**
 * C interfaces that use enterCXX() must provide a tranlsateExceptionFallback() function that
 * populates a StatusForAPI<> object to indicate a double-fault error during error reporting. The
 * translateExceptionFallback() function gets called when translateException() throws, and it should
 * not include any code that may itself throw.
 *
 * We use an out param instead of returning the StatusForAPI<> object so as to avoid a std::string
 * copy that may allocate memory.
 */
static void translateExceptionFallback(StitchSupportStatusImpl& status) noexcept {
    status.error = STITCH_SUPPORT_V1_ERROR_IN_REPORTING_ERROR;
    status.exception_code = -1;
    setErrorMessageNoAlloc(status.what);
}

}  // namespace mongo

struct stitch_support_v1_status {
    mongo::StitchSupportStatusImpl statusImpl;
};

namespace mongo {
namespace {

StitchSupportStatusImpl* getStatusImpl(stitch_support_v1_status* status) {
    return status ? &status->statusImpl : nullptr;
}

using StitchSupportException = ExceptionForAPI<stitch_support_v1_error>;

ServiceContext* initialize() {
    srand(static_cast<unsigned>(curTimeMicros64()));

    // The global initializers can take arguments, which would normally be supplied on the command
    // line, but we assume that clients of this library will never want anything other than the
    // defaults for all configuration that would be controlled by these parameters.
    Status status =
        mongo::runGlobalInitializers(0 /* argc */, nullptr /* argv */, nullptr /* envp */);
    uassertStatusOKWithContext(status, "Global initialization failed");
    setGlobalServiceContext(ServiceContext::make());

    return getGlobalServiceContext();
}

struct ServiceContextDestructor {
    /**
     * This destructor gets called when the Stitch Library gets torn down, either by a call to
     * stitch_support_v1_fini() or when the process exits.
     */
    void operator()(mongo::ServiceContext* const serviceContext) const noexcept {
        Status status = mongo::runGlobalDeinitializers();
        uassertStatusOKWithContext(status, "Global deinitilization failed");

        setGlobalServiceContext(nullptr);
    }
};

using EmbeddedServiceContextPtr = std::unique_ptr<mongo::ServiceContext, ServiceContextDestructor>;

}  // namespace
}  // namespace mongo

struct stitch_support_v1_lib {
    stitch_support_v1_lib() : serviceContext(mongo::initialize()) {}

    stitch_support_v1_lib(const stitch_support_v1_lib&) = delete;
    void operator=(const stitch_support_v1_lib&) = delete;

    mongo::EmbeddedServiceContextPtr serviceContext;
};

struct stitch_support_v1_collator {
    stitch_support_v1_collator(std::unique_ptr<mongo::CollatorInterface> collator)
        : collator(std::move(collator)) {}
    std::unique_ptr<mongo::CollatorInterface> collator;
};

struct stitch_support_v1_matcher {
    stitch_support_v1_matcher(mongo::ServiceContext::UniqueClient client,
                              const mongo::BSONObj& filterBSON,
                              stitch_support_v1_collator* collator)
        : client(std::move(client)),
          opCtx(this->client->makeOperationContext()),
          matcher(filterBSON,
                  new mongo::ExpressionContext(opCtx.get(),
                                               collator ? collator->collator.get() : nullptr)){};

    mongo::ServiceContext::UniqueClient client;
    mongo::ServiceContext::UniqueOperationContext opCtx;
    mongo::Matcher matcher;
};

namespace mongo {
namespace {

std::unique_ptr<stitch_support_v1_lib> library;

stitch_support_v1_lib* stitch_lib_init() {
    if (library) {
        throw StitchSupportException{
            STITCH_SUPPORT_V1_ERROR_LIBRARY_ALREADY_INITIALIZED,
            "Cannot initialize the Stitch Support Library when it is already initialized."};
    }

    library = std::make_unique<stitch_support_v1_lib>();

    return library.get();
}

void stitch_lib_fini(stitch_support_v1_lib* const lib) {
    if (!lib) {
        throw StitchSupportException{
            STITCH_SUPPORT_V1_ERROR_INVALID_LIB_HANDLE,
            "Cannot close a `NULL` pointer referencing a Stitch Support Library Instance"};
    }

    if (!library) {
        throw StitchSupportException{
            STITCH_SUPPORT_V1_ERROR_LIBRARY_NOT_INITIALIZED,
            "Cannot close the Stitch Support Library when it is not initialized"};
    }

    if (library.get() != lib) {
        throw StitchSupportException{STITCH_SUPPORT_V1_ERROR_INVALID_LIB_HANDLE,
                                     "Invalid Stitch Support Library handle."};
    }

    library.reset();
}

stitch_support_v1_collator* collator_create(stitch_support_v1_lib* const lib,
                                            BSONObj collationSpecExpr) {
    if (!library) {
        throw StitchSupportException{STITCH_SUPPORT_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                                     "Cannot create a new collator when the Stitch Support Library "
                                     "is not yet initialized."};
    }

    if (library.get() != lib) {
        throw StitchSupportException{STITCH_SUPPORT_V1_ERROR_INVALID_LIB_HANDLE,
                                     "Cannot create a new collator when the Stitch Support Library "
                                     "is not yet initialized."};
    }

    auto statusWithCollator =
        CollatorFactoryInterface::get(lib->serviceContext.get())->makeFromBSON(collationSpecExpr);
    uassertStatusOK(statusWithCollator.getStatus());
    return new stitch_support_v1_collator(std::move(statusWithCollator.getValue()));
}

stitch_support_v1_matcher* matcher_create(stitch_support_v1_lib* const lib,
                                          BSONObj filter,
                                          stitch_support_v1_collator* collator) {
    if (!library) {
        throw StitchSupportException{STITCH_SUPPORT_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                                     "Cannot create a new matcher when the Stitch Support Library "
                                     "is not yet initialized."};
    }

    if (library.get() != lib) {
        throw StitchSupportException{STITCH_SUPPORT_V1_ERROR_INVALID_LIB_HANDLE,
                                     "Cannot create a new matcher when the Stitch Support Library "
                                     "is not yet initialized."};
    }

    return new stitch_support_v1_matcher(
        lib->serviceContext->makeClient("stitch_support"), filter.getOwned(), collator);
}

int capi_status_get_error(const stitch_support_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.error;
}

const char* capi_status_get_what(const stitch_support_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.what.c_str();
}

int capi_status_get_code(const stitch_support_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.exception_code;
}

}  // namespace
}  // namespace mongo

extern "C" {

stitch_support_v1_lib* MONGO_API_CALL stitch_support_v1_init(stitch_support_v1_status* status) {
    return enterCXX(mongo::getStatusImpl(status), [&]() { return mongo::stitch_lib_init(); });
}

int MONGO_API_CALL stitch_support_v1_fini(stitch_support_v1_lib* const lib,
                                          stitch_support_v1_status* const status) {
    return enterCXX(mongo::getStatusImpl(status), [&]() { return mongo::stitch_lib_fini(lib); });
}

int MONGO_API_CALL
stitch_support_v1_status_get_error(const stitch_support_v1_status* const status) {
    return mongo::capi_status_get_error(status);
}

const char* MONGO_API_CALL
stitch_support_v1_status_get_explanation(const stitch_support_v1_status* const status) {
    return mongo::capi_status_get_what(status);
}

int MONGO_API_CALL stitch_support_v1_status_get_code(const stitch_support_v1_status* const status) {
    return mongo::capi_status_get_code(status);
}

stitch_support_v1_status* MONGO_API_CALL stitch_support_v1_status_create(void) {
    return new stitch_support_v1_status;
}

void MONGO_API_CALL stitch_support_v1_status_destroy(stitch_support_v1_status* const status) {
    delete status;
}

stitch_support_v1_collator* MONGO_API_CALL stitch_support_v1_collator_create(
    stitch_support_v1_lib* lib, const char* collationBSON, stitch_support_v1_status* const status) {
    return enterCXX(mongo::getStatusImpl(status), [&]() {
        mongo::BSONObj collationSpecExpr(collationBSON);
        return mongo::collator_create(lib, collationSpecExpr);
    });
}

void MONGO_API_CALL stitch_support_v1_collator_destroy(stitch_support_v1_collator* const collator) {
    mongo::StitchSupportStatusImpl* nullStatus = nullptr;
    static_cast<void>(enterCXX(nullStatus, [=]() { delete collator; }));
}

stitch_support_v1_matcher* MONGO_API_CALL
stitch_support_v1_matcher_create(stitch_support_v1_lib* lib,
                                 const char* filterBSON,
                                 stitch_support_v1_collator* collator,
                                 stitch_support_v1_status* const statusPtr) {
    return enterCXX(mongo::getStatusImpl(statusPtr), [&]() {
        mongo::BSONObj filter(filterBSON);
        return mongo::matcher_create(lib, filter, collator);
    });
}

void MONGO_API_CALL stitch_support_v1_matcher_destroy(stitch_support_v1_matcher* const matcher) {
    mongo::StitchSupportStatusImpl* nullStatus = nullptr;
    static_cast<void>(enterCXX(nullStatus, [=]() { delete matcher; }));
}

int MONGO_API_CALL stitch_support_v1_check_match(stitch_support_v1_matcher* matcher,
                                                 const char* documentBSON,
                                                 bool* isMatch,
                                                 stitch_support_v1_status* statusPtr) {
    return enterCXX(mongo::getStatusImpl(statusPtr), [&]() {
        mongo::BSONObj document(documentBSON);
        *isMatch = matcher->matcher.matches(document, nullptr);
    });
}

}  // extern "C"
