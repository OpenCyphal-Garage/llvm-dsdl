//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for Python backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_PYTHON_H
#define LLVMDSDL_CODEGEN_EMITTER_PYTHON_H

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
struct SemanticModule;
class EmitTraceSink;
}  // namespace llvmdsdl

namespace llvmdsdl::emitter::python
{

/// @file
/// @brief Python backend emission entry points.

/// @brief Runtime specialisation profile for generated Python runtime helpers.
enum class RuntimeSpecialization
{
    Portable,  ///< Conservative bit-level runtime helper implementation.
    Fast       ///< Enables byte-aligned runtime helper fast paths.
};

/// @brief Configuration options for Python code generation.
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

    /// @brief Generated Python package name.
    std::string packageName{"dsdl_gen"};

    /// @brief Runtime helper specialisation profile.
    RuntimeSpecialization runtimeSpecialization{RuntimeSpecialization::Portable};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Criteria selecting when support code is generated.
    SupportGeneration supportGeneration{SupportGeneration::AsNeeded};

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits Python artifacts from semantic and lowered MLIR inputs.
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

}  // namespace llvmdsdl::emitter::python

#endif  // LLVMDSDL_CODEGEN_EMITTER_PYTHON_H
