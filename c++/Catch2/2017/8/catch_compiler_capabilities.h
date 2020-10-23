/*
 *  Created by Phil on 15/04/2013.
 *  Copyright 2013 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#ifndef TWOBLUECUBES_CATCH_COMPILER_CAPABILITIES_HPP_INCLUDED
#define TWOBLUECUBES_CATCH_COMPILER_CAPABILITIES_HPP_INCLUDED

// Detect a number of compiler features - by compiler
// The following features are defined:
//
// CATCH_CONFIG_COUNTER : is the __COUNTER__ macro supported?
// CATCH_CONFIG_WINDOWS_SEH : is Windows SEH supported?
// CATCH_CONFIG_POSIX_SIGNALS : are POSIX signals supported?
// ****************
// Note to maintainers: if new toggles are added please document them
// in configuration.md, too
// ****************

// In general each macro has a _NO_<feature name> form
// (e.g. CATCH_CONFIG_NO_POSIX_SIGNALS) which disables the feature.
// Many features, at point of detection, define an _INTERNAL_ macro, so they
// can be combined, en-mass, with the _NO_ forms later.


#ifdef __cplusplus

#  if __cplusplus >= 201402L
#    define CATCH_CPP14_OR_GREATER
#  endif

#endif

#ifdef __clang__


#       define CATCH_INTERNAL_SUPPRESS_ETD_WARNINGS \
            _Pragma( "clang diagnostic push" ) \
            _Pragma( "clang diagnostic ignored \"-Wexit-time-destructors\"" )
#       define CATCH_INTERNAL_UNSUPPRESS_ETD_WARNINGS \
            _Pragma( "clang diagnostic pop" )

#       define CATCH_INTERNAL_SUPPRESS_PARENTHESES_WARNINGS \
            _Pragma( "clang diagnostic push" ) \
            _Pragma( "clang diagnostic ignored \"-Wparentheses\"" )
#       define CATCH_INTERNAL_UNSUPPRESS_PARENTHESES_WARNINGS \
            _Pragma( "clang diagnostic pop" )

#endif // __clang__


////////////////////////////////////////////////////////////////////////////////
// We know some environments not to support full POSIX signals
#if defined(__CYGWIN__) || defined(__QNX__)

#   if !defined(CATCH_CONFIG_POSIX_SIGNALS)
#       define CATCH_INTERNAL_CONFIG_NO_POSIX_SIGNALS
#   endif

#endif

#ifdef __OS400__
#       define CATCH_INTERNAL_CONFIG_NO_POSIX_SIGNALS
#       define CATCH_CONFIG_COLOUR_NONE
#endif

////////////////////////////////////////////////////////////////////////////////
// Cygwin
#ifdef __CYGWIN__

// Required for some versions of Cygwin to declare gettimeofday
// see: http://stackoverflow.com/questions/36901803/gettimeofday-not-declared-in-this-scope-cygwin
#   define _BSD_SOURCE

#endif // __CYGWIN__

////////////////////////////////////////////////////////////////////////////////
// Visual C++
#ifdef _MSC_VER

#define CATCH_INTERNAL_CONFIG_WINDOWS_SEH

#endif // _MSC_VER

////////////////////////////////////////////////////////////////////////////////

// All supported compilers support COUNTER macro,
//but user still might want to turn it off
#define CATCH_INTERNAL_CONFIG_COUNTER


// Now set the actual defines based on the above + anything the user has configured

// Use of __COUNTER__ is suppressed if __JETBRAINS_IDE__ is #defined (meaning we're being parsed by a JetBrains IDE for
// analytics) because, at time of writing, __COUNTER__ is not properly handled by it.
// This does not affect compilation
#if defined(CATCH_INTERNAL_CONFIG_COUNTER) && !defined(CATCH_CONFIG_NO_COUNTER) && !defined(CATCH_CONFIG_COUNTER) && !defined(__JETBRAINS_IDE__)
#   define CATCH_CONFIG_COUNTER
#endif
#if defined(CATCH_INTERNAL_CONFIG_WINDOWS_SEH) && !defined(CATCH_CONFIG_NO_WINDOWS_SEH) && !defined(CATCH_CONFIG_WINDOWS_SEH)
#   define CATCH_CONFIG_WINDOWS_SEH
#endif
// This is set by default, because we assume that unix compilers are posix-signal-compatible by default.
#if !defined(CATCH_INTERNAL_CONFIG_NO_POSIX_SIGNALS) && !defined(CATCH_CONFIG_NO_POSIX_SIGNALS) && !defined(CATCH_CONFIG_POSIX_SIGNALS)
#   define CATCH_CONFIG_POSIX_SIGNALS
#endif

#if !defined(CATCH_INTERNAL_SUPPRESS_PARENTHESES_WARNINGS)
#   define CATCH_INTERNAL_SUPPRESS_PARENTHESES_WARNINGS
#   define CATCH_INTERNAL_UNSUPPRESS_PARENTHESES_WARNINGS
#endif
#if !defined(CATCH_INTERNAL_SUPPRESS_ETD_WARNINGS)
#   define CATCH_INTERNAL_SUPPRESS_ETD_WARNINGS
#   define CATCH_INTERNAL_UNSUPPRESS_ETD_WARNINGS
#endif


#endif // TWOBLUECUBES_CATCH_COMPILER_CAPABILITIES_HPP_INCLUDED

