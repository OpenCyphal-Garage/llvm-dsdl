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

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{

/// @brief Renders one language-safe lowered helper-binding symbol.
/// @param[in] language Target naming policy language.
/// @param[in] helperSymbol Canonical lowered helper symbol.
/// @return Emitted helper-binding identifier.
std::string renderHelperBindingIdentifier(Language language, llvm::StringRef helperSymbol);

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
/// @param[in] qualifier A name every helper is prefixed with, for a language whose helpers share a
///                      scope with another definition's. Empty where the definition has a scope of
///                      its own.
/// @return The scope-local helper name.
std::string renderScopeLocalHelperName(Language        language,
                                       llvm::StringRef helperSymbol,
                                       llvm::StringRef schemaSymbol,
                                       llvm::StringRef qualifier = {});

/// @brief Names every helper of @p schema as the scope holding it reaches it.
///
/// The helpers of one definition are named together because the scope that keeps them apart is
/// what makes each name unique. Where the definition has a scope of its own -- a Rust module, a
/// TypeScript or Python module -- @p scope covers that definition alone and @p qualifier is empty.
/// Where several definitions share one -- a Go package holds a whole DSDL namespace -- @p scope is
/// the shared one and @p qualifier is the definition's type name, so the two answers together are
/// what the language needs: a name that reads as the definition's, and a scope that proves it.
///
/// Python says a module-level name is not part of the module's surface by beginning it with an
/// underscore, and that marker is applied here. Go says the same with the case of the first letter,
/// which its projection has already done; Rust and TypeScript say it by leaving off `pub` and
/// `export`.
///
/// A helper nothing calls is left out, as an accessors-only run has many.
/// @param[in] language Target naming policy language.
/// @param[in] module The lowered module.
/// @param[in] schema The definition's schema.
/// @param[in,out] scope The declaring scope, owned by the caller.
/// @param[in] qualifier A name every helper is prefixed with, or empty.
/// @return Each helper's lowered symbol, under the name the scope declared it as.
[[nodiscard]] llvm::StringMap<std::string> renderSchemaHelperNames(Language             language,
                                                                   mlir::ModuleOp       module,
                                                                   mlir::dsdl::SchemaOp schema,
                                                                   NamingScope&         scope,
                                                                   llvm::StringRef      qualifier = {});

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
