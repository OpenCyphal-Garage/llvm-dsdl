//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Definition discovery declarations for locating and loading DSDL source files.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_FRONTEND_DISCOVERY_H
#define LLVMDSDL_FRONTEND_DISCOVERY_H

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Support/NamingPolicy.h"

#include "llvm/ADT/ArrayRef.h"

#include <string>
#include <vector>

namespace llvmdsdl
{

class DiagnosticEngine;

/// @file
/// @brief Discovery routines for locating and loading DSDL definitions.

/// @brief One target language the current invocation will emit source for.
///
/// The name is carried alongside the enumerator so a diagnostic can say which backend collided in
/// the spelling the user typed on the command line.
struct OutputLanguage
{
    /// @brief Naming policy to project names with.
    CodegenNamingLanguage language;

    /// @brief The `--target-language` spelling, for diagnostics.
    llvm::StringRef name;
};

/// @brief Discovers and loads definitions reachable from namespace roots.
///
/// @param[in] rootNamespaceDirs Root namespace directories.
/// @param[in] lookupDirs Additional lookup directories.
/// @param[in,out] diagnostics Diagnostic sink for discovery/I/O issues.
/// @param[in] outputLanguages Languages this invocation will emit source for. Two distinct types
///            whose names project onto one output file or one type name in any of these are
///            rejected. Empty means nothing is being emitted -- an AST or MLIR dump, or the language
///            server -- and no output-name check runs, because there is no output to collide.
/// @return Discovered definitions with metadata and source text.
std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics,
                                                      llvm::ArrayRef<OutputLanguage>  outputLanguages = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DISCOVERY_H
