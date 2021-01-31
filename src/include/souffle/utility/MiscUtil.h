/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file MiscUtil.h
 *
 * @brief Datalog project utilities
 *
 ***********************************************************************/

#pragma once

#include "souffle/utility/Iteration.h"
#include "souffle/utility/Types.h"
#include "tinyformat.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>

/**
 * Windows headers define these and they interfere with the standard library
 * functions.
 */
#undef min
#undef max

/**
 * On windows, the following gcc builtins are missing.
 *
 * In the case of popcountll, __popcnt64 is the windows equivalent.
 *
 * For ctz and ctzll, BitScanForward and BitScanForward64 are the respective
 * windows equivalents.  However ctz is used in a constexpr context, and we can't
 * use BitScanForward, so we implement it ourselves.
 */
#define __builtin_popcountll __popcnt64

#if defined(_MSC_VER)
constexpr unsigned long __builtin_ctz(unsigned long value) {
    unsigned long trailing_zeroes = 0;
    while ((value = value >> 1) ^ 1) {
        ++trailing_zeroes;
    }
    return trailing_zeroes;
}

inline unsigned long __builtin_ctzll(unsigned long long value) {
    unsigned long trailing_zero = 0;

    if (_BitScanForward64(&trailing_zero, value)) {
        return trailing_zero;
    } else {
        return 64;
    }
}
#endif  // _MSC_VER
#endif  // _WIN32

// -------------------------------------------------------------------------------
//                               Timing Utils
// -------------------------------------------------------------------------------

namespace souffle {

// a type def for a time point
using time_point = std::chrono::high_resolution_clock::time_point;
using std::chrono::microseconds;

// a shortcut for taking the current time
inline time_point now() {
    return std::chrono::high_resolution_clock::now();
}

// a shortcut for obtaining the time difference in milliseconds
inline long duration_in_us(const time_point& start, const time_point& end) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

// a shortcut for obtaining the time difference in nanoseconds
inline long duration_in_ns(const time_point& start, const time_point& end) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

// -------------------------------------------------------------------------------
//                             Cloning Utilities
// -------------------------------------------------------------------------------

namespace detail {
// TODO: This function is still used by ram::Node::clone() because it hasn't been
// converted to return Own<>.  Once converted, remove this.
template <typename D, typename B>
Own<D> downCast(B* ptr) {
    // ensure the clone operation casts to appropriate pointer
    static_assert(std::is_base_of_v<std::remove_const_t<B>, std::remove_const_t<D>>,
            "Needs to be able to downcast");
    return Own<D>(ptr);
}

template <typename D, typename B>
Own<D> downCast(Own<B> ptr) {
    // ensure the clone operation casts to appropriate pointer
    static_assert(std::is_base_of_v<std::remove_const_t<B>, std::remove_const_t<D>>,
            "Needs to be able to downcast");
    return Own<D>(static_cast<D*>(ptr.release()));
}

}  // namespace detail

template <typename A>
std::enable_if_t<!std::is_pointer_v<A> && !is_range_v<A>, Own<A>> clone(const A& node) {
    return detail::downCast<A>(node.clone());
}

template <typename A>
Own<A> clone(const A* node) {
    return node ? clone(*node) : nullptr;
}

template <typename A>
Own<A> clone(const Own<A>& node) {
    return clone(node.get());
}

/**
 * Clone a range
 */
template <typename R>
auto cloneRange(R const& range) {
    return makeTransformRange(std::begin(range), std::end(range), [](auto const& x) { return clone(x); });
}

/**
 * Clone a range, optionally allowing up-casting the result to D
 */
template <typename D = void, typename R, std::enable_if_t<is_range_v<R>, void*> = nullptr>
auto clone(R const& range) {
    auto rn = cloneRange(range);
    using ValueType = remove_cvref_t<decltype(**std::begin(range))>;
    using ResType = std::conditional_t<std::is_same_v<D, void>, ValueType, D>;
    return VecOwn<ResType>(rn.begin(), rn.end());
}

template <typename A, typename B>
auto clone(const std::pair<A, B>& p) {
    return std::make_pair(clone(p.first), clone(p.second));
}

// -------------------------------------------------------------------------------
//                             Comparison Utilities
// -------------------------------------------------------------------------------
/**
 * Compares two values referenced by a pointer where the case where both
 * pointers are null is also considered equivalent.
 */
template <typename T>
bool equal_ptr(const T* a, const T* b) {
    if (a == nullptr && b == nullptr) {
        return true;
    }
    if (a != nullptr && b != nullptr) {
        return *a == *b;
    }
    return false;
}

/**
 * Compares two values referenced by a pointer where the case where both
 * pointers are null is also considered equivalent.
 */
template <typename T>
bool equal_ptr(const Own<T>& a, const Own<T>& b) {
    return equal_ptr(a.get(), b.get());
}

/**
 * This class is used to tell as<> that cross-casting is allowed.
 * I use a named type rather than just a bool to make the code stand out.
 */
class AllowCrossCast {};

/**
 * Helpers for `dynamic_cast`ing without having to specify redundant type qualifiers.
 * e.g. `as<AstLiteral>(p)` instead of `as<AstLiteral>(p)`.
 */
template <typename B, typename CastType = void, typename A>
auto as(A* x) {
    if constexpr (!std::is_same_v<CastType, AllowCrossCast>) {
        static_assert(std::is_base_of_v<std::remove_const_t<A>, std::remove_const_t<B>>,
                "`as<B, A>` does not allow cross-type dyn casts. "
                "(i.e. `as<B, A>` where `B <: A` is not true.) "
                "Such a cast is likely a mistake or typo.");
    }
    return dynamic_cast<copy_const_t<A, B>*>(x);
}

template <typename B, typename CastType = void, typename A>
auto as(A& x) {
    return as<B, CastType>(&x);
}

template <typename B, typename CastType = void, typename A>
auto as(Own<A>& x) {
    return as<B, CastType>(x.get());
}

template <typename B, typename CastType = void, typename A>
auto as(const Own<A>& x) {
    return as<B, CastType>(x.get());
}

/**
 * Down-casts and checks the cast has succeeded
 */
template <typename B, typename CastType = void, typename A>
auto& asAssert(A&& a) {
    auto* cast = as<B, CastType>(std::forward<A>(a));
    assert(cast && "Invalid cast");
    return *cast;
}

/**
 * Checks if the object of type Source can be casted to type Destination.
 */
template <typename B, typename A>
bool isA(A* x) {
    // Dont't forward onto as<> - need to check cross-casting
    return dynamic_cast<copy_const_t<A, B>*>(x) != nullptr;
}

template <typename B, typename A>
auto isA(A& x) {
    return isA<B>(&x);
}

template <typename B, typename A>
bool isA(const Own<A>& x) {
    return isA<B>(x.get());
}

template <typename B, typename A>
bool isA(Own<A>& x) {
    return isA<B>(x.get());
}
// -------------------------------------------------------------------------------
//                               Error Utilities
// -------------------------------------------------------------------------------

template <typename... Args>
[[noreturn]] void fatal(const char* format, const Args&... args) {
    tfm::format(std::cerr, format, args...);
    std::cerr << "\n";
    assert(false && "fatal error; see std err");
    abort();
}

// HACK:  Workaround to suppress spurious reachability warnings.
#define UNREACHABLE_BAD_CASE_ANALYSIS fatal("unhandled switch branch");
}  // namespace souffle
