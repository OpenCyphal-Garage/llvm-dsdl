//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Integer arithmetic helpers.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_INTEGERMATH_H
#define LLVMDSDL_SUPPORT_INTEGERMATH_H

#include <cstdint>
#include <limits>
#include <numeric>

namespace llvmdsdl
{

/// @brief Checked int64 addition.
[[nodiscard]] inline bool checkedAdd(const std::int64_t a, const std::int64_t b, std::int64_t& out)
{
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, &out);
#else
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
    {
        return false;
    }
    out = a + b;
    return true;
#endif
}

/// @brief Checked int64 multiplication.
[[nodiscard]] inline bool checkedMultiply(const std::int64_t a, const std::int64_t b, std::int64_t& out)
{
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, &out);
#else
    const __int128 product = static_cast<__int128>(a) * b;
    if (product < std::numeric_limits<std::int64_t>::min() || product > std::numeric_limits<std::int64_t>::max())
    {
        return false;
    }
    out = static_cast<std::int64_t>(product);
    return true;
#endif
}

/// @brief Saturating int64 addition for non-negative inputs.
[[nodiscard]] inline std::int64_t saturatingAddNonNegative(const std::int64_t a, const std::int64_t b)
{
    std::int64_t out = 0;
    return checkedAdd(a, b, out) ? out : std::numeric_limits<std::int64_t>::max();
}

/// @brief Saturating int64 multiplication for non-negative inputs.
[[nodiscard]] inline std::int64_t saturatingMultiplyNonNegative(const std::int64_t a, const std::int64_t b)
{
    std::int64_t out = 0;
    return checkedMultiply(a, b, out) ? out : std::numeric_limits<std::int64_t>::max();
}

/// @brief Rounds `value >= 0` up to the next multiple of `alignment >= 1`, saturating on overflow.
[[nodiscard]] inline std::int64_t saturatingRoundUpToMultipleNonNegative(const std::int64_t value,
                                                                         const std::int64_t alignment)
{
    const std::int64_t rem = value % alignment;
    return rem == 0 ? value : saturatingAddNonNegative(value, alignment - rem);
}

/// @brief Returns `value mod divisor` in `[0, divisor)` for positive divisors.
[[nodiscard]] constexpr std::int64_t euclideanModulo(const std::int64_t value, const std::int64_t divisor)
{
    const std::int64_t rem = value % divisor;
    return rem >= 0 ? rem : rem + divisor;
}

/// @brief Computes `lcm(a, b)` for positive inputs when the result does not exceed `limit`.
[[nodiscard]] inline bool lcmAtMost(const std::int64_t a,
                                    const std::int64_t b,
                                    const std::int64_t limit,
                                    std::int64_t&      result)
{
    if (a < 1 || b < 1 || limit < 1)
    {
        return false;
    }
    const std::int64_t g = std::gcd(a, b);
    const std::int64_t step = a / g;
    if (step > limit / b)
    {
        return false;
    }
    result = step * b;
    return true;
}

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_INTEGERMATH_H
