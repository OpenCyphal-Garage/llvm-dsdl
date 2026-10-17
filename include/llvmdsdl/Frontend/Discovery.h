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
#include <string>
#include <vector>

namespace llvmdsdl
{

class DiagnosticEngine;

/// @file
/// @brief Discovery routines for locating and loading DSDL definitions.

/// @brief Discovers and loads every DSDL definition reachable from the given roots.
///
/// Walks each root and each lookup directory, parses the file names into an identity -- namespace
/// components, short name, version, and optional fixed port ID -- and reads the source text. Nothing
/// here lexes or parses the *contents*; that is @ref parseDefinitions, which calls this first.
///
/// Both parameters are walked and both contribute to the result; a type can be resolved from a
/// lookup root without being generated. What separates them here is only that an empty file is kept
/// from a root and dropped from a lookup directory -- an empty definition someone put in a namespace
/// they are compiling is theirs to be told about, one in a dependency tree is noise. Which
/// definitions are *targets* is not decided here: the driver sets that afterwards, from the resolved
/// target file list.
///
/// The result is sorted by full name, then by descending version, then by path. The sort is what
/// keeps a directory walk's order -- which the standard does not define -- from reaching generated
/// output; everything downstream consumes this vector in order. See
/// `docs/reference/guarantees/determinism.md`.
///
/// ### What it rejects
///
/// Discovery is also where a corpus is checked for names that cannot coexist:
///
/// - **Duplicate versions.** Two files claiming one `name.major.minor`.
/// - **Case-insensitive filesystem collisions.** `ns.Foo` beside `ns.foo`, which are distinct in
///   DSDL and the same file on macOS and Windows.
///
/// Names that collide in a target language's output are a backend concern, checked where the names
/// are made.
///
/// @param[in] rootNamespaceDirs Root namespace directories. Definitions found here are targets.
/// @param[in] lookupDirs Additional directories searched for referenced types, not generated.
/// @param[in,out] diagnostics Diagnostic sink for discovery and I/O issues, and for the checks above.
/// @return Every definition found, sorted as described. Definitions are returned even when a check
///         above reported an error, so a caller that tolerates diagnostics still sees the corpus;
///         callers that must not proceed test @ref DiagnosticEngine::hasErrors.
std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DISCOVERY_H
