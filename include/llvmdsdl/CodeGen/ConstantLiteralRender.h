//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared constant-literal rendering helpers for code generation backends.
///
/// These APIs centralize backend-specific literal syntax for booleans, numeric
/// constants, and strings emitted from semantic constant values.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_CONSTANT_LITERAL_RENDER_H
#define LLVMDSDL_CODEGEN_CONSTANT_LITERAL_RENDER_H

#include <cstdint>
#include <string>

#include "llvmdsdl/Semantics/Evaluator.h"

namespace llvmdsdl
{

struct TypeExprAST;

/// @brief Numeric class of a constant's declared type, used to render range-correct literals.
enum class ConstantNumericClass
{
    /// @brief Non-numeric (bool/string) or unknown; render from the value alone.
    Other,

    /// @brief Signed integer target (`intN`).
    SignedInt,

    /// @brief Unsigned integer target (`uintN`, `byte`, `utf8`).
    UnsignedInt,

    /// @brief Floating-point target (`floatN`).
    Float,
};

/// @brief The declared type of a constant, sufficient to pick a range-correct literal spelling.
///
/// Needed because the same 64-bit value renders differently by target: a full-width `uint64` needs an
/// unsigned suffix, `INT64_MIN` cannot be written as a bare negated literal in Rust/C, an
/// integer-valued `float` constant must carry a decimal point, and a TypeScript 64-bit integer is a
/// `bigint` (an `n`-suffixed literal) even for a small value.
struct ConstantTypeInfo
{
    /// @brief Numeric class of the declared type.
    ConstantNumericClass numericClass = ConstantNumericClass::Other;

    /// @brief Declared bit width (e.g. 64 for `uint64`); 0 when unknown.
    std::uint32_t bitLength = 0;
};

/// @brief Extracts the constant-rendering type info from a declared type expression.
/// @param[in] type Declared type of the constant attribute.
/// @return Numeric class and bit width; `Other`/0 for non-primitive or non-numeric types.
[[nodiscard]] ConstantTypeInfo makeConstantTypeInfo(const TypeExprAST& type);

/// @brief Target language for constant-literal rendering.
enum class ConstantLiteralLanguage
{
    /// @brief C literal syntax.
    C,

    /// @brief C++ literal syntax.
    Cpp,

    /// @brief Rust literal syntax.
    Rust,

    /// @brief Go literal syntax.
    Go,

    /// @brief TypeScript literal syntax.
    TypeScript,

    /// @brief Python literal syntax.
    Python,
};

/// @brief Renders a semantic constant value as source code for one language.
/// @param[in] language Target language syntax.
/// @param[in] value Semantic constant value.
/// @param[in] typeInfo Declared type of the constant, used to render range-correct integer/float
///            literals (suffixes, `INT64_MIN`, integer-valued floats, TypeScript `bigint`). Defaults
///            to `Other`/0, which preserves value-only rendering for callers without type context.
/// @return Rendered constant expression.
std::string renderConstantLiteral(ConstantLiteralLanguage language, const Value& value, ConstantTypeInfo typeInfo = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_CONSTANT_LITERAL_RENDER_H
