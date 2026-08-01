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
#ifndef LLVMDSDL_CODEGEN_RUSTEMITTER_H
#define LLVMDSDL_CODEGEN_RUSTEMITTER_H

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
class EmitTraceSink;
struct SemanticModule;

/// @file
/// @brief Rust backend emission entry points.

/// @brief Rust crate profile selection.
enum class RustProfile
{

    /// @brief `std` profile.
    Std,

    /// @brief `no_std` plus `alloc` profile.
    NoStdAlloc,
};

/// @brief Runtime implementation specialization for generated Rust helpers.
enum class RustRuntimeSpecialization
{

    /// @brief Portable baseline implementation.
    Portable,

    /// @brief Faster specialized implementation.
    Fast,
};

/// @brief Memory strategy for variable-length data in generated Rust code.
enum class RustMemoryMode
{

    /// @brief Use fixed-capacity inline storage sized to DSDL maxima.
    MaxInline,

    /// @brief Inline below threshold and use per-type pools above threshold.
    InlineThenPool,
};

/// @brief Configuration options for Rust code generation.
struct RustEmitOptions final
{
    /// @brief Output directory root.
    std::string outDir;

    /// @brief Generated crate name.
    std::string crateName{"llvmdsdl_generated"};

    /// @brief Emits Cargo metadata when true.
    bool emitCargoToml{true};

    /// @brief Requested crate profile.
    RustProfile profile{RustProfile::Std};

    /// @brief Requested runtime specialization.
    RustRuntimeSpecialization runtimeSpecialization{RustRuntimeSpecialization::Portable};

    /// @brief Requested memory strategy for variable-length data.
    RustMemoryMode memoryMode{RustMemoryMode::MaxInline};

    /// @brief Inline storage threshold in bytes for pool mode.
    std::uint32_t inlineThresholdBytes{256U};

    /// @brief Enables optional lowered-serdes optimization before emission.
    bool optimizeLoweredSerDes{false};

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
/// @param[in] module Lowered MLIR module.
/// @param[in] options Backend configuration.
/// @param[in,out] diagnostics Diagnostic sink.
/// @return Success or detailed failure.
llvm::Error emitRust(const SemanticModule&  semantic,
                     mlir::ModuleOp         module,
                     const RustEmitOptions& options,
                     DiagnosticEngine&      diagnostics,
                     EmitTraceSink*         traceSink = nullptr);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_RUSTEMITTER_H
