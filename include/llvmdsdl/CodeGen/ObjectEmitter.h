//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Object-code emission entry points for generated DSDL artifacts.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_OBJECT_EMITTER_H
#define LLVMDSDL_CODEGEN_OBJECT_EMITTER_H

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/CodeGen/EmitCommon.h"

#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mlir
{
class ModuleOp;
}  // namespace mlir

namespace llvmdsdl
{
class DiagnosticEngine;
struct SemanticModule;

/// @brief Source language lane used for object emission.
enum class ObjectAbiLanguage
{
    /// @brief Emit object code from generated C translation units.
    C,

    /// @brief Emit object code from canonical C++ ABI wrappers + C shims.
    Cpp,
};

/// @brief Options controlling object-code emission.
struct ObjectEmitOptions final
{
    /// @brief Whether generated type names carry the definition's version.
    ///
    /// Unversioned by default: most code speaks one version of a type and reads better without the
    /// suffix. Set when the consuming code handles two versions of one type side by side and needs
    /// them to be distinct identifiers in its own source.
    TypeNameVersioning typeNameVersioning{TypeNameVersioning::Unversioned};
    /// @brief Output directory for produced object/archive artifacts.
    std::string outDir;

    /// @brief Target endianness (`little` or `big`).
    std::string targetEndianness;

    /// @brief Optional explicit target triple for compiler invocations.
    std::string targetTriple;

    /// @brief Archive file stem used when archive output is enabled.
    std::string archiveName{"llvmdsdl_generated"};

    /// @brief Emits only `.o` artifacts when true.
    bool noArchive{false};

    /// @brief Source language lane used by object emission.
    ObjectAbiLanguage abiLanguage{ObjectAbiLanguage::C};

    /// @brief Number of concurrent compile jobs (`0` means auto).
    std::uint32_t compileJobs{0U};

    /// @brief Enables optional lowered-serdes optimization pipeline.
    bool optimizeLoweredSerDes{false};

    /// @brief Optional selected type keys for filtered emission.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Shared write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits compiled object artifacts from semantic + MLIR inputs.
/// @param[in] semantic Semantic module closure.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Object emission options.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or a descriptive emission/build error.
llvm::Error emitObject(const SemanticModule&    semantic,
                       mlir::ModuleOp           module,
                       const ObjectEmitOptions& options,
                       DiagnosticEngine&        diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_OBJECT_EMITTER_H
