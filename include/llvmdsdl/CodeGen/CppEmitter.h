//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for C++ backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_CPPEMITTER_H
#define LLVMDSDL_CODEGEN_CPPEMITTER_H

#include "llvmdsdl/CodeGen/EmitCommon.h"

#include <string>
#include <vector>

#include "llvm/Support/Error.h"

namespace mlir
{
class ModuleOp;
}  // namespace mlir

namespace llvmdsdl
{
class DiagnosticEngine;
class EmitTraceSink;
struct SemanticModule;

/// @file
/// @brief C++ backend emission entry points.

/// @brief C++ runtime profile selection.
enum class CppProfile
{

    /// @brief Emit `std` profile only.
    Std,

    /// @brief Emit `pmr` profile only.
    Pmr,

    /// @brief Emit both `std` and `pmr` profiles.
    Both,

    /// @brief Emit AUTOSAR-oriented C++14 profile.
    Autosar,
};

/// @brief Configuration options for C++ code generation.
struct CppEmitOptions final
{
    /// @brief Output directory root.
    std::string outDir;

    /// @brief Requested C++ profile.
    CppProfile profile{CppProfile::Both};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits C++ artifacts from semantic and lowered MLIR inputs.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emitCpp(const SemanticModule& semantic,
                    mlir::ModuleOp        module,
                    const CppEmitOptions& options,
                    DiagnosticEngine&     diagnostics,
                    EmitTraceSink*        traceSink = nullptr);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_CPPEMITTER_H
