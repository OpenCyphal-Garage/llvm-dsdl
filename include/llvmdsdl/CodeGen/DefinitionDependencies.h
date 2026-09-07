//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared composite-dependency collection helpers for code generation.
///
/// This utility provides deterministic, deduplicated composite dependency
/// discovery for sections and definitions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_DEFINITION_DEPENDENCIES_H
#define LLVMDSDL_CODEGEN_DEFINITION_DEPENDENCIES_H

#include <string>
#include <vector>

#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

/// @brief Renders a stable dependency key for a semantic type reference.
/// @param[in] ref Referenced type.
/// @return Stable key string (`full_name:major:minor`).
std::string renderDefinitionDependencyKey(const SemanticTypeRef& ref);

/// @brief Collects deterministic, deduplicated composite dependencies in one section.
/// @param[in] section Section to analyse.
/// @return Composite type references sorted by stable key.
std::vector<SemanticTypeRef> collectSectionCompositeDependencies(const SemanticSection& section);

/// @brief Collects deterministic, deduplicated composite dependencies in one definition.
/// @param[in] def Definition to analyse.
/// @return Composite type references sorted by stable key.
std::vector<SemanticTypeRef> collectDefinitionCompositeDependencies(const SemanticDefinition& def);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_DEFINITION_DEPENDENCIES_H
