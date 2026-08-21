//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared identifier scopes for one section's attributes.
///
/// The emitters and the naming manifest both name a section's attributes through these, which is
/// what makes the manifest a report of the identifiers a backend writes.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SECTION_NAMING_H
#define LLVMDSDL_CODEGEN_SECTION_NAMING_H

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

/// @brief Builds the field-name scope for @p section in @p language.
///
/// Fields are declared in DSDL order, which makes the assignment reproducible; padding fields carry
/// no name and are skipped.
/// @param[in] language Naming language.
/// @param[in] section The section whose fields are being named.
/// @return A scope with every field declared.
[[nodiscard]] NamingScope makeSectionFieldScope(CodegenNamingLanguage language, const SemanticSection& section);

/// @brief Builds the constant-name scope for @p section in @p language.
/// @param[in] language Naming language.
/// @param[in] section The section whose constants are being named.
/// @return A scope with every constant declared.
[[nodiscard]] NamingScope makeSectionConstantScope(CodegenNamingLanguage language, const SemanticSection& section);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SECTION_NAMING_H
