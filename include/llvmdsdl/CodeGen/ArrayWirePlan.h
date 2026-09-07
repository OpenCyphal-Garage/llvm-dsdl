//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Array wire-plan declarations used to emit array serialisation and deserialisation paths.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_ARRAY_WIRE_PLAN_H
#define LLVMDSDL_CODEGEN_ARRAY_WIRE_PLAN_H

#include <cstdint>
#include <optional>

#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

namespace llvmdsdl
{
enum class HelperBindingDirection;
struct LoweredFieldFacts;
struct SemanticFieldType;

/// @file
/// @brief Array wire-layout helper planning utilities.

/// @brief Resolved wire plan for array serialisation/deserialization.
struct ArrayWirePlan final
{
    /// @brief True when the array is variable-length.
    bool variable{false};

    /// @brief Prefix width for variable arrays.
    std::uint32_t prefixBits{0};

    /// @brief Optional helper descriptor bundle.
    std::optional<ArrayLengthHelperDescriptor> descriptor;
};

/// @brief Builds array wire plan for a semantic field type.
/// @param[in] type Resolved field type.
/// @param[in] fieldFacts Lowered field facts.
/// @param[in] prefixBitsOverride Optional prefix width override.
/// @param[in] direction Serialise/deserialize direction.
/// @return Array wire plan.
ArrayWirePlan buildArrayWirePlan(const SemanticFieldType&     type,
                                 const LoweredFieldFacts*     fieldFacts,
                                 std::optional<std::uint32_t> prefixBitsOverride,
                                 HelperBindingDirection       direction);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_ARRAY_WIRE_PLAN_H
