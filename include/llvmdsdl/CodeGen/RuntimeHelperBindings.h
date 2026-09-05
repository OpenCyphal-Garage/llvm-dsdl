//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared helper-binding lookup utilities for scripted runtime emitters.
///
/// These APIs centralise lowered helper symbol resolution and per-field helper
/// binding names for TypeScript/Python emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_RUNTIME_HELPER_BINDINGS_H
#define LLVMDSDL_CODEGEN_RUNTIME_HELPER_BINDINGS_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;
struct SemanticSection;

/// @brief Name bundle for helper bindings used by one planned runtime field.
struct RuntimeFieldHelperNames final
{
    /// @brief Serialise scalar helper name.
    std::string serScalar;

    /// @brief Deserialise scalar helper name.
    std::string deserScalar;

    /// @brief Serialise array-prefix helper name.
    std::string serArrayPrefix;

    /// @brief Deserialise array-prefix helper name.
    std::string deserArrayPrefix;

    /// @brief Array-length validation helper name.
    std::string arrayValidate;

    /// @brief Delimiter-header validation helper name.
    std::string delimiterValidate;
};

/// @brief Name bundle for helper bindings shared across one runtime section.
struct RuntimeSectionHelperNames final
{
    /// @brief Capacity-check helper name.
    std::string capacityCheck;

    /// @brief Union-tag validation helper name.
    std::string unionTagValidate;

    /// @brief Serialise union-tag mask helper name.
    std::string serUnionTagMask;

    /// @brief Deserialise union-tag mask helper name.
    std::string deserUnionTagMask;
};

/// @brief Name-mangling callback from lowered symbol to emitted helper name.
using RuntimeHelperNameResolver = std::function<std::string(const std::string&)>;

/// @brief Resolves array-prefix width override from runtime field metadata.
/// @param[in] field Runtime field plan entry.
/// @return Prefix width override when applicable.
std::optional<std::uint32_t> runtimeArrayPrefixOverride(const RuntimeFieldPlan& field);

/// @brief Resolves section helper names from lowered section facts.
/// @param[in] sectionFacts Lowered section facts.
/// @param[in] helperNameResolver Callback to map lowered symbols to emitted names.
/// @return Section-level helper names.
RuntimeSectionHelperNames resolveRuntimeSectionHelperNames(const LoweredSectionFacts*       sectionFacts,
                                                           const RuntimeHelperNameResolver& helperNameResolver);

/// @brief Resolves per-field helper names from lowered facts and semantic type metadata.
/// @param[in] section Semantic section owning the field.
/// @param[in] sectionFacts Lowered section facts.
/// @param[in] field Runtime field plan entry.
/// @param[in] prefixBitsOverride Optional variable-array prefix width override.
/// @param[in] helperNameResolver Callback to map lowered symbols to emitted names.
/// @return Field-level helper names.
RuntimeFieldHelperNames resolveRuntimeFieldHelperNames(const SemanticSection&           section,
                                                       const LoweredSectionFacts*       sectionFacts,
                                                       const RuntimeFieldPlan&          field,
                                                       std::optional<std::uint32_t>     prefixBitsOverride,
                                                       const RuntimeHelperNameResolver& helperNameResolver);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_RUNTIME_HELPER_BINDINGS_H
