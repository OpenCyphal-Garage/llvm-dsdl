//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared composite-import dependency collection and projection helpers.
///
/// This utility centralises deterministic dependency discovery for composite
/// field references and language-specific import projection.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_COMPOSITE_IMPORT_GRAPH_H
#define LLVMDSDL_CODEGEN_COMPOSITE_IMPORT_GRAPH_H

#include <functional>
#include <string>
#include <vector>

#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

/// @brief One projected import item.
struct CompositeImportSpec final
{
    /// @brief Language/projected module path.
    std::string modulePath;

    /// @brief Type symbol imported from the module.
    std::string typeName;
};

/// @brief Collects unique non-self composite type dependencies from a section.
/// @param[in] section Semantic section to scan.
/// @param[in] owner Owning definition used for self-dependency filtering.
/// @return Deterministically ordered composite type references.
std::vector<SemanticTypeRef> collectCompositeDependencies(const SemanticSection&      section,
                                                          const DiscoveredDefinition& owner);

/// @brief Projects collected dependencies into deterministic import entries.
/// @param[in] dependencies Composite dependencies from
/// `collectCompositeDependencies`.
/// @param[in] modulePathProjector Projects one dependency to a module path.
/// @param[in] typeNameProjector Projects one dependency to a type symbol.
/// @return Sorted import entries with duplicates removed.
std::vector<CompositeImportSpec> projectCompositeImports(
    const std::vector<SemanticTypeRef>&                       dependencies,
    const std::function<std::string(const SemanticTypeRef&)>& modulePathProjector,
    const std::function<std::string(const SemanticTypeRef&)>& typeNameProjector);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_COMPOSITE_IMPORT_GRAPH_H
