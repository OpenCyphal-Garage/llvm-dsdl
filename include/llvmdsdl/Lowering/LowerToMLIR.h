//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Entry points for lowering the semantic model into DSDL MLIR.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LOWERING_LOWERTO_MLIR_H
#define LLVMDSDL_LOWERING_LOWERTO_MLIR_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

namespace mlir
{
class MLIRContext;
}

namespace llvmdsdl
{
class DiagnosticEngine;
struct SemanticModule;

/// @file
/// @brief Semantic-to-MLIR lowering entry points.

/// @brief Lowers semantic definitions into the DSDL MLIR dialect.
/// @param[in] module Semantic module to lower.
/// @param[in,out] context MLIR context owning produced operations.
/// @param[in,out] diagnostics Diagnostic sink for lowering failures.
/// @param[in] verifyModule When true (the default, used by the codegen path), the lowered
///            module is verified via `mlir::verify` and a verification failure is a hard
///            error (null return) — so no backend ever consumes malformed IR. The LSP
///            passes false: it lowers error-tolerant / partially-resolved semantic models
///            to render best-effort *introspection* snapshots, where a partial module is an
///            acceptable debug view, not a codegen contract.
/// @return Owning MLIR module reference (null on error, or on verification failure when
///         @p verifyModule is true).
mlir::OwningOpRef<mlir::ModuleOp> lowerToMLIR(const SemanticModule& module,
                                              mlir::MLIRContext&    context,
                                              DiagnosticEngine&     diagnostics,
                                              bool                  verifyModule = true);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_LOWERING_LOWERTO_MLIR_H
