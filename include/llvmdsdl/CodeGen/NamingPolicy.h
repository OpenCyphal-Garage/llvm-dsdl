//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared naming-policy helpers for backend code generation.
///
/// This interface centralizes language-specific identifier sanitization and
/// common case projections (snake/pascal/upper-snake) used by emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_NAMING_POLICY_H
#define LLVMDSDL_CODEGEN_NAMING_POLICY_H

#include <string>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Target language for identifier naming projection.
enum class CodegenNamingLanguage
{
    /// @brief C naming policy.
    C,

    /// @brief C++ naming policy.
    Cpp,

    /// @brief Rust naming policy.
    Rust,

    /// @brief Go naming policy.
    Go,

    /// @brief TypeScript naming policy.
    TypeScript,

    /// @brief Python naming policy.
    Python,
};

/// @brief Returns true when an identifier is a keyword in the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return True when the identifier is reserved.
bool codegenIsKeyword(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Sanitizes one identifier for the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return Language-safe identifier.
std::string codegenSanitizeIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into snake_case and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe snake_case identifier.
std::string codegenToSnakeCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into PascalCase and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe PascalCase identifier.
std::string codegenToPascalCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into UPPER_SNAKE_CASE and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe UPPER_SNAKE_CASE identifier.
std::string codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Assigns collision-free identifiers to a set of distinct source names.
///
/// The case-folding projections above are many-to-one, so two distinct, spec-valid DSDL names (e.g.
/// `fooBar` and `foo_bar`, which both fold to `foo_bar`/`FooBar`) can map to the same identifier and
/// produce duplicate struct fields / object keys / attributes in a generated type. This allocator
/// applies a chosen projection to each source name in declaration order and appends `_2`, `_3`, ... to
/// the second and subsequent names that would otherwise collide (checked against every previously
/// assigned identifier, including suffixed ones), so the mapping stays injective. Source names must be
/// distinct (the frontend already rejects exact duplicate attribute names).
class CodegenIdentifierAllocator final
{
public:
    /// @brief Builds the allocation over @p sourceNames (declaration order) using @p transform.
    /// @param[in] sourceNames Distinct source names, in the order they should claim identifiers.
    /// @param[in] transform Projection from a source name to its (possibly colliding) base identifier.
    CodegenIdentifierAllocator(llvm::ArrayRef<std::string>                       sourceNames,
                               llvm::function_ref<std::string(llvm::StringRef)> transform);

    /// @brief Returns the collision-free identifier assigned to @p sourceName.
    /// @param[in] sourceName A name provided at construction.
    /// @return The unique identifier; falls back to @p sourceName if it was not registered.
    [[nodiscard]] std::string get(llvm::StringRef sourceName) const;

private:
    /// @brief Source name -> assigned unique identifier.
    llvm::StringMap<std::string> assigned_;
};

/// @brief Builds a collision-free UPPER_SNAKE_CASE allocation over @p names (declaration order).
///
/// Convenience wrapper around @ref CodegenIdentifierAllocator for constant names, whose projection
/// (`codegenToUpperSnakeCaseIdentifier`) is many-to-one and can otherwise emit two `#define`s / `const`s
/// with the same name (e.g. `fooBar` and `foo_bar` both fold to `FOO_BAR`).
/// @param[in] language Naming language.
/// @param[in] names Distinct source names in declaration order.
/// @return Allocator mapping each source name to a unique UPPER_SNAKE_CASE identifier.
[[nodiscard]] CodegenIdentifierAllocator codegenUpperSnakeAllocator(CodegenNamingLanguage        language,
                                                                    llvm::ArrayRef<std::string> names);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NAMING_POLICY_H
