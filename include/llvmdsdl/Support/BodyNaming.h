//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The names of a definition's lowered functions: its helpers, and the link names of the bodies a
/// language compiles apart from their entry points.
///
/// `allocateSurface` and the emitters name the lowered functions through these. They take the
/// functions' parsed symbols rather than the module, so that a caller without one can name them.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_BODY_NAMING_H
#define LLVMDSDL_SUPPORT_BODY_NAMING_H

#include <string>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/PlanSymbol.h"

namespace llvmdsdl
{

/// @brief One lowered function a definition owns.
struct BodyParts final
{
    /// @brief The function's symbol.
    std::string symbol;

    /// @brief What the symbol names.
    PlanSymbol plan;

    /// @brief Whether it is a helper no body calls, as an accessors-only run has many.
    bool unreferenced{};
};

/// @brief Renders a helper's name for a scope that already names the definition.
///
/// Where a language puts the definition in a scope of its own -- a module, a namespace, a class --
/// the definition is what the scope already says, and a helper is named by its kind and the
/// section, step and direction that tell it from its siblings, projected under
/// @ref IdentifierRole::InternalFunctionName. Where the scope is the section's own type, the type
/// says the section too, and the name leaves it out.
///
/// The result is unique among one definition's helpers only up to the projection. Callers allocate
/// it from a @ref NamingScope covering the target scope, which is what keeps two helpers that
/// project onto one name apart.
/// @param[in] language Target naming policy language.
/// @param[in] helper The helper, as its symbol reads.
/// @param[in] qualifier A name every helper is prefixed with, for a language whose helpers share a
///                      scope with another definition's. Empty where the definition has a scope of
///                      its own.
/// @param[in] sectionScope Whether the scope is the section's own type.
/// @return The scope-local helper name.
[[nodiscard]] std::string renderScopeLocalHelperName(Language          language,
                                                     const PlanSymbol& helper,
                                                     llvm::StringRef   qualifier,
                                                     bool              sectionScope);

/// @brief Names the lowered body @p symbol names as a language that compiles the bodies apart from
///        their entry points links it.
///
/// The IR names a function by its definition's DSDL identity, which such a language cannot
/// declare. It spells the name the entry point or accessor takes for the section's type name with
/// its version, which its one global scope keeps unique: `ns__Msg_1_0__serialize_`,
/// `ns__Msg_1_0__get_speed_`. A helper is not linked: it is declared where the row places it.
/// @param[in] language A language whose row compiles its bodies apart from their entry points.
/// @param[in] symbol The function, which is not a helper.
/// @param[in] member The member an accessor reads or writes, as the section's type declares it.
/// @return The link name.
[[nodiscard]] std::string renderLoweredLinkName(Language language, const PlanSymbol& symbol, llvm::StringRef member);

/// @brief Names every helper among @p bodies as the scope holding it reaches it.
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
/// @param[in] bodies The definition's lowered functions.
/// @param[in,out] scope The declaring scope, owned by the caller.
/// @param[in] qualifier A name every helper is prefixed with, or empty.
/// @param[in] sectionScope Whether @p scope is the section's own type, which every one of
///            @p bodies belongs to.
/// @return Each helper's lowered symbol, under the name the scope declared it as.
[[nodiscard]] llvm::StringMap<std::string> declareHelperNames(Language                  language,
                                                              llvm::ArrayRef<BodyParts> bodies,
                                                              NamingScope&              scope,
                                                              llvm::StringRef           qualifier,
                                                              bool                      sectionScope);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_BODY_NAMING_H
