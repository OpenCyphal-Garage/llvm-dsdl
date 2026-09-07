//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// APIs for resolving backend helper symbols from lowered metadata and semantic field types.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_HELPER_SYMBOL_RESOLVER_H
#define LLVMDSDL_CODEGEN_HELPER_SYMBOL_RESOLVER_H

#include <optional>
#include <string>
#include <cstdint>

#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace llvmdsdl
{
enum class HelperBindingDirection;
struct LoweredFieldFacts;
struct LoweredSectionFacts;
struct SemanticFieldType;

/// @file
/// @brief Symbol-resolution helpers for lowered serdes helper functions.

/// @brief Resolves section capacity-check helper symbol.
/// @param[in] sectionFacts Lowered section facts.
/// @return Capacity-check helper symbol or empty string.
std::string resolveSectionCapacityCheckHelperSymbol(const LoweredSectionFacts* sectionFacts);

/// @brief Resolves section union-tag validation helper symbol.
/// @param[in] sectionFacts Lowered section facts.
/// @return Union-tag validation helper symbol or empty string.
std::string resolveSectionUnionTagValidateHelperSymbol(const LoweredSectionFacts* sectionFacts);

/// @brief Resolves section union-tag mask helper symbol.
/// @param[in] sectionFacts Lowered section facts.
/// @param[in] direction Serialise/deserialize direction.
/// @return Union-tag helper symbol or empty string.
std::string resolveSectionUnionTagMaskHelperSymbol(const LoweredSectionFacts* sectionFacts,
                                                   HelperBindingDirection     direction);

/// @brief Resolves scalar helper symbol.
/// @param[in] type Semantic field type.
/// @param[in] fieldFacts Lowered field facts.
/// @param[in] direction Serialise/deserialize direction.
/// @return Scalar helper symbol or empty string.
std::string resolveScalarHelperSymbol(const SemanticFieldType& type,
                                      const LoweredFieldFacts* fieldFacts,
                                      HelperBindingDirection   direction);

/// @brief Resolves variable-array helper descriptor.
/// @param[in] type Semantic field type.
/// @param[in] fieldFacts Lowered field facts.
/// @param[in] prefixBitsOverride Optional prefix width override.
/// @param[in] direction Serialise/deserialize direction.
/// @return Helper descriptor when variable-array helpers apply.
std::optional<ArrayLengthHelperDescriptor> resolveArrayLengthHelperDescriptor(
    const SemanticFieldType&     type,
    const LoweredFieldFacts*     fieldFacts,
    std::optional<std::uint32_t> prefixBitsOverride,
    HelperBindingDirection       direction);

/// @brief Resolves delimiter validation helper symbol.
/// @param[in] type Semantic field type.
/// @param[in] fieldFacts Lowered field facts.
/// @return Delimiter validation helper symbol or empty string.
std::string resolveDelimiterValidateHelperSymbol(const SemanticFieldType& type, const LoweredFieldFacts* fieldFacts);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_SYMBOL_RESOLVER_H
