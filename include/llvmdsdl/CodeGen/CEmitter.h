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

#include "llvmdsdl/Support/DefinitionNaming.h"
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

/// @brief What the C backend delivers.
///
/// The API is the same either way -- the same headers, declaring the same symbols. What differs
/// is whether the definitions arrive as C to be compiled or as objects already compiled.
enum class CEmitArtifact : std::uint8_t
{
    Source,
    Object,
};

/// @brief Configuration options for C code generation.
struct CEmitOptions final
{
    /// @brief Whether the definitions are delivered as sources or as objects.
    CEmitArtifact artifact{CEmitArtifact::Source};

    /// @brief The target an object is emitted for; the host's own when empty.
    std::string targetTriple;

    /// @brief Whether generated type names carry the definition's version.
    ///
    /// Unversioned by default: most code speaks one version of a type and reads better without the
    /// suffix. Set when the consuming code handles two versions of one type side by side and needs
    /// them to be distinct identifiers in its own source.
    TypeNameVersioning typeNameVersioning{TypeNameVersioning::Unversioned};
    /// @brief Output directory root for generated files.
    std::string outDir;

    /// @brief Emits C89-style top-of-block variable declarations when true.
    bool declareVariablesAtTop{false};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

    /// @brief Emits a language-native deprecation attribute on `@deprecated` definitions.
    ///
    /// @details
    /// On by default: a deprecation that only a reader of the documentation can see is a deprecation
    /// nobody acts on, so the attribute is what gives the marking teeth. Only code that names a
    /// deprecated type is diagnosed -- each generated file suppresses the diagnostic across its own
    /// body, so including generated headers stays clean under `-Werror`. Disable this when a
    /// `-Werror` build must keep using deprecated definitions that have no migration target yet. The
    /// deprecation notice and the metadata constant are emitted regardless of this setting.
    bool emitDeprecationAttributes{true};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Criteria selecting when support code is generated.
    SupportGeneration supportGeneration{SupportGeneration::AsNeeded};

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

/// @brief What @p triple spells `size_t` at, in bits.
///
/// A variable-length array holds its count in one, so the struct an object addresses a member
/// within depends on it. Answered from the target's own data layout.
/// @param[in] triple The target, or empty for the host's own.
/// @return The width in bits, or an error naming the target when no backend knows it.
[[nodiscard]] llvm::Expected<unsigned> targetSizeBits(const std::string& triple);

/// @brief Emits the C API's headers beside objects holding its definitions.
///
/// The same entry points a C consumer calls, with the serialisation compiled rather than handed
/// over as source. No C is written and no compiler is invoked.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration; the artifact is set here.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emitObject(const SemanticModule& semantic,
                       mlir::ModuleOp        module,
                       CEmitOptions          options,
                       DiagnosticEngine&     diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_CEMITTER_H
