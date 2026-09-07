//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared native-emitter function skeleton orchestration declarations.
///
/// This utility centralises common C++/Rust/Go serialise and deserialise body
/// sequencing around lowered render-IR construction, helper-contract
/// validation, traversal dispatch, and epilogue emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_NATIVE_FUNCTION_SKELETON_H
#define LLVMDSDL_CODEGEN_NATIVE_FUNCTION_SKELETON_H

#include <functional>
#include <string>

#include "llvmdsdl/CodeGen/NativeEmitterTraversal.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"

namespace llvmdsdl
{
struct LoweredSectionFacts;
struct SemanticSection;

/// @brief Callback bundle for shared native function-skeleton orchestration.
struct NativeFunctionSkeletonCallbacks final
{
    /// @brief Emits language-specific helper binding declarations.
    std::function<void(const SectionHelperBindingPlan&)> emitHelperBindings;

    /// @brief Emits language-specific missing-helper fallback.
    std::function<void(const std::string&)> emitMissingHelperContract;

    /// @brief Emits language-specific preflight checks before traversal.
    std::function<void(const SectionHelperBindingPlan&)> emitContractPreflight;

    /// @brief Builds traversal callbacks bound to language-specific value emitters.
    std::function<NativeEmitterTraversalCallbacks(const LoweredBodyRenderIR&)> makeTraversalCallbacks;

    /// @brief Emits language-specific function epilogue.
    std::function<void()> emitEpilogue;
};

/// @brief Emits shared native function body skeleton steps.
/// @param[in] section Semantic section for helper requirements.
/// @param[in] sectionFacts Optional lowered section facts.
/// @param[in] direction Serialise or deserialise.
/// @param[in] callbacks Language-specific callback bundle.
/// @return `true` when helper contract is satisfied and traversal was emitted.
/// @return `false` when required helper contracts are missing.
bool emitNativeFunctionSkeleton(const SemanticSection&                 section,
                                const LoweredSectionFacts*             sectionFacts,
                                HelperBindingDirection                 direction,
                                const NativeFunctionSkeletonCallbacks& callbacks);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NATIVE_FUNCTION_SKELETON_H
