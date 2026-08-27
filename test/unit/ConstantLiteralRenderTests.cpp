//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>
#include <string>

#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"
#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvmdsdl/Support/Rational.h"

#include "UnitTests.h"

bool runConstantLiteralRenderTests()
{
    using llvmdsdl::ConstantLiteralLanguage;
    using llvmdsdl::Value;
    using llvmdsdl::renderConstantLiteral;

    const Value booleanValue{true};
    if (renderConstantLiteral(ConstantLiteralLanguage::Python, booleanValue) != "True" ||
        renderConstantLiteral(ConstantLiteralLanguage::TypeScript, booleanValue) != "true")
    {
        std::cerr << "boolean literal rendering mismatch\n";
        return false;
    }

    const Value integerValue{llvmdsdl::Rational(42, 1)};
    if (renderConstantLiteral(ConstantLiteralLanguage::C, integerValue) != "42" ||
        renderConstantLiteral(ConstantLiteralLanguage::Rust, integerValue) != "42")
    {
        std::cerr << "integer literal rendering mismatch\n";
        return false;
    }

    const Value fractionValue{llvmdsdl::Rational(3, 2)};
    if (renderConstantLiteral(ConstantLiteralLanguage::C, fractionValue) != "((double)3/(double)2)")
    {
        std::cerr << "C fractional literal rendering mismatch\n";
        return false;
    }
    if (renderConstantLiteral(ConstantLiteralLanguage::Rust, fractionValue) != "(3f64 / 2f64)")
    {
        std::cerr << "Rust fractional literal rendering mismatch\n";
        return false;
    }
    if (renderConstantLiteral(ConstantLiteralLanguage::Python, fractionValue) != "(3 / 2)")
    {
        std::cerr << "Python fractional literal rendering mismatch\n";
        return false;
    }

    const Value stringValue{std::string("a\"b\\c")};
    if (renderConstantLiteral(ConstantLiteralLanguage::TypeScript, stringValue) != R"("a\"b\\c")")
    {
        std::cerr << "string escaping mismatch\n";
        return false;
    }

    const Value charValue{std::string("x")};
    if (renderConstantLiteral(ConstantLiteralLanguage::C, charValue) != "'x'" ||
        renderConstantLiteral(ConstantLiteralLanguage::Cpp, charValue) != "'x'" ||
        renderConstantLiteral(ConstantLiteralLanguage::Go, charValue) != "\"x\"")
    {
        std::cerr << "single-character literal rendering mismatch\n";
        return false;
    }

    using llvmdsdl::ConstantNumericClass;
    using llvmdsdl::ConstantTypeInfo;

    // INT64_MIN cannot be a bare negated literal in Rust (positive part overflows i64) or safely in C
    // (the magnitude is unsigned); it must render as i64::MIN / (-INT64_MAX - 1).
    const ConstantTypeInfo int64Info{ConstantNumericClass::SignedInt, 64};
    const Value            floorValue{llvmdsdl::Rational(static_cast<__int128>(INT64_MIN), 1)};
    if (renderConstantLiteral(ConstantLiteralLanguage::Rust, floorValue, int64Info) != "i64::MIN" ||
        renderConstantLiteral(ConstantLiteralLanguage::C, floorValue, int64Info) != "(-9223372036854775807LL - 1)" ||
        renderConstantLiteral(ConstantLiteralLanguage::Cpp, floorValue, int64Info) != "(-9223372036854775807LL - 1)" ||
        renderConstantLiteral(ConstantLiteralLanguage::Go, floorValue, int64Info) != "-9223372036854775808" ||
        renderConstantLiteral(ConstantLiteralLanguage::TypeScript, floorValue, int64Info) != "-9223372036854775808n" ||
        renderConstantLiteral(ConstantLiteralLanguage::Python, floorValue, int64Info) != "-9223372036854775808")
    {
        std::cerr << "INT64_MIN literal rendering mismatch\n";
        return false;
    }

    // A full-width uint64 (> INT64_MAX) needs an unsigned suffix in Rust/C and is a bigint in TS.
    const ConstantTypeInfo uint64Info{ConstantNumericClass::UnsignedInt, 64};
    const Value            maskValue{llvmdsdl::Rational((static_cast<__int128>(1) << 64) - 1, 1)};
    if (renderConstantLiteral(ConstantLiteralLanguage::Rust, maskValue, uint64Info) != "18446744073709551615u64" ||
        renderConstantLiteral(ConstantLiteralLanguage::C, maskValue, uint64Info) != "18446744073709551615ULL" ||
        renderConstantLiteral(ConstantLiteralLanguage::Go, maskValue, uint64Info) != "18446744073709551615" ||
        renderConstantLiteral(ConstantLiteralLanguage::TypeScript, maskValue, uint64Info) != "18446744073709551615n" ||
        renderConstantLiteral(ConstantLiteralLanguage::Python, maskValue, uint64Info) != "18446744073709551615")
    {
        std::cerr << "UINT64_MAX literal rendering mismatch\n";
        return false;
    }

    // A small 64-bit integer is still a bigint in TypeScript (n suffix) but a plain literal elsewhere.
    const Value smallU64{llvmdsdl::Rational(5, 1)};
    if (renderConstantLiteral(ConstantLiteralLanguage::TypeScript, smallU64, uint64Info) != "5n" ||
        renderConstantLiteral(ConstantLiteralLanguage::Rust, smallU64, uint64Info) != "5" ||
        renderConstantLiteral(ConstantLiteralLanguage::C, smallU64, uint64Info) != "5")
    {
        std::cerr << "small bigint literal rendering mismatch\n";
        return false;
    }

    // An integer-valued float constant must carry a decimal point (a bare `2` fails to type-check as
    // f64 in Rust); TypeScript numbers are already floating so `2` is fine.
    const ConstantTypeInfo floatInfo{ConstantNumericClass::Float, 64};
    const Value            twoPointZero{llvmdsdl::Rational(2, 1)};
    if (renderConstantLiteral(ConstantLiteralLanguage::Rust, twoPointZero, floatInfo) != "2.0" ||
        renderConstantLiteral(ConstantLiteralLanguage::C, twoPointZero, floatInfo) != "2.0" ||
        renderConstantLiteral(ConstantLiteralLanguage::Go, twoPointZero, floatInfo) != "2.0" ||
        renderConstantLiteral(ConstantLiteralLanguage::Python, twoPointZero, floatInfo) != "2.0" ||
        renderConstantLiteral(ConstantLiteralLanguage::TypeScript, twoPointZero, floatInfo) != "2")
    {
        std::cerr << "integer-valued float literal rendering mismatch\n";
        return false;
    }

    return true;
}
