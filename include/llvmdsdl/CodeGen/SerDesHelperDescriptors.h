//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Descriptor types and builder APIs for backend serdes helper generation.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SERDES_HELPER_DESCRIPTORS_H
#define LLVMDSDL_CODEGEN_SERDES_HELPER_DESCRIPTORS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "llvmdsdl/Frontend/AST.h"

namespace llvmdsdl
{
struct SemanticField;
struct SemanticFieldType;
struct SemanticSection;

/// @file
/// @brief Structured descriptor types for generated serdes helpers.

/// @brief Descriptor for section-level capacity-check helpers.
struct CapacityCheckHelperDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Required minimum capacity in bits.
    std::int64_t requiredBits{0};
};

/// @brief Descriptor for union-tag validation helpers.
struct UnionTagValidateHelperDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Allowed union tag values.
    std::vector<std::int64_t> allowedTags;
};

/// @brief Section-level helper descriptor bundle.
struct SharedSerDesHelperDescriptors final
{
    /// @brief Optional capacity-check helper.
    std::optional<CapacityCheckHelperDescriptor> capacityCheck;

    /// @brief Optional union-tag validation helper.
    std::optional<UnionTagValidateHelperDescriptor> unionTagValidate;
};

/// @brief Descriptor for variable-array length helpers.
struct ArrayLengthHelperDescriptor final
{
    /// @brief Prefix encode/decode helper symbol.
    std::string prefixSymbol;

    /// @brief Array-length validation helper symbol.
    std::string validateSymbol;

    /// @brief Prefix bit width.
    std::uint32_t prefixBits{0};

    /// @brief Maximum allowed element count.
    std::int64_t capacity{0};
};

/// @brief Scalar helper symbol set used to resolve directional bindings.
struct ScalarHelperSymbols final
{
    /// @brief Serialise unsigned helper symbol.
    std::string serUnsignedSymbol;

    /// @brief Deserialise unsigned helper symbol.
    std::string deserUnsignedSymbol;

    /// @brief Serialise signed helper symbol.
    std::string serSignedSymbol;

    /// @brief Deserialise signed helper symbol.
    std::string deserSignedSymbol;

    /// @brief Serialise float helper symbol.
    std::string serFloatSymbol;

    /// @brief Deserialise float helper symbol.
    std::string deserFloatSymbol;
};

/// @brief Scalar helper family.
enum class ScalarHelperKind
{

    /// @brief Unsigned helper family.
    Unsigned,

    /// @brief Signed helper family.
    Signed,

    /// @brief Floating-point helper family.
    Float,
};

/// @brief Descriptor for scalar helper pair.
struct ScalarHelperDescriptor final
{
    /// @brief Helper family.
    ScalarHelperKind kind{ScalarHelperKind::Unsigned};

    /// @brief Serialise helper symbol.
    std::string serSymbol;

    /// @brief Deserialise helper symbol.
    std::string deserSymbol;

    /// @brief Scalar bit width.
    std::uint32_t bitLength{0};

    /// @brief Cast mode semantics.
    CastMode castMode{CastMode::Truncated};
};

/// @brief Descriptor for delimiter validation helper.
struct DelimiterValidateHelperDescriptor final
{
    /// @brief Helper symbol.
    std::string symbol;
};

/// @brief Builds section-level helper descriptors.
/// @param[in] section Semantic section.
/// @param[in] capacityCheckSymbol Capacity-check helper symbol.
/// @param[in] unionTagValidateSymbol Union-tag validation helper symbol.
/// @return Shared helper descriptor bundle.
SharedSerDesHelperDescriptors buildSharedSerDesHelperDescriptors(const SemanticSection& section,
                                                                 const std::string&     capacityCheckSymbol,
                                                                 const std::string&     unionTagValidateSymbol);

/// @brief Builds array-length descriptor for a semantic field.
/// @param[in] field Semantic field.
/// @param[in] prefixBitsOverride Optional prefix width override.
/// @param[in] prefixSymbol Prefix helper symbol.
/// @param[in] validateSymbol Validation helper symbol.
/// @return Descriptor when applicable.
std::optional<ArrayLengthHelperDescriptor> buildArrayLengthHelperDescriptor(
    const SemanticField&         field,
    std::optional<std::uint32_t> prefixBitsOverride,
    const std::string&           prefixSymbol,
    const std::string&           validateSymbol);

/// @brief Builds array-length descriptor for a semantic field type.
/// @param[in] type Semantic field type.
/// @param[in] prefixBitsOverride Optional prefix width override.
/// @param[in] prefixSymbol Prefix helper symbol.
/// @param[in] validateSymbol Validation helper symbol.
/// @return Descriptor when applicable.
std::optional<ArrayLengthHelperDescriptor> buildArrayLengthHelperDescriptor(
    const SemanticFieldType&     type,
    std::optional<std::uint32_t> prefixBitsOverride,
    const std::string&           prefixSymbol,
    const std::string&           validateSymbol);

/// @brief Builds scalar helper descriptor for a semantic field.
/// @param[in] field Semantic field.
/// @param[in] symbols Candidate helper symbols.
/// @return Descriptor when scalar helpers apply.
std::optional<ScalarHelperDescriptor> buildScalarHelperDescriptor(const SemanticField&       field,
                                                                  const ScalarHelperSymbols& symbols);

/// @brief Builds scalar helper descriptor for a semantic field type.
/// @param[in] type Semantic field type.
/// @param[in] symbols Candidate helper symbols.
/// @return Descriptor when scalar helpers apply.
std::optional<ScalarHelperDescriptor> buildScalarHelperDescriptor(const SemanticFieldType&   type,
                                                                  const ScalarHelperSymbols& symbols);

/// @brief Builds delimiter validation descriptor for a semantic field.
/// @param[in] field Semantic field.
/// @param[in] symbol Delimiter validation helper symbol.
/// @return Descriptor when delimiter validation applies.
std::optional<DelimiterValidateHelperDescriptor> buildDelimiterValidateHelperDescriptor(const SemanticField& field,
                                                                                        const std::string&   symbol);

/// @brief Builds delimiter validation descriptor for a semantic field type.
/// @param[in] type Semantic field type.
/// @param[in] symbol Delimiter validation helper symbol.
/// @return Descriptor when delimiter validation applies.
std::optional<DelimiterValidateHelperDescriptor> buildDelimiterValidateHelperDescriptor(const SemanticFieldType& type,
                                                                                        const std::string& symbol);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SERDES_HELPER_DESCRIPTORS_H
