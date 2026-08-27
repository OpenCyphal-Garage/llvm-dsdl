//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements normalized rational arithmetic utilities.
///
/// Rational helpers provide stable arithmetic primitives used by expression evaluation and semantic calculations.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/Rational.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace llvmdsdl
{

namespace
{

/// @brief Greatest common divisor over 128-bit operands (safe for `INT64_MIN`, unlike `llabs`).
__int128 wideGcd(__int128 a, __int128 b)
{
    if (a < 0)
    {
        a = -a;
    }
    if (b < 0)
    {
        b = -b;
    }
    while (b != 0)
    {
        const __int128 t = a % b;
        a                = b;
        b                = t;
    }
    return a == 0 ? 1 : a;
}

constexpr __int128 kInt64Max = static_cast<__int128>(INT64_MAX);
constexpr __int128 kInt64Min = static_cast<__int128>(INT64_MIN);

bool fitsInt64(__int128 v)
{
    return v >= kInt64Min && v <= kInt64Max;
}

}  // namespace

std::string wideToString(__int128 value)
{
    if (value == 0)
    {
        return "0";
    }
    const bool negative = value < 0;
    // Accumulate the magnitude in unsigned 128-bit so the most-negative value (whose positive is
    // unrepresentable as a signed 128-bit) is still handled without overflow.
    unsigned __int128 magnitude =
        negative ? (static_cast<unsigned __int128>(-(value + 1)) + 1U) : static_cast<unsigned __int128>(value);
    std::string digits;
    while (magnitude > 0)
    {
        digits.push_back(static_cast<char>('0' + static_cast<int>(magnitude % 10U)));
        magnitude /= 10U;
    }
    if (negative)
    {
        digits.push_back('-');
    }
    std::ranges::reverse(digits);
    return digits;
}

Rational::Rational()
    : numerator_(0)
    , denominator_(1)
{
}

Rational::Rational(__int128 numerator, __int128 denominator)
    : numerator_(numerator)
    , denominator_(denominator == 0 ? 1 : denominator)
{
    normalize();
}

std::optional<std::int64_t> Rational::asInteger() const
{
    if (!isInteger() || !fitsInt64(numerator_))
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(numerator_);
}

std::optional<__int128> Rational::asWideInteger() const
{
    if (!isInteger())
    {
        return std::nullopt;
    }
    return numerator_;
}

std::string Rational::str() const
{
    std::string out = wideToString(numerator_);
    if (denominator_ != 1)
    {
        out += '/';
        out += wideToString(denominator_);
    }
    return out;
}

Rational Rational::fromWide(__int128 numerator, __int128 denominator, bool operandOverflowed)
{
    Rational out;
    if (denominator == 0)
    {
        out.numerator_   = 0;
        out.denominator_ = 1;
        out.overflowed_  = operandOverflowed;
        return out;
    }
    if (denominator < 0)
    {
        numerator   = -numerator;
        denominator = -denominator;
    }
    const __int128 g = wideGcd(numerator, denominator);
    out.numerator_   = numerator / g;
    out.denominator_ = denominator / g;
    out.overflowed_  = operandOverflowed;
    return out;
}

Rational operator+(const Rational& lhs, const Rational& rhs)
{
    __int128   t1 = 0;
    __int128   t2 = 0;
    __int128   n  = 0;
    __int128   d  = 0;
    const bool of = __builtin_mul_overflow(lhs.numerator_, rhs.denominator_, &t1) ||
                    __builtin_mul_overflow(rhs.numerator_, lhs.denominator_, &t2) ||
                    __builtin_add_overflow(t1, t2, &n) ||
                    __builtin_mul_overflow(lhs.denominator_, rhs.denominator_, &d);
    if (of)
    {
        return Rational::fromWide(0, 1, true);
    }
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator-(const Rational& lhs, const Rational& rhs)
{
    __int128   t1 = 0;
    __int128   t2 = 0;
    __int128   n  = 0;
    __int128   d  = 0;
    const bool of = __builtin_mul_overflow(lhs.numerator_, rhs.denominator_, &t1) ||
                    __builtin_mul_overflow(rhs.numerator_, lhs.denominator_, &t2) ||
                    __builtin_sub_overflow(t1, t2, &n) ||
                    __builtin_mul_overflow(lhs.denominator_, rhs.denominator_, &d);
    if (of)
    {
        return Rational::fromWide(0, 1, true);
    }
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator*(const Rational& lhs, const Rational& rhs)
{
    __int128   n  = 0;
    __int128   d  = 0;
    const bool of = __builtin_mul_overflow(lhs.numerator_, rhs.numerator_, &n) ||
                    __builtin_mul_overflow(lhs.denominator_, rhs.denominator_, &d);
    if (of)
    {
        return Rational::fromWide(0, 1, true);
    }
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator/(const Rational& lhs, const Rational& rhs)
{
    if (rhs.numerator_ == 0)
    {
        return {0, 1};
    }
    __int128   n  = 0;
    __int128   d  = 0;
    const bool of = __builtin_mul_overflow(lhs.numerator_, rhs.denominator_, &n) ||
                    __builtin_mul_overflow(lhs.denominator_, rhs.numerator_, &d);
    if (of)
    {
        return Rational::fromWide(0, 1, true);
    }
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

bool operator==(const Rational& lhs, const Rational& rhs)
{
    return lhs.numerator_ == rhs.numerator_ && lhs.denominator_ == rhs.denominator_;
}

bool operator!=(const Rational& lhs, const Rational& rhs)
{
    return !(lhs == rhs);
}

bool operator<(const Rational& lhs, const Rational& rhs)
{
    // Denominators are always positive after normalization, so the cross-multiplied comparison is
    // order-preserving. Values are bounded to `[INT64_MIN, UINT64_MAX]` in practice, so the products
    // fit 128 bits; guard with an overflow check for pathological coprime fractions and fall back to
    // extended-precision ordering (equality is decided exactly by `operator==`).
    __int128 p1 = 0;
    __int128 p2 = 0;
    if (!__builtin_mul_overflow(lhs.numerator_, rhs.denominator_, &p1) &&
        !__builtin_mul_overflow(rhs.numerator_, lhs.denominator_, &p2))
    {
        return p1 < p2;
    }
    return (static_cast<long double>(lhs.numerator_) / static_cast<long double>(lhs.denominator_)) <
           (static_cast<long double>(rhs.numerator_) / static_cast<long double>(rhs.denominator_));
}

bool operator<=(const Rational& lhs, const Rational& rhs)
{
    return lhs < rhs || lhs == rhs;
}

bool operator>(const Rational& lhs, const Rational& rhs)
{
    return rhs < lhs;
}

bool operator>=(const Rational& lhs, const Rational& rhs)
{
    return rhs <= lhs;
}

void Rational::normalize()
{
    if (denominator_ == 0)
    {
        denominator_ = 1;
        numerator_   = 0;
        return;
    }
    if (denominator_ < 0)
    {
        numerator_   = -numerator_;
        denominator_ = -denominator_;
    }
    const __int128 g = wideGcd(numerator_, denominator_);
    numerator_ /= g;
    denominator_ /= g;
}

}  // namespace llvmdsdl
