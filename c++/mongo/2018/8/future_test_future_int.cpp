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

#include "mongo/platform/basic.h"

#include "mongo/util/future.h"

#include "mongo/stdx/thread.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

#include "mongo/util/future_test_utils.h"

namespace mongo {
namespace {

MONGO_STATIC_ASSERT(std::is_same<FutureContinuationResult<std::function<void()>>, void>::value);
MONGO_STATIC_ASSERT(std::is_same<FutureContinuationResult<std::function<Status()>>, void>::value);
MONGO_STATIC_ASSERT(
    std::is_same<FutureContinuationResult<std::function<Future<void>()>>, void>::value);
MONGO_STATIC_ASSERT(std::is_same<FutureContinuationResult<std::function<int()>>, int>::value);
MONGO_STATIC_ASSERT(
    std::is_same<FutureContinuationResult<std::function<StatusWith<int>()>>, int>::value);
MONGO_STATIC_ASSERT(
    std::is_same<FutureContinuationResult<std::function<Future<int>()>>, int>::value);
MONGO_STATIC_ASSERT(
    std::is_same<FutureContinuationResult<std::function<int(bool)>, bool>, int>::value);

template <typename T>
auto overloadCheck(T) -> FutureContinuationResult<std::function<std::true_type(bool)>, T>;
auto overloadCheck(...) -> std::false_type;

MONGO_STATIC_ASSERT(decltype(overloadCheck(bool()))::value);          // match.
MONGO_STATIC_ASSERT(!decltype(overloadCheck(std::string()))::value);  // SFINAE-failure.


TEST(Future, Success_getLvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; }, [](Future<int>&& fut) { ASSERT_EQ(fut.get(), 1); });
}

TEST(Future, Success_getConstLvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; }, [](const Future<int>& fut) { ASSERT_EQ(fut.get(), 1); });
}

TEST(Future, Success_getRvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) { ASSERT_EQ(std::move(fut).get(), 1); });
}

TEST(Future, Success_getNothrowLvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) { ASSERT_EQ(fut.getNoThrow(), 1); });
}

TEST(Future, Success_getNothrowConstLvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](const Future<int>& fut) { ASSERT_EQ(fut.getNoThrow(), 1); });
}

TEST(Future, Success_getNothrowRvalue) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) { ASSERT_EQ(std::move(fut).getNoThrow(), 1); });
}

TEST(Future, Success_getAsync) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            auto pf = makePromiseFuture<int>();
            std::move(fut).getAsync([outside = pf.promise.share()](StatusWith<int> sw) mutable {
                ASSERT_OK(sw);
                outside.emplaceValue(sw.getValue());
            });
            ASSERT_EQ(std::move(pf.future).get(), 1);
        });
}

TEST(Future, Fail_getLvalue) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) { ASSERT_THROWS_failStatus(fut.get()); });
}

TEST(Future, Fail_getConstLvalue) {
    FUTURE_FAIL_TEST<int>([](const Future<int>& fut) { ASSERT_THROWS_failStatus(fut.get()); });
}

TEST(Future, Fail_getRvalue) {
    FUTURE_FAIL_TEST<int>(
        [](Future<int>&& fut) { ASSERT_THROWS_failStatus(std::move(fut).get()); });
}

TEST(Future, Fail_getNothrowLvalue) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) { ASSERT_EQ(fut.getNoThrow(), failStatus()); });
}

TEST(Future, Fail_getNothrowConstLvalue) {
    FUTURE_FAIL_TEST<int>(
        [](const Future<int>& fut) { ASSERT_EQ(fut.getNoThrow(), failStatus()); });
}

TEST(Future, Fail_getNothrowRvalue) {
    FUTURE_FAIL_TEST<int>(
        [](Future<int>&& fut) { ASSERT_EQ(std::move(fut).getNoThrow(), failStatus()); });
}

TEST(Future, Fail_getAsync) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        auto pf = makePromiseFuture<int>();
        std::move(fut).getAsync([outside = pf.promise.share()](StatusWith<int> sw) mutable {
            ASSERT(!sw.isOK());
            outside.setError(sw.getStatus());
        });
        ASSERT_EQ(std::move(pf.future).getNoThrow(), failStatus());
    });
}

TEST(Future, Success_isReady) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            const auto id = stdx::this_thread::get_id();
                            while (!fut.isReady()) {
                            }
                            std::move(fut).getAsync([&](StatusWith<int> status) {
                                ASSERT_EQ(stdx::this_thread::get_id(), id);
                                ASSERT_EQ(status, 1);
                            });

                        });
}

TEST(Future, Fail_isReady) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        const auto id = stdx::this_thread::get_id();
        while (!fut.isReady()) {
        }
        std::move(fut).getAsync([&](StatusWith<int> status) {
            ASSERT_EQ(stdx::this_thread::get_id(), id);
            ASSERT_NOT_OK(status);
        });

    });
}

TEST(Future, isReady_TSAN_OK) {
    bool done = false;
    auto fut = async([&] {
        done = true;
        return 1;
    });
    while (!fut.isReady()) {
    }
    // ASSERT(done);  // Data Race! Uncomment to make sure TSAN is working.
    (void)fut.get();
    ASSERT(done);
}

TEST(Future, Success_thenSimple) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut).then([](int i) { return i + 2; }).get(), 3);
                        });
}

TEST(Future, Success_thenSimpleAuto) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut).then([](auto i) { return i + 2; }).get(), 3);
                        });
}

TEST(Future, Success_thenVoid) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            ASSERT_EQ(
                std::move(fut).then([](int i) { ASSERT_EQ(i, 1); }).then([] { return 3; }).get(),
                3);
        });
}

TEST(Future, Success_thenStatus) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .then([](int i) {
                                              ASSERT_EQ(i, 1);
                                              return Status::OK();
                                          })
                                          .then([] { return 3; })
                                          .get(),
                                      3);
                        });
}

TEST(Future, Success_thenError_Status) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            auto fut2 = std::move(fut).then(
                                [](int i) { return Status(ErrorCodes::BadValue, "oh no!"); });
                            MONGO_STATIC_ASSERT(std::is_same<decltype(fut2), Future<void>>::value);
                            ASSERT_THROWS(fut2.get(), ExceptionFor<ErrorCodes::BadValue>);
                        });
}

TEST(Future, Success_thenError_StatusWith) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            auto fut2 = std::move(fut).then(
                [](int i) { return StatusWith<double>(ErrorCodes::BadValue, "oh no!"); });
            MONGO_STATIC_ASSERT(std::is_same<decltype(fut2), Future<double>>::value);
            ASSERT_THROWS(fut2.get(), ExceptionFor<ErrorCodes::BadValue>);
        });
}

TEST(Future, Success_thenFutureImmediate) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            ASSERT_EQ(
                std::move(fut).then([](int i) { return Future<int>::makeReady(i + 2); }).get(), 3);
        });
}

TEST(Future, Success_thenFutureReady) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .then([](int i) {
                                              auto pf = makePromiseFuture<int>();
                                              pf.promise.emplaceValue(i + 2);
                                              return std::move(pf.future);
                                          })
                                          .get(),
                                      3);
                        });
}

TEST(Future, Success_thenFutureAsync) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            ASSERT_EQ(std::move(fut).then([](int i) { return async([i] { return i + 2; }); }).get(),
                      3);
        });
}

TEST(Future, Success_thenFutureAsyncThrow) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .then([](int i) {
                                              uasserted(ErrorCodes::BadValue, "oh no!");
                                              return Future<int>();
                                          })
                                          .getNoThrow(),
                                      ErrorCodes::BadValue);
                        });
}

TEST(Future, Fail_thenSimple) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .then([](int i) {
                          FAIL("then() callback was called");
                          return int();
                      })
                      .getNoThrow(),
                  failStatus());
    });
}

TEST(Future, Fail_thenFutureAsync) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .then([](int i) {
                          FAIL("then() callback was called");
                          return Future<int>();
                      })
                      .getNoThrow(),
                  failStatus());
    });
}

TEST(Future, Success_onErrorSimple) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .onError([](Status) {
                                              FAIL("onError() callback was called");
                                              return 0;
                                          })
                                          .then([](int i) { return i + 2; })
                                          .get(),
                                      3);
                        });
}

TEST(Future, Success_onErrorFutureAsync) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .onError([](Status) {
                                              FAIL("onError() callback was called");
                                              return Future<int>();
                                          })
                                          .then([](int i) { return i + 2; })
                                          .get(),
                                      3);
                        });
}

TEST(Future, Fail_onErrorSimple) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .getNoThrow(),
                  3);
    });
}

TEST(Future, Fail_onErrorError_throw) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        auto fut2 = std::move(fut).onError([](Status s) -> int {
            ASSERT_EQ(s, failStatus());
            uasserted(ErrorCodes::BadValue, "oh no!");
        });
        ASSERT_EQ(fut2.getNoThrow(), ErrorCodes::BadValue);
    });
}

TEST(Future, Fail_onErrorError_StatusWith) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        auto fut2 = std::move(fut).onError([](Status s) {
            ASSERT_EQ(s, failStatus());
            return StatusWith<int>(ErrorCodes::BadValue, "oh no!");
        });
        ASSERT_EQ(fut2.getNoThrow(), ErrorCodes::BadValue);
    });
}

TEST(Future, Fail_onErrorFutureImmediate) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return Future<int>::makeReady(3);
                      })
                      .get(),
                  3);
    });
}

TEST(Future, Fail_onErrorFutureReady) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          auto pf = makePromiseFuture<int>();
                          pf.promise.emplaceValue(3);
                          return std::move(pf.future);
                      })
                      .get(),
                  3);
    });
}

TEST(Future, Fail_onErrorFutureAsync) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return async([] { return 3; });
                      })
                      .get(),
                  3);
    });
}

TEST(Future, Success_onErrorCode) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            ASSERT_EQ(std::move(fut)
                                          .onError<ErrorCodes::InternalError>([](Status) {
                                              FAIL("onError<code>() callback was called");
                                              return 0;
                                          })
                                          .then([](int i) { return i + 2; })
                                          .get(),
                                      3);
                        });
}

TEST(Future, Fail_onErrorCodeMatch) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        auto res =
            std::move(fut)
                .onError([](Status s) {
                    ASSERT_EQ(s, failStatus());
                    return StatusWith<int>(ErrorCodes::InternalError, "");
                })
                .onError<ErrorCodes::InternalError>([](Status&&) { return StatusWith<int>(3); })
                .getNoThrow();
        ASSERT_EQ(res, 3);
    });
}

TEST(Future, Fail_onErrorCodeMatchFuture) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        auto res = std::move(fut)
                       .onError([](Status s) {
                           ASSERT_EQ(s, failStatus());
                           return StatusWith<int>(ErrorCodes::InternalError, "");
                       })
                       .onError<ErrorCodes::InternalError>([](Status&&) { return Future<int>(3); })
                       .getNoThrow();
        ASSERT_EQ(res, 3);
    });
}

TEST(Future, Fail_onErrorCodeMismatch) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .onError<ErrorCodes::InternalError>([](Status s) -> int {
                          FAIL("Why was this called?") << s;
                          MONGO_UNREACHABLE;
                      })
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .getNoThrow(),
                  3);
    });
}


TEST(Future, Success_tap) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            bool tapCalled = false;
                            ASSERT_EQ(std::move(fut)
                                          .tap([&tapCalled](int i) {
                                              ASSERT_EQ(i, 1);
                                              tapCalled = true;
                                          })
                                          .then([](int i) { return i + 2; })
                                          .get(),
                                      3);
                            ASSERT(tapCalled);
                        });
}

TEST(Future, Success_tapError) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            ASSERT_EQ(std::move(fut)
                          .tapError([](Status s) { FAIL("tapError() callback was called"); })
                          .then([](int i) { return i + 2; })
                          .get(),
                      3);
        });
}

TEST(Future, Success_tapAll_StatusWith) {
    FUTURE_SUCCESS_TEST([] { return 1; },
                        [](Future<int>&& fut) {
                            bool tapCalled = false;
                            ASSERT_EQ(std::move(fut)
                                          .tapAll([&tapCalled](StatusWith<int> sw) {
                                              ASSERT_EQ(sw, 1);
                                              tapCalled = true;
                                          })
                                          .then([](int i) { return i + 2; })
                                          .get(),
                                      3);
                            ASSERT(tapCalled);
                        });
}

TEST(Future, Success_tapAll_Overloaded) {
    FUTURE_SUCCESS_TEST(
        [] { return 1; },
        [](Future<int>&& fut) {
            struct Callback {
                void operator()(int i) {
                    ASSERT_EQ(i, 1);
                    called = true;
                }
                void operator()(Status status) {
                    FAIL("Status overload called with ") << status;
                }
                bool called = false;
            };
            Callback callback;

            ASSERT_EQ(
                std::move(fut).tapAll(std::ref(callback)).then([](int i) { return i + 2; }).get(),
                3);
            ASSERT(callback.called);
        });
}

TEST(Future, Fail_tap) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        ASSERT_EQ(std::move(fut)
                      .tap([](int i) { FAIL("tap() callback was called"); })
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .get(),
                  3);
    });
}

TEST(Future, Fail_tapError) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        bool tapCalled = false;
        ASSERT_EQ(std::move(fut)
                      .tapError([&tapCalled](Status s) {
                          ASSERT_EQ(s, failStatus());
                          tapCalled = true;
                      })
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .get(),
                  3);
        ASSERT(tapCalled);
    });
}

TEST(Future, Fail_tapAll_StatusWith) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        bool tapCalled = false;
        ASSERT_EQ(std::move(fut)
                      .tapAll([&tapCalled](StatusWith<int> sw) {
                          ASSERT_EQ(sw.getStatus(), failStatus());
                          tapCalled = true;
                      })
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .get(),
                  3);
        ASSERT(tapCalled);
    });
}

TEST(Future, Fail_tapAll_Overloaded) {
    FUTURE_FAIL_TEST<int>([](Future<int>&& fut) {
        struct Callback {
            void operator()(int i) {
                FAIL("int overload called with ") << i;
            }
            void operator()(Status status) {
                ASSERT_EQ(status, failStatus());
                called = true;
            }
            bool called = false;
        };
        Callback callback;

        ASSERT_EQ(std::move(fut)
                      .tapAll(std::ref(callback))
                      .onError([](Status s) {
                          ASSERT_EQ(s, failStatus());
                          return 3;
                      })
                      .get(),
                  3);

        ASSERT(callback.called);
    });
}
}  // namespace
}  // namespace mongo
