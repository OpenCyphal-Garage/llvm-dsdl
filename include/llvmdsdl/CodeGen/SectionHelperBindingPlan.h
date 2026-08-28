//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Helper-binding planning declarations shared by language backends.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SECTION_HELPER_BINDING_PLAN_H
#define LLVMDSDL_CODEGEN_SECTION_HELPER_BINDING_PLAN_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace llvmdsdl
{
struct LoweredFieldFacts;
struct LoweredSectionFacts;
struct SemanticSection;

/// @file
/// @brief Helper binding plans for section serialize/deserialize bodies.

/// @brief Direction for helper binding resolution.
enum class HelperBindingDirection
{

    /// @brief Serialize helper bindings.
    Serialize,

    /// @brief Deserialize helper bindings.
    Deserialize,
};

/// @brief Descriptor for union-tag mask helper binding.
struct UnionTagMaskBindingDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Union tag width in bits.
    std::uint32_t bits{0};
};

/// @brief Descriptor for scalar helper binding.
struct ScalarBindingDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Scalar helper descriptor.
    ScalarHelperDescriptor descriptor;
};

/// @brief Descriptor for array-prefix helper binding.
struct ArrayPrefixBindingDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Prefix width in bits.
    std::uint32_t bits{0};
};

/// @brief Descriptor for array-length validation helper binding.
struct ArrayValidateBindingDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;

    /// @brief Maximum allowed element count.
    std::int64_t capacity{0};
};

/// @brief Descriptor for delimiter validation helper binding.
struct DelimiterValidateBindingDescriptor final
{
    /// @brief Helper symbol name.
    std::string symbol;
};

/// @brief Complete helper-binding plan for one section and direction.
struct SectionHelperBindingPlan final
{
    /// @brief Optional capacity-check helper binding.
    std::optional<CapacityCheckHelperDescriptor> capacityCheck;

    /// @brief Optional union-tag validation helper binding.
    std::optional<UnionTagValidateHelperDescriptor> unionTagValidate;

    /// @brief Optional union-tag mask helper binding.
    std::optional<UnionTagMaskBindingDescriptor> unionTagMask;

    /// @brief Scalar helper bindings.
    std::vector<ScalarBindingDescriptor> scalarBindings;

    /// @brief Array-prefix helper bindings.
    std::vector<ArrayPrefixBindingDescriptor> arrayPrefixBindings;

    /// @brief Array-validation helper bindings.
    std::vector<ArrayValidateBindingDescriptor> arrayValidateBindings;

    /// @brief Delimiter-validation helper bindings.
    std::vector<DelimiterValidateBindingDescriptor> delimiterValidateBindings;
};

/// @brief Builds helper-binding plan for a section and direction.
/// @param[in] section Semantic section.
/// @param[in] sectionFacts Lowered section metadata.
/// @param[in] direction Serialize/deserialize direction.
/// @return Section helper-binding plan.
SectionHelperBindingPlan buildSectionHelperBindingPlan(const SemanticSection&     section,
                                                       const LoweredSectionFacts* sectionFacts,
                                                       HelperBindingDirection     direction);

/// @brief Returns the array-length prefix helper symbol for one direction.
/// @param[in] direction Serialize/deserialize direction.
/// @param[in] fieldFacts Lowered field metadata, or `nullptr` when the field has none.
/// @return Helper symbol, empty when there are no facts for the field.
[[nodiscard]] std::string arrayLengthPrefixHelper(HelperBindingDirection   direction,
                                                  const LoweredFieldFacts* fieldFacts);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SECTION_HELPER_BINDING_PLAN_H
