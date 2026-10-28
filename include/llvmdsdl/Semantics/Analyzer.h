//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Public semantic analysis entry points converting AST modules into semantic modules.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SEMANTICS_ANALYZER_H
#define LLVMDSDL_SEMANTICS_ANALYZER_H

#include "llvmdsdl/Semantics/Model.h"
#include "llvm/Support/Error.h"

namespace llvmdsdl
{
class DiagnosticEngine;
struct ASTModule;

/// @file
/// @brief Semantic analysis entry points.

/// @brief Options that control semantic-analysis policy checks.
struct AnalyzeOptions final
{
    /// @brief Allow fixed port IDs outside regulated ranges.
    bool allowUnregulatedFixedPortId{false};

    /// @brief Optional external semantic catalogue consulted for composite resolution.
    ///
    /// Local definitions remain authoritative when keys collide.
    const SemanticModule* externalSemanticCatalog{nullptr};
};

/// @brief Converts parsed AST into the resolved semantic model.
/// @param[in] module Parsed AST module.
/// @param[in,out] diagnostics Diagnostic sink for semantic issues.
/// @return Resolved semantic module on success.
llvm::Expected<SemanticModule> analyze(const ASTModule& module, DiagnosticEngine& diagnostics);

/// @brief Converts parsed AST into the resolved semantic model with options.
/// @param[in] module Parsed AST module.
/// @param[in,out] diagnostics Diagnostic sink for semantic issues.
/// @param[in] options Semantic policy options.
/// @return Resolved semantic module on success.
llvm::Expected<SemanticModule> analyze(const ASTModule&      module,
                                       DiagnosticEngine&     diagnostics,
                                       const AnalyzeOptions& options);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_ANALYZER_H
