//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The naming manifest: what every DSDL name is called in the generated output.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_NAMING_MANIFEST_H
#define LLVMDSDL_CODEGEN_NAMING_MANIFEST_H

#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Semantics/Model.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <string>

namespace llvmdsdl
{

/// @brief The surface a generation run's lowering wrote for its target, for one profile.
struct ManifestSurface final
{
    /// @brief The profile; empty for a language without profiles.
    std::string profile;

    SurfacePlan plan;
};

/// @brief Renders the DSDL-name to generated-identifier map as JSON.
///
/// The map answers the question a user cannot otherwise answer without reading generated source:
/// what did my field become? It is also what lets a build integration reference a generated symbol
/// without reimplementing the projection, and what gives the language server its hover text.
/// @param[in] semantic The analysed definitions.
/// @param[in] languages Languages to report, each with the name to key it under.
/// @param[in] toolVersion Generator version, recorded so a consumer can tell manifests apart.
/// @return The manifest as pretty-printed JSON, newline-terminated.
/// @param[in] typeNameVersioning The scheme this invocation generated under. The names below depend
///            on it, so a consumer should not have to infer which one produced them.
/// @param[in] target The language a generation run generated, whose definitions report the whole
///            surface its lowering wrote; none for an analysis run, which reports the definition
///            layer alone.
/// @param[in] surfaces The target's surfaces, one for each profile. Where there are several, each
///            definition reports each under `profiles`.
[[nodiscard]] std::string renderNamingManifest(const SemanticModule&           semantic,
                                               llvm::ArrayRef<LanguageTraits>  languages,
                                               llvm::StringRef                 toolVersion,
                                               TypeNameVersioning              typeNameVersioning,
                                               std::optional<Language>         target   = std::nullopt,
                                               llvm::ArrayRef<ManifestSurface> surfaces = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NAMING_MANIFEST_H
