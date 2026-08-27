//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared constant-literal rendering helpers for code generation.
///
/// This module renders semantic constant values into language-specific literal
/// expressions while preserving backend behavioral parity.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ConstantLiteralRender.h"

#include <cstdint>
#include <sstream>

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Support/Rational.h"

namespace llvmdsdl
{
namespace
{

constexpr __int128 kInt64Min = static_cast<__int128>(INT64_MIN);
constexpr __int128 kInt64Max = static_cast<__int128>(INT64_MAX);

/// @brief Renders an integer constant whose exact value is `wide`, correct for the target language.
///
/// The extreme values only arise for the 64-bit types, so their spelling is inferred from the value:
/// `INT64_MIN` cannot be written as a bare negated literal in Rust (`-(9223372036854775808)` overflows
/// `i64`) or safely in C (the magnitude is unsigned), and a `uint64` above `INT64_MAX` needs an
/// unsigned suffix. TypeScript models any 64-bit integer as `bigint`, so those literals take an `n`
/// suffix even for small values. Go and Python integer literals are arbitrary precision, so the bare
/// decimal is always correct.
std::string renderIntegerConstant(const ConstantLiteralLanguage language,
                                  const __int128                wide,
                                  const ConstantTypeInfo        typeInfo)
{
    const std::string dec = wideToString(wide);
    switch (language)
    {
    case ConstantLiteralLanguage::Go:
    case ConstantLiteralLanguage::Python:
        return dec;
    case ConstantLiteralLanguage::TypeScript:
        return (typeInfo.bitLength > 53U) ? (dec + "n") : dec;
    case ConstantLiteralLanguage::Rust:
        if (wide == kInt64Min)
        {
            return "i64::MIN";
        }
        if (wide > kInt64Max)
        {
            return dec + "u64";
        }
        return dec;
    case ConstantLiteralLanguage::C:
    case ConstantLiteralLanguage::Cpp:
        if (wide == kInt64Min)
        {
            // The bare literal is unsigned (and negating it is a wrong/warned value); build the floor
            // as `-(INT64_MAX) - 1` so the expression is a signed 64-bit `long long`.
            return "(-9223372036854775807LL - 1)";
        }
        if (wide > kInt64Max)
        {
            return dec + "ULL";
        }
        return dec;
    }
    return dec;
}

std::string quoteStringLiteral(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value)
    {
        if (c == '\\' || c == '"')
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string quoteCharLiteral(const char value)
{
    if (value == '\\' || value == '\'')
    {
        return std::string("'\\") + value + "'";
    }
    return std::string("'") + value + "'";
}

/// @brief Renders an integer-valued rational as a floating literal (so a `float` constant such as
///        `2.0` is not emitted as the integer `2`, which fails to type-check as `f64` in Rust).
std::string renderIntegerValuedFloat(const ConstantLiteralLanguage language, const __int128 wide)
{
    const std::string dec = wideToString(wide);
    switch (language)
    {
    case ConstantLiteralLanguage::TypeScript:
        // A TypeScript `number` is already floating; a decimal point is unnecessary and `2` suffices.
        return dec;
    default:
        // `2.0` is a valid floating literal in C/C++/Rust/Go/Python (Rust infers `f64` in a typed
        // `const` context).
        return dec + ".0";
    }
}

}  // namespace

ConstantTypeInfo makeConstantTypeInfo(const TypeExprAST& type)
{
    ConstantTypeInfo info;
    const auto*      prim = std::get_if<PrimitiveTypeExprAST>(&type.scalar);
    if (prim == nullptr)
    {
        return info;
    }
    info.bitLength = prim->bitLength;
    switch (prim->kind)
    {
    case PrimitiveKind::SignedInt:
        info.numericClass = ConstantNumericClass::SignedInt;
        break;
    case PrimitiveKind::UnsignedInt:
    case PrimitiveKind::Byte:
    case PrimitiveKind::Utf8:
        info.numericClass = ConstantNumericClass::UnsignedInt;
        break;
    case PrimitiveKind::Float:
        info.numericClass = ConstantNumericClass::Float;
        break;
    case PrimitiveKind::Bool:
        break;
    }
    return info;
}

std::string renderConstantLiteral(const ConstantLiteralLanguage language,
                                  const Value&                  value,
                                  const ConstantTypeInfo        typeInfo)
{
    if (const auto* booleanValue = std::get_if<bool>(&value.data))
    {
        switch (language)
        {
        case ConstantLiteralLanguage::Python:
            return *booleanValue ? "True" : "False";
        default:
            return *booleanValue ? "true" : "false";
        }
    }

    if (const auto* rationalValue = std::get_if<Rational>(&value.data))
    {
        const bool floatTarget = typeInfo.numericClass == ConstantNumericClass::Float;
        if (rationalValue->isInteger())
        {
            const __int128 wide = rationalValue->asWideInteger().value();
            return floatTarget ? renderIntegerValuedFloat(language, wide)
                               : renderIntegerConstant(language, wide, typeInfo);
        }
        const std::string  num = wideToString(rationalValue->numerator());
        const std::string  den = wideToString(rationalValue->denominator());
        std::ostringstream out;
        switch (language)
        {
        case ConstantLiteralLanguage::C:
        case ConstantLiteralLanguage::Cpp:
            out << "((double)" << num << "/(double)" << den << ")";
            return out.str();
        case ConstantLiteralLanguage::Rust:
            out << "(" << num << "f64 / " << den << "f64)";
            return out.str();
        case ConstantLiteralLanguage::Go:
        case ConstantLiteralLanguage::TypeScript:
        case ConstantLiteralLanguage::Python:
            out << "(" << num << " / " << den << ")";
            return out.str();
        }
    }

    if (const auto* stringValue = std::get_if<std::string>(&value.data))
    {
        if ((language == ConstantLiteralLanguage::C || language == ConstantLiteralLanguage::Cpp) &&
            stringValue->size() == 1U)
        {
            return quoteCharLiteral((*stringValue)[0]);
        }
        return quoteStringLiteral(*stringValue);
    }

    return value.str();
}

}  // namespace llvmdsdl
