//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The naming parts of the semantic model, and the section scopes of
/// `llvmdsdl/Support/SectionScopes.h` built from them.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_SECTION_NAMING_H
#define LLVMDSDL_CODEGEN_SECTION_NAMING_H

#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{

/// @brief The parts of @p section that naming reads.
/// @param[in] section The section.
/// @return Its parts.
[[nodiscard]] SectionParts sectionParts(const SemanticSection& section);

/// @brief The parts of @p definition that naming reads.
/// @param[in] definition The definition.
/// @return Its parts.
[[nodiscard]] DefinitionParts definitionParts(const SemanticDefinition& definition);

/// @brief @ref makeSectionFieldScope over the parts of @p section.
/// @param[in] language Naming language.
/// @param[in] section The section whose fields are being named.
/// @return A scope with every field declared.
[[nodiscard]] NamingScope makeSectionFieldScope(Language language, const SemanticSection& section);

/// @brief @ref makeSectionConstantScope over the parts of @p section.
/// @param[in] language Naming language.
/// @param[in] section The section whose constants are being named.
/// @param[in] typeConstantPrefix As @ref makeSectionConstantScope takes it.
/// @return A scope with every constant declared.
[[nodiscard]] NamingScope makeSectionConstantScope(Language               language,
                                                   const SemanticSection& section,
                                                   llvm::StringRef        typeConstantPrefix);

/// @brief @ref makeGoConstantScope over the parts of @p section.
/// @param[in] section The section whose constants these are.
/// @param[in] typeName The section's Go type name.
/// @return The scope.
[[nodiscard]] NamingScope makeGoConstantScope(const SemanticSection& section, llvm::StringRef typeName);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_SECTION_NAMING_H
