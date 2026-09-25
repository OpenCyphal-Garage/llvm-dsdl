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

/// @brief Creates the pass that lowers serialisation plans into the canonical
/// @details Lowered-serdes contract form.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createLowerDSDLSerializationPass();

/// @brief Creates the executable-contract lowering pass alias.
/// @details This pass is functionally equivalent to `createLowerDSDLSerializationPass`
///          but is exposed under the `lower-dsdl-exec` pipeline name.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createLowerDSDLExecPass();

/// @brief Creates the pass that checks a plan's layout verdicts against the steps it carries.
/// @details `wire_flat` and `host_image` are decided during semantic analysis, where the fields
///          still carry their source locations. This pass re-derives from the steps what they can
///          decide and reports a disagreement, so the lowered IR cannot drift from the schema it
///          came from. Registered under the `dsdl-verify-alias-layout` pipeline name.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createDSDLVerifyAliasLayoutPass();

/// @brief Builds serialisation plan bodies as dialect operations, before a target is chosen.
///
/// Runs after the C member names are stamped, so that every backend translates the same bodies
/// rather than each producing its own.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createBuildDSDLPlanBodiesPass();

/// @brief Creates the pass that folds a host-image section's bodies into one move.
///
/// `build-dsdl-plan-bodies` always emits the field-wise body, which is the contract every backend
/// translates. Where the target's objects are byte images of the wire -- which is what a language
/// whose structures have a layout can offer -- this replaces that body with `dsdl.image_read` or
/// `dsdl.image_write`. A body it does not recognise is left alone, so declining costs correctness
/// nothing.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createFoldDSDLHostImageBodiesPass();

/// @brief Drops every plan's serialise, deserialise and initialise body, keeping the field
///        accessors: what `--aliasable-only` emits. Registered as `dsdl-keep-accessors`.
/// @return Newly constructed pass instance.
std::unique_ptr<mlir::Pass> createKeepDSDLAccessorsPass();

/// @brief Lowers DSDL plan operations into the LLVM dialect, for emission as objects.
std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass();

/// @brief The same conversion, told what the target spells `size_t` at and how it orders bytes.
///
/// A module carrying its own data layout answers the width for itself; a per-definition module
/// built by a backend does not, and both facts have to come from the target the backend is
/// emitting for. On a little-endian target a byte-aligned scalar of a register's width is one
/// load or one store within the buffer, which is the wire's own encoding there.
/// @param[in] sizeBits Width of the target's `size_t`, in bits.
/// @param[in] littleEndian Whether the target orders bytes as the wire does.
/// @return The pass.
std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass(unsigned sizeBits, bool littleEndian);

/// @brief Defines the serialisation primitives a lowered plan calls.
///
/// They are `static inline` in the runtime header, so an object has no symbol to link against.
/// @return The pass.
std::unique_ptr<mlir::Pass> createEmitDSDLRuntimePass();

/// @brief Registers the primitive-emitting pass with the global registry.
void registerEmitDSDLRuntimePass();

/// @brief Registers the LLVM lowering with the pass registry.
void registerDSDLToLLVMPasses();

/// @brief Which of a plan body's pointer arguments a target can present as null.
///
/// A body opens by testing the three it is handed, and answers `-2` when any is null. A target
/// whose references cannot be null never reaches that answer, so the test is a constant and the
/// guard is a branch nothing takes. Each defaults to the answer C gives, which is that any of them
/// may be null, so a caller that says nothing keeps the guard.
struct TargetNullability final
{
    /// @brief Whether the object a body serialises can arrive null.
    ///
    /// True for C and C++, which are handed a pointer. False for Rust, which is handed a reference.
    /// Go, TypeScript and Python are handed an object that a caller may still omit, so it is true
    /// for them as well.
    bool objectPointer{true};

    /// @brief Whether the buffer and the slot holding its size can arrive null.
    ///
    /// True only where they are pointers. Rust has a slice and a local, Go a slice, TypeScript and
    /// Python a view and a local; none of those can be null.
    bool rawPointer{true};

    /// @brief Whether a field accessor's buffer can arrive null, where `rawPointer` says a body's can.
    ///
    /// True for C, whose accessors take a pointer. False for C++, whose accessors take a span while
    /// its serialise and deserialise still take a pointer.
    bool accessorBuffer{true};

    /// @brief Whether every argument is still nullable, so the guard has nothing to fold.
    [[nodiscard]] constexpr bool allNullable() const
    {
        return objectPointer && rawPointer && accessorBuffer;
    }
};

/// @brief Replaces a null test the target cannot fail with a constant, and folds what that kills.
///
/// Its own stage rather than an option on `build-dsdl-plan-bodies`, which produces one body per
/// plan regardless of target and is the pipeline section 4 of DESIGN.md names. The fold is
/// complete on its own: it canonicalises the bodies it changed rather than waiting for
/// `--optimize-lowered-serdes`, which is off unless a caller asks for it.
/// @param[in] nullability What the target can present as null.
/// @return The pass.
std::unique_ptr<mlir::Pass> createFoldDSDLNullGuardsPass(TargetNullability nullability);

/// @brief Erases the size a composite getter writes back, for a target whose getter returns a view.
///
/// A composite getter answers a pointer to the nested type's bytes and writes their length through
/// its last argument. A target that answers a view -- a span, a slice, a `memoryview`, a
/// `Uint8Array` -- hands that length to the caller inside the view, so the write is observed by
/// nothing and whatever computed it is dead. A write is erased only where the pointer is never read
/// back.
/// @return The pass.
std::unique_ptr<mlir::Pass> createFoldDSDLUnobservedAccessorSizesPass();

/// @brief Adds the target-independent lowering: `lower-dsdl-exec`, `dsdl-verify-alias-layout`
///        and `build-dsdl-plan-bodies`, after which every serialisation plan is a serialise and a
///        deserialise function of dialect operations. A backend is a translation of that output
///        (DESIGN.md, *Backend Contract*). Registered with `dsdl-opt` as `lower-dsdl-bodies`.
/// @param[in] pm Pass manager to extend.
/// @param[in] optimizeLoweredSerDes Canonicalises the helpers and bodies once they are built, so every backend
///                                  translates the simplified functions.
/// @param[in] targetObjectsAreByteImages Whether this target's objects can be byte images of the
///            wire. True for the native backends; false where a structure has no layout to speak
///            of, as in TypeScript and Python.
/// @param[in] accessorsOnly Whether to drop the bodies once they are built and keep the field
///                          accessors alone, which is what `--aliasable-only` emits.
/// @param[in] nullability What the target can present as null, which decides whether the entry
///                        guard survives.
/// @param[in] accessorsReturnViews Whether the target's composite getter returns a view carrying its
///            own length, so the size the getter writes back is observed by nothing. False, the
///            default, is what C needs: it passes the size back through a pointer.
void addLowerDSDLBodiesPipeline(mlir::OpPassManager& pm,
                                bool                 optimizeLoweredSerDes,
                                bool                 targetObjectsAreByteImages = false,
                                bool                 accessorsOnly              = false,
                                TargetNullability    nullability                = {},
                                bool                 accessorsReturnViews       = false);

/// @brief Adds the canonicaliser and common-subexpression elimination, nested on every function.
/// @param[in,out] pm Pass manager receiving the optimisation pipeline.
void addOptimizeLoweredSerDesPipeline(mlir::OpPassManager& pm);

/// @brief Registers the plan-body builder with the pass registry.
void registerBuildDSDLPlanBodiesPass();

/// @brief Registers all DSDL passes and pipelines.
void registerDSDLPasses();

}  // namespace llvmdsdl

#endif  // LLVMDSDL_TRANSFORMS_PASSES_H
