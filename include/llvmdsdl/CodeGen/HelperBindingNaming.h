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
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvmdsdl
{

/// @brief Renders one language-safe helper-binding identifier from a composed name.
/// @param[in] language Target naming policy language.
/// @param[in] name The composed name.
/// @return Emitted helper-binding identifier.
std::string renderHelperBindingIdentifier(Language language, llvm::StringRef name);

/// @brief Names a helper where the scope holding it is the definition's namespace.
///
/// C++ declares a helper in the definition's namespace, beside the definitions sharing it, so the
/// name carries the helper's kind, the definition's full name and version, and the section, step
/// and direction that tell it from its siblings: `mlir_llvmdsdl_plan_capacity_check_ns_Msg_1_0`.
/// The full name's dots become underscores. Within one namespace that reaches no name twice: the
/// definitions differ in their short names, and a version is two numbers after them.
/// @param[in] language Target naming policy language.
/// @param[in] helper The helper, as its symbol reads.
/// @return Emitted helper-binding identifier.
std::string renderHelperBindingIdentifier(Language language, const PlanSymbol& helper);

/// @brief Renders a helper's name for a scope that already names the definition.
///
/// Where a language puts the definition in a scope of its own -- a module, a namespace, a class --
/// the definition is what the scope already says, and a helper is named by its kind and the
/// section, step and direction that tell it from its siblings, projected under
/// @ref IdentifierRole::InternalFunctionName.
///
/// The result is unique among one definition's helpers only up to the projection. Callers allocate
/// it from a @ref NamingScope covering the target scope, which is what keeps two helpers that
/// project onto one name apart.
/// @param[in] language Target naming policy language.
/// @param[in] helper The helper, as its symbol reads.
/// @param[in] qualifier A name every helper is prefixed with, for a language whose helpers share a
///                      scope with another definition's. Empty where the definition has a scope of
///                      its own.
/// @return The scope-local helper name.
std::string renderScopeLocalHelperName(Language language, const PlanSymbol& helper, llvm::StringRef qualifier = {});

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
