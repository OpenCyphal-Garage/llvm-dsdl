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
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/SurfacePlan.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstddef>
#include <string>
#include <vector>

namespace llvmdsdl
{

class DiagnosticEngine;

/// @file
/// @brief Discovery routines for locating and loading DSDL definitions.

/// @brief A type's or a namespace's name, in the scope a definition shares with other definitions.
struct ScopedTypeName final
{
    /// @brief The scope, from @ref sharedScopeOf; for a namespace, its parent's.
    std::string scope;

    /// @brief The name as emitted.
    std::string name;

    /// @brief The name under @ref TypeNameVersioning::Versioned.
    std::string versionedName;

    /// @brief The section the type is: `request`, `response`, or empty.
    std::string section;

    /// @brief For a namespace, its full DSDL name; empty for a type.
    std::string namespaceName;
};

/// @brief The parts of @p info that naming reads, before its contents are known in full.
/// @param[in] info The definition.
/// @param[in] isService Whether the definition is a service.
/// @param[in] isDeprecated Whether the definition is deprecated.
/// @return Its parts, with sections that declare nothing.
[[nodiscard]] DefinitionParts discoveredParts(const DiscoveredDefinition& info, bool isService, bool isDeprecated);

/// @brief The scope the definition at @p index of @p plan declares its names in, where its language
///        shares one across definitions.
/// @param[in] plan The definitions' plan.
/// @param[in] index The definition's position in @p plan.
/// @return The namespace path, each component followed by `.`, or empty where the namespace is in
///         the identifier and the scope is global.
[[nodiscard]] std::string sharedScopeOf(const SurfacePlan& plan, std::size_t index);

/// @brief Every type name the definition at @p index declares in @p language's shared scope.
///
/// The definition's, each section's, and a deprecated struct's own where the language declares it
/// apart; and, where the row's `namespaceAndTypeShareScope` is set, each namespace's in its
/// parent's scope. Empty where the language gives every definition a module of its own. Each name is
/// read from the plan, with its versioned form from @p versioned.
/// @param[in] language Naming language.
/// @param[in] plan The definitions' plan under the run's versioning.
/// @param[in] versioned The same definitions' plan under @ref TypeNameVersioning::Versioned.
/// @param[in] index The definition's position in both plans.
/// @param[in] namespaceComponents The definition's DSDL namespace, which names a namespace in a
///            diagnostic.
/// @return The names, namespaces first.
[[nodiscard]] std::vector<ScopedTypeName> scopedTypeNames(const LanguageTraits&       language,
                                                          const SurfacePlan&          plan,
                                                          const SurfacePlan&          versioned,
                                                          std::size_t                 index,
                                                          llvm::ArrayRef<std::string> namespaceComponents);

/// @brief Rejects a generated type name that another name declared in its scope also takes.
///
/// A service emits a type per section, named after the service with a suffix -- Go's `Foo` gives
/// `FooRequest`. A sibling definition may be *called* `FooRequest`, which is conformant DSDL, and
/// then the two land on one identifier. @ref discoverDefinitions cannot see this: it keys each
/// definition on its own short name, and `Foo` and `FooRequest` do not collide as declared names.
/// A section declared in its service's own type is in no shared scope, and is not claimed.
///
/// A deprecated definition's C++ struct is declared as `<name>_`, with `<name>` a deprecated alias of
/// it (@ref renderDeclaredTypeName), so that name is claimed as well.
///
/// Where the row's `namespaceAndTypeShareScope` is set, each namespace is claimed in its parent's
/// scope beside the types: `ns.Foo` and a namespace `ns.Foo` are both C++'s `ns::Foo`, and so are
/// `ns.Foo.1.0` and a namespace `ns.Foo_1_0` under @ref TypeNameVersioning::Versioned.
///
/// Only where a language shares one scope across a namespace does this break a build -- C in its
/// single global scope, C++ in the namespace, Go in the package. Rust, TypeScript and Python give
/// every definition its own module, so the repeat is unreachable and is not reported.
///
/// A language whose `namespaceJoin` puts the namespace in the identifier has its names checked in
/// one scope across every namespace. C joins with `__`, which a DSDL name may hold as well, so
/// `ns.A__B` and `ns.A.B` are both `ns__A__B`.
///
/// The check runs after parsing because that is where a definition is known to be a service, and it
/// reads each name from `allocateSurface`, which composes them with the calls the emitters use.
///
/// @param[in] definitions Parsed definitions to check.
/// @param[in] outputLanguages Languages whose output names are checked; empty disables the check.
/// @param[in] versioning Whether generated type names carry the version, which decides the names
///            compared. The diagnostic suggests `--versioned-type-names` where the versioned names
///            of the two differ.
/// @param[in,out] diagnostics Diagnostic sink.
void checkScopedTypeNameCollisions(llvm::ArrayRef<ParsedDefinition> definitions,
                                   llvm::ArrayRef<LanguageTraits>   outputLanguages,
                                   TypeNameVersioning               versioning,
                                   DiagnosticEngine&                diagnostics);

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
/// - **Generated-output collisions.** Two distinct types whose names project onto one output file
///   or one type name in a selected language. Both projections are many-to-one and they fold
///   differently -- `FooBar`/`Foo_bar` meet as file names, `Break`/`Break_` meet once the keyword
///   escape fires -- so whichever half collides, one type would be lost or the output would not
///   compile. The names come from `allocateSurface`, which composes them with the calls the
///   emitters use, so the check cannot drift from what is written.
/// - **File and directory collisions.** A definition whose output file and a namespace whose
///   directory take one module name, in a selected language where a file and a directory of one
///   name are one module: `ns/File.1.0` beside `ns/file_1_0/` in Rust and Python.
///
/// It does *not* catch a service section colliding with a sibling type, because that needs to know
/// which definitions are services and this runs before parsing. See
/// @ref checkScopedTypeNameCollisions.
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
///            @ref allLanguageTraits, since there is no build to fail and hiding a collision helps
///            nobody. Empty disables the check entirely.
/// @return Every definition found, sorted as described. Definitions are returned even when a check
///         above reported an error, so a caller that tolerates diagnostics still sees the corpus;
///         callers that must not proceed test @ref DiagnosticEngine::hasErrors.
std::vector<DiscoveredDefinition> discoverDefinitions(const std::vector<std::string>& rootNamespaceDirs,
                                                      const std::vector<std::string>& lookupDirs,
                                                      DiagnosticEngine&               diagnostics,
                                                      llvm::ArrayRef<LanguageTraits>  outputLanguages = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DISCOVERY_H
