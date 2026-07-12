//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Declarations for exact rational arithmetic used by compile-time constant evaluation.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_RATIONAL_H
#define LLVMDSDL_SUPPORT_RATIONAL_H

#include <cstdint>
#include <optional>
#include <string>

namespace llvmdsdl
{

/// @file
/// @brief Exact rational number type used by constant expression evaluation.

/// @brief Immutable normalized rational value.
class Rational final
{
public:
    /// @brief Constructs zero (`0/1`).
    Rational();

    /// @brief Constructs and normalizes a rational value.
    /// @param[in] numerator Numerator value.
    /// @param[in] denominator Denominator value (must not be zero).
    Rational(std::int64_t numerator, std::int64_t denominator = 1);

    /// @brief Returns the normalized numerator.
    [[nodiscard]] std::int64_t numerator() const
    {
        return numerator_;
    }

    /// @brief Returns the normalized denominator.
    [[nodiscard]] std::int64_t denominator() const
    {
        return denominator_;
    }

    /// @brief Returns true when the value is an integer.
    [[nodiscard]] bool isInteger() const
    {
        return denominator_ == 1;
    }

    /// @brief Returns true when a preceding arithmetic operation overflowed 64-bit range.
    ///
    /// Arithmetic operators (`+`, `-`, `*`, `/`) evaluate in 128-bit intermediates and set this
    /// flag (rather than invoking signed-overflow undefined behaviour) when the reduced result no
    /// longer fits in `std::int64_t`. The flag is sticky: any operation involving an overflowed
    /// operand yields an overflowed result. Callers performing constant-expression evaluation
    /// should check this and surface a diagnostic instead of using the (clamped) value. Comparisons
    /// are always computed exactly in 128-bit and never overflow.
    [[nodiscard]] bool overflowed() const
    {
        return overflowed_;
    }

    /// @brief Converts to integer when the value is integral.
    /// @return Integer value or `std::nullopt` for non-integral rationals.
    [[nodiscard]] std::optional<std::int64_t> asInteger() const;

    /// @brief Returns a human-readable textual representation.
    /// @return String representation of the rational.
    [[nodiscard]] std::string str() const;

    /// @brief Adds two rationals.
    friend Rational operator+(const Rational& lhs, const Rational& rhs);

    /// @brief Subtracts two rationals.
    friend Rational operator-(const Rational& lhs, const Rational& rhs);

    /// @brief Multiplies two rationals.
    friend Rational operator*(const Rational& lhs, const Rational& rhs);

    /// @brief Divides two rationals.
    friend Rational operator/(const Rational& lhs, const Rational& rhs);

    /// @brief Equality comparison.
    friend bool operator==(const Rational& lhs, const Rational& rhs);

    /// @brief Inequality comparison.
    friend bool operator!=(const Rational& lhs, const Rational& rhs);

    /// @brief Strict weak ordering.
    friend bool operator<(const Rational& lhs, const Rational& rhs);

    /// @brief Less-or-equal comparison.
    friend bool operator<=(const Rational& lhs, const Rational& rhs);

    /// @brief Greater-than comparison.
    friend bool operator>(const Rational& lhs, const Rational& rhs);

    /// @brief Greater-or-equal comparison.
    friend bool operator>=(const Rational& lhs, const Rational& rhs);

private:
    /// @brief Computes greatest common divisor.
    static std::int64_t gcd(std::int64_t a, std::int64_t b);

    /// @brief Reduces sign and fraction by GCD.
    void normalize();

    /// @brief Builds a normalized rational from 128-bit numerator/denominator, marking the result
    ///        overflowed (and clamping to zero) when the reduced value does not fit in 64 bits.
    static Rational fromWide(__int128 numerator, __int128 denominator, bool operandOverflowed);

    /// @brief Normalized numerator.
    std::int64_t numerator_;

    /// @brief Normalized denominator (always positive).
    std::int64_t denominator_;

    /// @brief Set when a preceding arithmetic op overflowed 64-bit range; see `overflowed()`.
    bool overflowed_ = false;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_RATIONAL_H
