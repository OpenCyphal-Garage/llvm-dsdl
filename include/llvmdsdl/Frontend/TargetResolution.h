//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Target path resolution for nnvg-style CLI inputs.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_FRONTEND_TARGETRESOLUTION_H
#define LLVMDSDL_FRONTEND_TARGETRESOLUTION_H

#include "llvm/Support/Error.h"

#include <string>
#include <vector>

namespace llvmdsdl
{

class DiagnosticEngine;

/// @brief CLI options that affect target path interpretation.
struct TargetResolveOptions final
{
    /// @brief Positional folder targets are rejected when true.
    bool noTargetNamespaces{false};

    /// @brief Repeatable lookup directories from CLI.
    std::vector<std::string> lookupDirs;
};

/// @brief Result of resolving positional targets and lookup sources.
struct ResolvedTargets final
{
    /// @brief Root namespace directories used as primary roots.
    std::vector<std::string> rootNamespaceDirs;

    /// @brief Additional lookup directories (CLI + environment).
    std::vector<std::string> lookupDirs;

    /// @brief Explicitly targeted DSDL files (absolute normalized paths).
    std::vector<std::string> explicitTargetFiles;

    /// @brief The subset of @ref explicitTargetFiles the user named one at a time.
    ///
    /// A folder target expands to every `.dsdl` beneath it, and those land in
    /// @ref explicitTargetFiles alongside a file the user actually typed -- the two are
    /// indistinguishable there, which is right for deciding what to emit and wrong for deciding what
    /// may be dropped. A default that generates only the newest version of each type must leave a
    /// version the user asked for by name alone, and this is how it tells them apart.
    ///
    /// Populated by the direct-file and colon-syntax paths, never by folder expansion.
    std::vector<std::string> namedTargetFiles;
};

/// @brief Resolves positional target paths under nnvg-like semantics.
///
/// @details
/// Supports:
/// - file/folder positional targets
/// - colon syntax (`root:path/to/Type.1.0.dsdl`)
/// - lookup roots from `--lookup-dir`, `DSDL_INCLUDE_PATH`, and `CYPHAL_PATH`
/// - folder expansion into explicit `.dsdl` targets unless disabled
///
/// @param[in] targetFilesOrRootNamespace Positional target tokens.
/// @param[in] options Resolver options.
/// @param[in,out] diagnostics Diagnostic sink for resolution errors.
/// @return Resolved targets or an error.
llvm::Expected<ResolvedTargets> resolveTargets(const std::vector<std::string>& targetFilesOrRootNamespace,
                                               const TargetResolveOptions&     options,
                                               DiagnosticEngine&               diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_TARGETRESOLUTION_H
