//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds array wire-layout planning facts for lowered sections.
///
/// The implementation combines semantic array properties with lowered helper contracts so emitters can share consistent
/// serialization behaviour.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ArrayWirePlan.h"

#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/Semantics/Model.h"
#include <cstdint>
#include <optional>

namespace llvmdsdl
{
enum class HelperBindingDirection;
struct LoweredFieldFacts;

ArrayWirePlan buildArrayWirePlan(const SemanticFieldType&           type,
                                 const LoweredFieldFacts*           fieldFacts,
                                 const std::optional<std::uint32_t> prefixBitsOverride,
                                 const HelperBindingDirection       direction)
{
    ArrayWirePlan out;
    out.variable   = isVariableArray(type.arrayKind);
    out.prefixBits = prefixBitsOverride.value_or(static_cast<std::uint32_t>(type.arrayLengthPrefixBits));
    out.descriptor = resolveArrayLengthHelperDescriptor(type, fieldFacts, out.prefixBits, direction);
    return out;
}

}  // namespace llvmdsdl
