//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for TypeScript backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_TS_EMITTER_H
#define LLVMDSDL_CODEGEN_TS_EMITTER_H

#include "llvmdsdl/Support/DefinitionNaming.h"
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
/// @brief TypeScript backend emission entry points.

/// @brief TypeScript runtime specialization selection.
enum class TsRuntimeSpecialization
{
    /// @brief Emit conservative portable runtime helpers.
    Portable,

    /// @brief Emit runtime helpers with byte-aligned fast paths.
    Fast,
};

/// @brief Configuration options for TypeScript code generation.
struct TsEmitOptions final
{
    /// @brief Whether generated type names carry the definition's version.
    ///
    /// Unversioned by default: most code speaks one version of a type and reads better without the
    /// suffix. Set when the consuming code handles two versions of one type side by side and needs
    /// them to be distinct identifiers in its own source.
    TypeNameVersioning typeNameVersioning{TypeNameVersioning::Unversioned};
    /// @brief Output directory root.
    std::string outDir;

    /// @brief Generated npm/module name.
    std::string moduleName{"llvmdsdl_generated"};

    /// @brief Emits package metadata when true.
    bool emitPackageJson{true};

    /// @brief Requested runtime helper specialization.
    TsRuntimeSpecialization runtimeSpecialization{TsRuntimeSpecialization::Portable};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Criteria selecting when support code is generated.
    SupportGeneration supportGeneration{SupportGeneration::AsNeeded};

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits TypeScript artifacts from semantic and lowered MLIR inputs.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @param[in] traceSink Optional emit-order trace sink (for the emit-order verifier); null (default) disables tracing at zero cost.
/// @return Success or detailed failure.
llvm::Error emitTs(const SemanticModule& semantic,
                   mlir::ModuleOp        module,
                   const TsEmitOptions&  options,
                   DiagnosticEngine&     diagnostics,
                   EmitTraceSink*        traceSink = nullptr);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_TS_EMITTER_H
