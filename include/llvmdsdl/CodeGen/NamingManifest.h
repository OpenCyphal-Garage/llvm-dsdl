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
#include "llvmdsdl/Semantics/Model.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace llvmdsdl
{

/// @brief Renders the DSDL-name to generated-identifier map as JSON.
///
/// The map answers the question a user cannot otherwise answer without reading generated source:
/// what did my field become? It is also what lets a build integration reference a generated symbol
/// without reimplementing the projection, and what gives the language server its hover text.
/// @param[in] semantic The analysed definitions.
/// @param[in] languages Languages to report, each with the name to key it under.
/// @param[in] toolVersion Generator version, recorded so a consumer can tell manifests apart.
/// @return The manifest as pretty-printed JSON, newline-terminated.
[[nodiscard]] std::string renderNamingManifest(const SemanticModule&          semantic,
                                               llvm::ArrayRef<OutputLanguage> languages,
                                               llvm::StringRef                toolVersion);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_NAMING_MANIFEST_H
