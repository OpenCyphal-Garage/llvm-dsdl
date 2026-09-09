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
#ifndef LLVMDSDL_CODEGEN_EMITTER_TS_H
#define LLVMDSDL_CODEGEN_EMITTER_TS_H

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
}  // namespace llvmdsdl

namespace llvmdsdl::emitter::ts
{

/// @file
/// @brief TypeScript backend emission entry points.

/// @brief TypeScript runtime specialisation selection.
enum class RuntimeSpecialization
{
    /// @brief Emit conservative portable runtime helpers.
    Portable,

    /// @brief Emit runtime helpers with byte-aligned fast paths.
    Fast,
};

/// @brief Configuration options for TypeScript code generation.
struct Options final
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

    /// @brief Requested runtime helper specialisation.
    RuntimeSpecialization runtimeSpecialization{RuntimeSpecialization::Portable};

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
/// @param[in] traceSink Optional emit-order trace sink (for the emit-order verifier); null (default) disables tracing
/// at zero cost.
/// @return Success or detailed failure.
llvm::Error emit(const SemanticModule& semantic,
                 mlir::ModuleOp        module,
                 const Options&        options,
                 DiagnosticEngine&     diagnostics,
                 EmitTraceSink*        traceSink = nullptr);

}  // namespace llvmdsdl::emitter::ts

#endif  // LLVMDSDL_CODEGEN_EMITTER_TS_H
