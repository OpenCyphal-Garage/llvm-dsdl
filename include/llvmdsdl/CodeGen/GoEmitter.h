//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for Go backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_GO_EMITTER_H
#define LLVMDSDL_CODEGEN_GO_EMITTER_H

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
/// @brief Go backend emission entry points.

/// @brief Configuration options for Go code generation.
struct GoEmitOptions final
{
    /// @brief Output directory root.
    std::string outDir;

    /// @brief Generated Go module name.
    std::string moduleName{"llvmdsdl_generated"};

    /// @brief Emits `go.mod` when true.
    bool emitGoMod{true};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Criteria selecting when support code is generated.
    SupportGeneration supportGeneration{SupportGeneration::AsNeeded};

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits Go artifacts from semantic and lowered MLIR inputs.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emitGo(const SemanticModule& semantic,
                   mlir::ModuleOp        module,
                   const GoEmitOptions&  options,
                   DiagnosticEngine&     diagnostics,
                   EmitTraceSink*        traceSink = nullptr);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_GO_EMITTER_H
