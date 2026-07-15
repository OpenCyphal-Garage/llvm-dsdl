//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared snake_case name canonicalization.
///
/// A single definition of the snake_case projection so that identifier naming (backend emitters) and
/// output-name collision detection (frontend discovery) fold names identically.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_NAMECANONICALIZATION_H
#define LLVMDSDL_SUPPORT_NAMECANONICALIZATION_H

#include <string>

#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Projects @p name into snake_case (lowercase, `_` inserted at case boundaries, runs of
///        non-alphanumeric characters collapsed to a single `_`).
/// @param[in] name Source name.
/// @return snake_case form.
[[nodiscard]] std::string canonicalSnakeCase(llvm::StringRef name);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_NAMECANONICALIZATION_H
