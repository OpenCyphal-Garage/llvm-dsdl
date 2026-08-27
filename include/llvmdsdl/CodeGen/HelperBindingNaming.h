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
/// This utility centralizes helper binding symbol projection so emitters do not
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

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_HELPER_BINDING_NAMING_H
