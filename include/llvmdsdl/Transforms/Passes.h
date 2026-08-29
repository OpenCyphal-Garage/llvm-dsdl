//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Declarations for DSDL MLIR pass factories and registration entry points.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TRANSFORMS_PASSES_H
#define LLVMDSDL_TRANSFORMS_PASSES_H

#include <memory>

namespace mlir
{
class Pass;
class OpPassManager;
}  // namespace mlir

namespace llvmdsdl
{

/// @file
/// @brief Registration and factory APIs for DSDL MLIR transform passes.

/// @brief Creates the pass that lowers serialization plans into the canonical
/// @details Lowered-serdes contract form.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createLowerDSDLSerializationPass();

/// @brief Creates the executable-contract lowering pass alias.
/// @details This pass is functionally equivalent to `createLowerDSDLSerializationPass`
///          but is exposed under the `lower-dsdl-exec` pipeline name.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createLowerDSDLExecPass();

/// @brief Creates the pass that annotates serialization plans with conservative
///        zero-overhead aliasability facts.
/// @details This is a conservative *annotator*, not a proof: it stamps
///          `zoh_alias_eligible`/`zoh_alias_reason` metadata on eligible plans and
///          does not alter the emitted serialization path. Registered under the
///          `dsdl-annotate-aliasability` pipeline name.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createDSDLAnnotateAliasabilityPass();

/// @brief Creates the pass that validates and stamps the module's
///        target-endianness metadata.
/// @details Validation-only: it checks the `llvmdsdl.target_endianness` attribute
///          and stamps `llvmdsdl.target_endianness_legalized`. It performs no byte
///          reordering. The DSDL wire format is always little-endian, so per-target
///          endianness handling lives entirely in the emitted code (the
///          `LLVMDSDL_TARGET_ENDIANNESS_BIG` conditional gates only the zero-copy
///          view helpers; `serialize_`/`deserialize_` are host-endianness-agnostic).
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createDSDLEndianLegalizePass();

/// @brief Creates the pass that converts lowered DSDL IR to EmitC-oriented IR.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createConvertDSDLToEmitCPass();

/// @brief Builds serialization plan bodies as dialect operations, before a target is chosen.
///
/// Runs after the C member names are stamped and before any backend conversion, so that both
/// the C path and object emission lower the same bodies rather than each producing its own.
std::unique_ptr<mlir::Pass> createBuildDSDLPlanBodiesPass();

/// @brief Lowers DSDL plan operations into the LLVM dialect, for emission as objects.
std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass();

/// @brief The same conversion, told what the target spells `size_t` at.
///
/// A module carrying its own data layout answers this for itself; a per-definition module built
/// by a backend does not, and the width has to come from the target the backend is emitting for.
/// @param[in] sizeBits Width of the target's `size_t`, in bits.
/// @return The pass.
std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass(unsigned sizeBits);

/// @brief Defines the serialisation primitives a lowered plan calls.
///
/// They are `static inline` in the runtime header, so an object has no symbol to link against.
/// @return The pass.
std::unique_ptr<mlir::Pass> createEmitDSDLRuntimePass();

/// @brief Registers the primitive-emitting pass with the global registry.
void registerEmitDSDLRuntimePass();

/// @brief Registers the LLVM lowering with the pass registry.
void registerDSDLToLLVMPasses();

/// @brief Adds optional lowered-serdes optimization passes to a pipeline.
/// @param[in,out] pm Pass manager receiving the optimization pipeline.
void addOptimizeLoweredSerDesPipeline(mlir::OpPassManager& pm);

/// @brief Registers conversion-oriented DSDL passes.
void registerDSDLConvertPasses();

/// @brief Registers all DSDL passes and pipelines.
void registerDSDLPasses();

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_PASSES_H
