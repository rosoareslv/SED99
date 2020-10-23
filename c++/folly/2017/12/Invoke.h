/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <functional>
#include <type_traits>

#include <folly/Traits.h>

/**
 *  include or backport:
 *  * std::invoke
 *  * std::invoke_result
 *  * std::invoke_result_t
 *  * std::is_invocable
 *  * std::is_invocable_r
 *  * std::is_nothrow_invocable
 *  * std::is_nothrow_invocable_r
 */

#if __cpp_lib_invoke >= 201411 || _MSC_VER

namespace folly {

/* using override */ using std::invoke;

}

#else

namespace folly {

//  mimic: std::invoke, C++17
template <typename F, typename... Args>
constexpr auto invoke(F&& f, Args&&... args) noexcept(
    noexcept(std::forward<F>(f)(std::forward<Args>(args)...)))
    -> decltype(std::forward<F>(f)(std::forward<Args>(args)...)) {
  return std::forward<F>(f)(std::forward<Args>(args)...);
}
template <typename M, typename C, typename... Args>
constexpr auto invoke(M(C::*d), Args&&... args)
    -> decltype(std::mem_fn(d)(std::forward<Args>(args)...)) {
  return std::mem_fn(d)(std::forward<Args>(args)...);
}

} // namespace folly

#endif

// Only available in >= MSVC 2017 15.3
#if __cpp_lib_is_invocable >= 201703 || _MSC_VER >= 1911

namespace folly {

/* using override */ using std::invoke_result;
/* using override */ using std::invoke_result_t;
/* using override */ using std::is_invocable;
/* using override */ using std::is_invocable_r;
/* using override */ using std::is_nothrow_invocable;
/* using override */ using std::is_nothrow_invocable_r;

}

#else

namespace folly {

namespace detail {

template <typename F, typename... Args>
using invoke_result_ =
    decltype(invoke(std::declval<F>(), std::declval<Args>()...));

template <typename F, typename... Args>
struct invoke_nothrow_
    : std::integral_constant<
          bool,
          noexcept(invoke(std::declval<F>(), std::declval<Args>()...))> {};

//  from: http://en.cppreference.com/w/cpp/types/result_of, CC-BY-SA

template <typename Void, typename F, typename... Args>
struct invoke_result {};

template <typename F, typename... Args>
struct invoke_result<void_t<invoke_result_<F, Args...>>, F, Args...> {
  using type = invoke_result_<F, Args...>;
};

template <typename Void, typename F, typename... Args>
struct is_invocable : std::false_type {};

template <typename F, typename... Args>
struct is_invocable<void_t<invoke_result_<F, Args...>>, F, Args...>
    : std::true_type {};

template <typename Void, typename R, typename F, typename... Args>
struct is_invocable_r : std::false_type {};

template <typename R, typename F, typename... Args>
struct is_invocable_r<void_t<invoke_result_<F, Args...>>, R, F, Args...>
    : std::is_convertible<invoke_result_<F, Args...>, R> {};

template <typename Void, typename F, typename... Args>
struct is_nothrow_invocable : std::false_type {};

template <typename F, typename... Args>
struct is_nothrow_invocable<void_t<invoke_result_<F, Args...>>, F, Args...>
    : invoke_nothrow_<F, Args...> {};

template <typename Void, typename R, typename F, typename... Args>
struct is_nothrow_invocable_r : std::false_type {};

template <typename R, typename F, typename... Args>
struct is_nothrow_invocable_r<void_t<invoke_result_<F, Args...>>, R, F, Args...>
    : StrictConjunction<
        std::is_convertible<invoke_result_<F, Args...>, R>,
        invoke_nothrow_<F, Args...>> {};

} // namespace detail

//  mimic: std::invoke_result, C++17
template <typename F, typename... Args>
struct invoke_result : detail::invoke_result<void, F, Args...> {};

//  mimic: std::invoke_result_t, C++17
template <typename F, typename... Args>
using invoke_result_t = typename invoke_result<F, Args...>::type;

//  mimic: std::is_invocable, C++17
template <typename F, typename... Args>
struct is_invocable : detail::is_invocable<void, F, Args...> {};

//  mimic: std::is_invocable_r, C++17
template <typename R, typename F, typename... Args>
struct is_invocable_r : detail::is_invocable_r<void, R, F, Args...> {};

//  mimic: std::is_nothrow_invocable, C++17
template <typename F, typename... Args>
struct is_nothrow_invocable : detail::is_nothrow_invocable<void, F, Args...> {};

//  mimic: std::is_nothrow_invocable_r, C++17
template <typename R, typename F, typename... Args>
struct is_nothrow_invocable_r
    : detail::is_nothrow_invocable_r<void, R, F, Args...> {};

} // namespace folly

#endif

/***
 *  FOLLY_CREATE_MEMBER_INVOKE_TRAITS
 *
 *  Used to create traits container, bound to a specific member-invocable name,
 *  with the following member traits types and aliases:
 *
 *  * invoke_result
 *  * invoke_result_t
 *  * is_invocable
 *  * is_invocable_r
 *  * is_nothrow_invocable
 *  * is_nothrow_invocable_r
 *
 *  The container also has a static member function:
 *
 *  * invoke
 *
 *  These members have behavior matching the behavior of C++17's corresponding
 *  invocation traits types, aliases, and functions, but substituting canonical
 *  invocation with member invocation.
 *
 *  Example:
 *
 *    FOLLY_CREATE_MEMBER_INVOKE_TRAITS(foo_invoke_traits, foo);
 *
 *  The traits container type `foo_invoke_traits` is generated in the current
 *  namespace and has the listed member types and aliases. They may be used as
 *  follows:
 *
 *    struct CanFoo {
 *      int foo(Bar const&) { return 1; }
 *      int foo(Car&&) noexcept { return 2; }
 *    };
 *
 *    using traits = foo_invoke_traits;
 *
 *    traits::invoke(CanFoo{}, Bar{}) // 1
 *
 *    traits::invoke_result<CanFoo, Bar&&> // has member
 *    traits::invoke_result_t<CanFoo, Bar&&> // int
 *    traits::invoke_result<CanFoo, Bar&> // empty
 *    traits::invoke_result_t<CanFoo, Bar&> // error
 *
 *    traits::is_invocable<CanFoo, Bar&&>::value // true
 *    traits::is_invocable<CanFoo, Bar&>::value // false
 *
 *    traits::is_invocable_r<int, CanFoo, Bar&&>::value // true
 *    traits::is_invocable_r<char*, CanFoo, Bar&&>::value // false
 *
 *    traits::is_nothrow_invocable<CanFoo, Bar&&>::value // false
 *    traits::is_nothrow_invocable<CanFoo, Car&&>::value // true
 *
 *    traits::is_nothrow_invocable<int, CanFoo, Bar&&>::value // false
 *    traits::is_nothrow_invocable<char*, CanFoo, Bar&&>::value // false
 *    traits::is_nothrow_invocable<int, CanFoo, Car&&>::value // true
 *    traits::is_nothrow_invocable<char*, CanFoo, Car&&>::value // false
 */
#define FOLLY_CREATE_MEMBER_INVOKE_TRAITS(classname, membername)              \
  struct classname {                                                          \
   private:                                                                   \
    template <typename T>                                                     \
    using v_ = ::folly::void_t<T>;                                            \
    template <typename F, typename... Args>                                   \
    using result_ =                                                           \
        decltype(::std::declval<F>().membername(::std::declval<Args>()...));  \
    template <typename F, typename... Args>                                   \
    struct nothrow_ : std::integral_constant<                                 \
                          bool,                                               \
                          noexcept(::std::declval<F>().membername(            \
                              ::std::declval<Args>()...))> {};                \
                                                                              \
    template <typename, typename F, typename... Args>                         \
    struct invoke_result_ {};                                                 \
    template <typename F, typename... Args>                                   \
    struct invoke_result_<v_<result_<F, Args...>>, F, Args...> {              \
      using type = result_<F, Args...>;                                       \
    };                                                                        \
                                                                              \
    template <typename, typename F, typename... Args>                         \
    struct is_invocable_ : ::std::false_type {};                              \
    template <typename F, typename... Args>                                   \
    struct is_invocable_<v_<result_<F, Args...>>, F, Args...>                 \
        : ::std::true_type {};                                                \
                                                                              \
    template <typename, typename R, typename F, typename... Args>             \
    struct is_invocable_r_ : ::std::false_type {};                            \
    template <typename R, typename F, typename... Args>                       \
    struct is_invocable_r_<v_<result_<F, Args...>>, R, F, Args...>            \
        : ::std::is_convertible<result_<F, Args...>, R> {};                   \
                                                                              \
    template <typename, typename F, typename... Args>                         \
    struct is_nothrow_invocable_ : ::std::false_type {};                      \
    template <typename F, typename... Args>                                   \
    struct is_nothrow_invocable_<v_<result_<F, Args...>>, F, Args...>         \
        : nothrow_<F, Args...> {};                                            \
                                                                              \
    template <typename, typename R, typename F, typename... Args>             \
    struct is_nothrow_invocable_r_ : ::std::false_type {};                    \
    template <typename R, typename F, typename... Args>                       \
    struct is_nothrow_invocable_r_<v_<result_<F, Args...>>, R, F, Args...>    \
        : ::folly::StrictConjunction<                                         \
              ::std::is_convertible<result_<F, Args...>, R>,                  \
              nothrow_<F, Args...>> {};                                       \
                                                                              \
   public:                                                                    \
    template <typename F, typename... Args>                                   \
    struct invoke_result : invoke_result_<void, F, Args...> {};               \
    template <typename F, typename... Args>                                   \
    using invoke_result_t = typename invoke_result<F, Args...>::type;         \
    template <typename F, typename... Args>                                   \
    struct is_invocable : is_invocable_<void, F, Args...> {};                 \
    template <typename R, typename F, typename... Args>                       \
    struct is_invocable_r : is_invocable_r_<void, R, F, Args...> {};          \
    template <typename F, typename... Args>                                   \
    struct is_nothrow_invocable : is_nothrow_invocable_<void, F, Args...> {}; \
    template <typename R, typename F, typename... Args>                       \
    struct is_nothrow_invocable_r                                             \
        : is_nothrow_invocable_r_<void, R, F, Args...> {};                    \
                                                                              \
    template <typename F, typename... Args>                                   \
    static constexpr result_<F, Args...> invoke(                              \
        F&& f,                                                                \
        Args&&... args) noexcept(nothrow_<F, Args...>::value) {               \
      return std::forward<F>(f).membername(std::forward<Args>(args)...);      \
    }                                                                         \
  }
