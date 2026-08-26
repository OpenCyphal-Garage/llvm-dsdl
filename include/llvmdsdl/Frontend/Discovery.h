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
#include "llvmdsdl/Support/DefinitionNaming.h"
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

/// @brief Every language whose output names can be checked.
///
/// Pass this when the invocation emits no source of its own: there is no build to fail, so reporting
/// a collision costs nothing and hiding one helps nobody. An invocation that does emit source passes
/// only what it emits, so it never fails over output it was not going to produce.
/// @return All six target languages, in a fixed order.
llvm::ArrayRef<OutputLanguage> allOutputLanguages();

/// @brief Discovers and loads definitions reachable from namespace roots.
///
/// @param[in] rootNamespaceDirs Root namespace directories.
/// @param[in] lookupDirs Additional lookup directories.
/// @param[in,out] diagnostics Diagnostic sink for discovery/I/O issues.
/// @param[in] outputLanguages Languages whose output names are checked. Two distinct types whose
///            names project onto one output file or one type name in any of these are rejected.
///            A source-emitting invocation passes the language it emits, so a build never fails over
///            a hazard in output it was not going to produce; an analysis invocation that emits
///            nothing passes @ref allOutputLanguages, because there is no build to fail and the
///            diagnostic is pure information. Empty disables the check.
/// @return Discovered definitions with metadata and source text.
/// @brief Rejects a service section whose generated type name collides with another type's.
///
/// A service emits a type per section, named after the service with a suffix -- `Foo` gives
/// `Foo_Request`. A sibling definition may be *called* `Foo_Request`, which is conformant DSDL, and
/// then the two land on one identifier. @ref discoverDefinitions cannot see this: it keys each
/// definition on its own short name, and `Foo` and `Foo_Request` do not collide as declared names.
///
/// Only where a language shares one scope across a namespace does this break a build -- C in its
/// single global scope, C++ in the namespace, Go in the package. Rust, TypeScript and Python give
/// every definition its own module, so the repeat is unreachable and is not reported.
///
/// The check runs after parsing because that is where a definition is known to be a service, and it
/// composes the section name with @ref renderSectionTypeSuffix, the same call the emitters use.
///
/// @param[in] definitions Parsed definitions to check.
/// @param[in] outputLanguages Languages whose output names are checked; empty disables the check.
/// @param[in] versioning Whether generated type names carry the version. Under
///            @ref TypeNameVersioning::Versioned the two names differ and nothing is reported.
/// @param[in,out] diagnostics Diagnostic sink.
void checkServiceSectionTypeNameCollisions(llvm::ArrayRef<ParsedDefinition> definitions,
                                           llvm::ArrayRef<OutputLanguage>  outputLanguages,
                                           TypeNameVersioning              versioning,
                                           DiagnosticEngine&               diagnostics);

std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics,
                                                      llvm::ArrayRef<OutputLanguage>  outputLanguages = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DISCOVERY_H
