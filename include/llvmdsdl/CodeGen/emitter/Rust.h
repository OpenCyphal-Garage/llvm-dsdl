//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public entry points and options for Rust backend emission.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITTER_RUST_H
#define LLVMDSDL_CODEGEN_EMITTER_RUST_H

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/CodeGen/EmitCommon.h"

#include <cstdint>
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
}  // namespace llvmdsdl

namespace llvmdsdl::emitter::rust
{

/// @file
/// @brief Rust backend emission entry points.

/// @brief Rust crate profile selection.
enum class Profile
{

    /// @brief `std` profile.
    Std,

    /// @brief `no_std` plus `alloc` profile.
    NoStdAlloc,
};

/// @brief Runtime implementation specialisation for generated Rust helpers.
enum class RuntimeSpecialization
{

    /// @brief Portable baseline implementation.
    Portable,

    /// @brief Faster specialised implementation.
    Fast,
};

/// @brief Memory strategy for variable-length data in generated Rust code.
enum class MemoryMode
{

    /// @brief Use fixed-capacity inline storage sized to DSDL maxima.
    MaxInline,

    /// @brief Inline below threshold and use per-type pools above threshold.
    InlineThenPool,
};

/// @brief Configuration options for Rust code generation.
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

    /// @brief Generated crate name.
    std::string crateName{"llvmdsdl_generated"};

    /// @brief Emits Cargo metadata when true.
    bool emitCargoToml{true};

    /// @brief Requested crate profile.
    Profile profile{Profile::Std};

    /// @brief Requested runtime specialisation.
    RuntimeSpecialization runtimeSpecialization{RuntimeSpecialization::Portable};

    /// @brief Requested memory strategy for variable-length data.
    MemoryMode memoryMode{MemoryMode::MaxInline};

    /// @brief Inline storage threshold in bytes for pool mode.
    std::uint32_t inlineThresholdBytes{256U};

    /// @brief Emits a language-native deprecation attribute on `@deprecated` definitions.
    ///
    /// @details
    /// On by default: a deprecation that only a reader of the documentation can see is a deprecation
    /// nobody acts on, so the attribute is what gives the marking teeth. Only code that names a
    /// deprecated type is diagnosed -- each generated file suppresses the diagnostic across its own
    /// body, so compiling the generated crate stays clean under `-D warnings`. Disable this when a
    /// deny-warnings build must keep using deprecated definitions that have no migration target yet.
    /// The deprecation notice and the metadata constant are emitted regardless of this setting.
    bool emitDeprecationAttributes{true};

    /// @brief Optional list of selected type keys to emit.
    std::vector<std::string> selectedTypeKeys;

    /// @brief Criteria selecting when support code is generated.
    SupportGeneration supportGeneration{SupportGeneration::AsNeeded};

    /// @brief Output write policy.
    EmitWritePolicy writePolicy;
};

/// @brief Emits Rust artifacts from semantic and lowered MLIR inputs.
/// @param[in] semantic Resolved semantic module.
/// @param[in] module The module after `lower-dsdl-bodies`.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emit(const SemanticModule& semantic,
                 mlir::ModuleOp        module,
                 const Options&        options,
                 DiagnosticEngine&     diagnostics);

}  // namespace llvmdsdl::emitter::rust

#endif  // LLVMDSDL_CODEGEN_EMITTER_RUST_H
