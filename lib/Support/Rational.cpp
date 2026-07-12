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

#include <cstdint>
#include <cstdlib>
#include <sstream>

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

Rational::Rational()
    : numerator_(0)
    , denominator_(1)
{
}

Rational::Rational(std::int64_t numerator, std::int64_t denominator)
    : numerator_(numerator)
    , denominator_(denominator == 0 ? 1 : denominator)
{
    normalize();
}

std::optional<std::int64_t> Rational::asInteger() const
{
    if (!isInteger())
    {
        return std::nullopt;
    }
    return numerator_;
}

std::string Rational::str() const
{
    std::ostringstream out;
    out << numerator_;
    if (denominator_ != 1)
    {
        out << '/' << denominator_;
    }
    return out.str();
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
    numerator /= g;
    denominator /= g;
    if (!fitsInt64(numerator) || !fitsInt64(denominator))
    {
        // Reduced result exceeds 64-bit range: poison instead of truncating to a wrong value.
        out.numerator_   = 0;
        out.denominator_ = 1;
        out.overflowed_  = true;
        return out;
    }
    out.numerator_   = static_cast<std::int64_t>(numerator);
    out.denominator_ = static_cast<std::int64_t>(denominator);
    out.overflowed_  = operandOverflowed;
    return out;
}

Rational operator+(const Rational& lhs, const Rational& rhs)
{
    const __int128 n = static_cast<__int128>(lhs.numerator_) * rhs.denominator_ +
                       static_cast<__int128>(rhs.numerator_) * lhs.denominator_;
    const __int128 d = static_cast<__int128>(lhs.denominator_) * rhs.denominator_;
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator-(const Rational& lhs, const Rational& rhs)
{
    const __int128 n = static_cast<__int128>(lhs.numerator_) * rhs.denominator_ -
                       static_cast<__int128>(rhs.numerator_) * lhs.denominator_;
    const __int128 d = static_cast<__int128>(lhs.denominator_) * rhs.denominator_;
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator*(const Rational& lhs, const Rational& rhs)
{
    const __int128 n = static_cast<__int128>(lhs.numerator_) * rhs.numerator_;
    const __int128 d = static_cast<__int128>(lhs.denominator_) * rhs.denominator_;
    return Rational::fromWide(n, d, lhs.overflowed_ || rhs.overflowed_);
}

Rational operator/(const Rational& lhs, const Rational& rhs)
{
    if (rhs.numerator_ == 0)
    {
        return Rational(0, 1);
    }
    const __int128 n = static_cast<__int128>(lhs.numerator_) * rhs.denominator_;
    const __int128 d = static_cast<__int128>(lhs.denominator_) * rhs.numerator_;
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
    // order-preserving. Both products are int64*int64 and therefore fit exactly in 128 bits, so this
    // never overflows (unlike a 64-bit cross-multiply).
    return static_cast<__int128>(lhs.numerator_) * rhs.denominator_ <
           static_cast<__int128>(rhs.numerator_) * lhs.denominator_;
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

std::int64_t Rational::gcd(std::int64_t a, std::int64_t b)
{
    // Compute in 128-bit so `INT64_MIN` (whose 64-bit absolute value is unrepresentable) is safe.
    return static_cast<std::int64_t>(wideGcd(a, b));
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
        // Move the sign onto the numerator. `INT64_MIN` cannot be negated in 64-bit, so evaluate the
        // reduced fraction in 128-bit; if it no longer fits, poison rather than invoke UB.
        const __int128 g = wideGcd(numerator_, denominator_);
        const __int128 n = -(static_cast<__int128>(numerator_) / g);
        const __int128 d = -(static_cast<__int128>(denominator_) / g);
        if (!fitsInt64(n) || !fitsInt64(d))
        {
            numerator_   = 0;
            denominator_ = 1;
            overflowed_  = true;
            return;
        }
        numerator_   = static_cast<std::int64_t>(n);
        denominator_ = static_cast<std::int64_t>(d);
        return;
    }
    const __int128 g = wideGcd(numerator_, denominator_);
    numerator_       = static_cast<std::int64_t>(static_cast<__int128>(numerator_) / g);
    denominator_     = static_cast<std::int64_t>(static_cast<__int128>(denominator_) / g);
}

}  // namespace llvmdsdl
