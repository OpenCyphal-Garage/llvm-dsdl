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
/// Discovery is also where a corpus is checked for names that cannot coexist, because it is the
/// first point that has seen all of them:
///
/// - **Duplicate versions.** Two files claiming one `name.major.minor`.
/// - **Case-insensitive filesystem collisions.** `ns.Foo` beside `ns.foo`, which are distinct in
///   DSDL and the same file on macOS and Windows.
/// - **Generated-output collisions.** Two distinct types whose names project onto one output file
///   or one type name in a selected language. Both projections are many-to-one and they fold
///   differently -- `FooBar`/`Foo_bar` meet as file names, `Break`/`Break_` meet once the keyword
///   escape fires -- so whichever half collides, one type would be lost or the output would not
///   compile. The keys come from the same engine the emitters name with, so the check cannot drift
///   from what is actually written.
///
/// It does *not* catch a service section colliding with a sibling type, because that needs to know
/// which definitions are services and this runs before parsing. See
/// @ref checkServiceSectionTypeNameCollisions.
///
/// A rename that changes a path -- an escaped file or namespace name -- is reported as a note rather
/// than silently applied, since it changes what a build has to reference.
///
/// @param[in] rootNamespaceDirs Root namespace directories. Definitions found here are targets.
/// @param[in] lookupDirs Additional directories searched for referenced types, not generated.
/// @param[in,out] diagnostics Diagnostic sink for discovery and I/O issues, and for the checks above.
/// @param[in] outputLanguages Languages whose output names are checked. A source-emitting invocation
///            passes the language it emits, so a build never fails over a hazard in output it was
///            not going to produce; an analysis invocation that emits nothing passes
///            @ref allOutputLanguages, because there is no build to fail and the diagnostic is pure
///            information. Empty disables the check entirely.
/// @return Every definition found, sorted as described. Definitions are returned even when a check
///         above reported an error, so a caller that tolerates diagnostics still sees the corpus;
///         callers that must not proceed test @ref DiagnosticEngine::hasErrors.
std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics,
                                                      llvm::ArrayRef<OutputLanguage>  outputLanguages = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DISCOVERY_H
