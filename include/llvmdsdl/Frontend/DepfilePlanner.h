//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Cached dependency planner for per-output depfile input resolution.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_FRONTEND_DEPFILEPLANNER_H
#define LLVMDSDL_FRONTEND_DEPFILEPLANNER_H

#include "llvmdsdl/Semantics/Model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvmdsdl
{

/// @brief Resolves and caches transitive DSDL input dependencies for depfile emission.
///
/// @details
/// The planner indexes one semantic module once, then memoizes:
/// 1) per-type transitive input paths; and
/// 2) per-required-type-set merged input paths.
///
/// Returned dependency vectors are normalized absolute paths, sorted, and de-duplicated.
///
/// Synthetic embedded-uavcan paths are never emitted as dependencies -- they do not name files a
/// build system can stat. An output that depends on the compiled-in catalog is not thereby
/// dependency-free, though: the catalog is a real input that changes when the compiler carrying it
/// changes. Such outputs get `toolchainStampPath` (normally the `dsdlc` executable) as a stand-in
/// prerequisite, so upgrading the compiler rebuilds what its catalog produced.
class DepfilePlanner final
{
public:
    /// @brief Build planner index from semantic definitions.
    ///
    /// @param[in] semantic Semantic module covering every type reachable from generated outputs.
    ///     Embedded-catalog definitions must be present when they participate in the closure;
    ///     otherwise the planner cannot distinguish "supplied by the compiled-in catalog" from
    ///     "unknown type", and both silently yield no dependencies.
    /// @param[in] toolchainStampPath Prerequisite recorded for outputs that draw on the embedded
    ///     catalog or on compiled-in support content. Empty disables the behavior.
    explicit DepfilePlanner(const SemanticModule& semantic, std::string toolchainStampPath = {});

    /// @brief Returns the dependency list for an output rendered entirely from compiled-in content.
    ///
    /// @details
    /// Support artifacts -- runtime headers, manifests, scaffolding -- are rendered from data
    /// compiled into the compiler and reference no DSDL definition, so the binary is their only
    /// input. Without this they would get a rule with no prerequisites and never rebuild.
    ///
    /// @return A one-element list naming the toolchain stamp, or an empty list when none is set.
    const std::vector<std::string>& depsForCompilerOnlyOutput();

    /// @brief Returns transitive input dependencies for one type key.
    ///
    /// @param[in] typeKey Canonical type key (`full_name:major:minor`).
    /// @return Cached sorted+deduped dependency list.
    const std::vector<std::string>& depsForTypeKey(const std::string& typeKey);

    /// @brief Returns merged dependencies for required type keys.
    ///
    /// @param[in] requiredTypeKeys Required output type keys (order-insensitive).
    /// @return Cached sorted+deduped merged dependency list.
    const std::vector<std::string>& depsForRequiredTypeKeys(const std::vector<std::string>& requiredTypeKeys);

private:
    struct Node final
    {
        std::vector<std::size_t> dependencyIndexes;
        std::string              normalizedInputPath;
        bool                     fromEmbeddedCatalog{false};
    };

    std::string                                               toolchainStampPath_;
    std::vector<std::string>                                  compilerOnlyDeps_;
    std::vector<Node>                                         nodes_;
    std::unordered_map<std::string, std::size_t>              nodeIndexByTypeKey_;
    std::unordered_map<std::string, std::vector<std::string>> depsByTypeKey_;
    std::unordered_map<std::string, std::vector<std::string>> depsByRequiredSetSignature_;
    std::vector<std::uint32_t>                                visitEpochByNode_;
    std::uint32_t                                             nextVisitEpoch_{1U};
    std::vector<std::string>                                  emptyDeps_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_FRONTEND_DEPFILEPLANNER_H
