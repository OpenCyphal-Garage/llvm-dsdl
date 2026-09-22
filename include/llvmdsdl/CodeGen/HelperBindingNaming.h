//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared naming helpers for lowered helper-binding symbols.
///
/// This utility centralises helper binding symbol projection so emitters do not
/// duplicate `mlir_` prefix and language sanitization logic.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
#define LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H

#include <string>

#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Renders one language-safe lowered helper-binding symbol.
/// @param[in] language Target naming policy language.
/// @param[in] helperSymbol Canonical lowered helper symbol.
/// @return Emitted helper-binding identifier.
std::string renderHelperBindingIdentifier(CodegenNamingLanguage language, llvm::StringRef helperSymbol);

/// @brief Renders a helper's name for a scope that already names the definition.
///
/// The lowered symbol carries the helper's kind, the schema it belongs to, and the section, field
/// index and direction that distinguish it from its siblings. Where a language puts the definition
/// in a scope of its own -- a module, a namespace, a class -- the schema component names what the
/// scope already says, and the identifier reads as its own address. This drops the pass's prefix
/// and the schema component and projects what is left under @ref IdentifierRole::FunctionName.
///
/// The result is unique among one definition's helpers only up to the projection. Callers allocate
/// it from a @ref NamingScope covering the target scope, which is what keeps two helpers that
/// project onto one name apart.
/// @param[in] language Target naming policy language.
/// @param[in] helperSymbol Canonical lowered helper symbol.
/// @param[in] schemaSymbol The schema symbol the scope already names.
/// @return The scope-local helper name.
std::string renderScopeLocalHelperName(CodegenNamingLanguage language,
                                       llvm::StringRef       helperSymbol,
                                       llvm::StringRef       schemaSymbol);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
