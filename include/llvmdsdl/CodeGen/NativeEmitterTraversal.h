//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared lowered-step traversal helpers for native emitters.
///
/// This utility centralises the common orchestration pattern used by C++,
/// Rust, and Go emitters: dispatch lowered union steps and apply field or
/// padding alignment callbacks before value-emission callbacks.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_NATIVE_EMITTER_TRAVERSAL_H
#define LLVMDSDL_CODEGEN_NATIVE_EMITTER_TRAVERSAL_H

#include <cstdint>
#include <functional>

#include "llvmdsdl/CodeGen/LoweredRenderIR.h"

namespace llvmdsdl
{

/// @brief Callback bundle for native emitter lowered-step traversal.
struct NativeEmitterTraversalCallbacks final
{
    /// @brief Called for union-dispatch steps.
    std::function<void(const std::vector<PlannedFieldStep>&)> onUnionDispatch;

    /// @brief Called before each non-padding field step with field alignment.
    std::function<void(std::int64_t)> onFieldAlignment;

    /// @brief Called for each non-padding field step after alignment callback.
    std::function<void(const PlannedFieldStep&)> onField;

    /// @brief Called before each padding step with field alignment.
    std::function<void(std::int64_t)> onPaddingAlignment;

    /// @brief Called for each padding step after alignment callback.
    std::function<void(const PlannedFieldStep&)> onPadding;
};

/// @brief Traverses lowered render IR for native emitters with alignment hooks.
/// @param[in] renderIR Lowered render body.
/// @param[in] callbacks Native traversal callbacks.
void forEachNativeEmitterRenderStep(const LoweredBodyRenderIR&             renderIR,
                                    const NativeEmitterTraversalCallbacks& callbacks);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NATIVE_EMITTER_TRAVERSAL_H
