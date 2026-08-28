//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared composite-import dependency collection and projection.
///
/// This component ensures stable ordering and de-duplication across scripted
/// emitters that consume composite imports.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/CompositeImportGraph.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

std::string dependencyKey(const std::string&  fullName,
                          const std::uint32_t majorVersion,
                          const std::uint32_t minorVersion)
{
    return fullName + ":" + std::to_string(majorVersion) + ":" + std::to_string(minorVersion);
}

std::string dependencyKey(const SemanticTypeRef& ref)
{
    return dependencyKey(ref.fullName, ref.majorVersion, ref.minorVersion);
}

}  // namespace

std::vector<SemanticTypeRef> collectCompositeDependencies(const SemanticSection&      section,
                                                          const DiscoveredDefinition& owner)
{
    const auto ownerKey = dependencyKey(owner.fullName, owner.majorVersion, owner.minorVersion);

    std::set<std::string>        seen;
    std::vector<SemanticTypeRef> out;
    for (const auto& field : section.fields)
    {
        if (field.isPadding || !field.resolvedType.compositeType)
        {
            continue;
        }

        const auto& ref    = *field.resolvedType.compositeType;
        const auto  refKey = dependencyKey(ref);
        if (refKey == ownerKey)
        {
            continue;
        }
        if (seen.insert(refKey).second)
        {
            out.push_back(ref);
        }
    }

    std::ranges::sort(out, [](const SemanticTypeRef& lhs, const SemanticTypeRef& rhs) {
        return dependencyKey(lhs) < dependencyKey(rhs);
    });
    return out;
}

std::vector<CompositeImportSpec> projectCompositeImports(
    const std::vector<SemanticTypeRef>&                       dependencies,
    const std::function<std::string(const SemanticTypeRef&)>& modulePathProjector,
    const std::function<std::string(const SemanticTypeRef&)>& typeNameProjector)
{
    std::map<std::string, std::set<std::string>> importsByModule;
    for (const auto& dependency : dependencies)
    {
        const auto modulePath = modulePathProjector(dependency);
        const auto typeName   = typeNameProjector(dependency);
        if (modulePath.empty() || typeName.empty())
        {
            continue;
        }
        importsByModule[modulePath].insert(typeName);
    }

    std::vector<CompositeImportSpec> out;
    for (const auto& [modulePath, typeNames] : importsByModule)
    {
        for (const auto& typeName : typeNames)
        {
            out.push_back({modulePath, typeName});
        }
    }
    return out;
}

}  // namespace llvmdsdl
