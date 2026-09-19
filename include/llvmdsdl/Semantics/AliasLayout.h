//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
/// @file
/// @brief Decides, for each section, whether its wire form is a flat byte image and whether the
///        generated structure is that same image.
///
/// One implementation, two consumers: the `@aliasable` diagnostic reads the verdict where the
/// source locations are, and the lowered attribute carries it to every backend. The pass over
/// lowered IR verifies this answer rather than computing a second one.

#ifndef LLVMDSDL_SEMANTICS_ALIAS_LAYOUT_H
#define LLVMDSDL_SEMANTICS_ALIAS_LAYOUT_H

#include "llvmdsdl/Semantics/Model.h"

#include <llvm/ADT/StringRef.h>

namespace llvmdsdl
{

/// @brief Records `wireFlat` and `hostImage` on every section of `module`.
/// @param[in,out] module Module whose sections receive the verdicts.
/// @param[in] externalCatalog Catalogue consulted for composites the module does not define.
void annotateAliasLayout(SemanticModule&       module,
                         const SemanticModule* externalCatalog = nullptr,
                         bool                  aliasableViews  = false);

/// @brief The wire-format token for a reason, as the generated constants spell it.
llvm::StringRef aliasLayoutReasonToken(AliasLayoutReason reason);

/// @brief A sentence naming what blocked the layout, for a diagnostic.
/// @param[in] verdict Verdict to describe; it must not hold.
std::string describeAliasLayoutVerdict(const AliasLayoutVerdict& verdict);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SEMANTICS_ALIAS_LAYOUT_H
