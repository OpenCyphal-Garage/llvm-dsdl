//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared native-emitter helper-contract validation declarations.
///
/// This utility centralises required helper-presence checks for native
/// backends (C++/Rust/Go) so semantic helper requirements remain uniform.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_NATIVE_HELPER_CONTRACT_H
#define LLVMDSDL_CODEGEN_NATIVE_HELPER_CONTRACT_H

#include <string>

namespace llvmdsdl
{
enum class HelperBindingDirection;
struct LoweredSectionFacts;
struct SectionHelperBindingPlan;
struct SemanticSection;

/// @brief Validates required helper presence for native emitter section plans.
/// @details Native backends require lowered capacity helpers for every section,
/// union sections additionally require union-tag validate/mask helpers, and
/// field-level scalar/array/delimiter helper contracts must be satisfiable for
/// the requested serialisation direction.
/// @param[in] section Semantic section metadata.
/// @param[in] sectionFacts Lowered section facts used to resolve field helper symbols.
/// @param[in] direction Serialise/deserialize direction.
/// @param[in] helperBindings Section helper bindings for one direction.
/// @param[out] missingRequirement Optional missing-requirement label.
/// @return True when required helpers are present.
bool validateNativeSectionHelperContract(const SemanticSection&          section,
                                         const LoweredSectionFacts*      sectionFacts,
                                         HelperBindingDirection          direction,
                                         const SectionHelperBindingPlan& helperBindings,
                                         std::string*                    missingRequirement = nullptr);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NATIVE_HELPER_CONTRACT_H
