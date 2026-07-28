//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for C backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_CEMITTER_H
#define LLVMDSDL_CODEGEN_CEMITTER_H

#include "llvmdsdl/CodeGen/EmitCommon.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/Error.h"

namespace llvmdsdl
{
class DiagnosticEngine;
struct SemanticModule;

/// @file
/// @brief C backend emission entry points.

/// @brief Configuration options for C code generation.
struct CEmitOptions final
{
    /// @brief Output directory root for generated files.
    std::string outDir;

    /// @brief Emits C89-style top-of-block variable declarations when true.
    bool declareVariablesAtTop{true};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

    /// @brief Emits a language-native deprecation attribute on `@deprecated` definitions.
    ///
    /// @details
    /// Off by default. The attribute turns a documentation signal into a compiler diagnostic, and
    /// the standard `uavcan` namespace contains two dozen deprecated definitions, so enabling it
    /// unconditionally would break `-Werror` builds on upgrade. The deprecation notice and the
    /// metadata constant are emitted regardless of this setting.
    bool emitDeprecationAttributes{false};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits C artifacts from semantic and lowered MLIR inputs.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emitC(const SemanticModule& semantic,
                  mlir::ModuleOp        module,
                  const CEmitOptions&   options,
                  DiagnosticEngine&     diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_CEMITTER_H
