//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared composite-dependency collection helpers.
///
/// The collector deduplicates by stable dependency key and returns deterministic
/// order for reproducible generation.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/DefinitionDependencies.h"
#include "llvmdsdl/Semantics/Model.h"

#include <map>
#include <string>
#include <vector>

namespace llvmdsdl
{

std::string renderDefinitionDependencyKey(const SemanticTypeRef& ref)
{
    return ref.fullName + ":" + std::to_string(ref.majorVersion) + ":" + std::to_string(ref.minorVersion);
}

std::vector<SemanticTypeRef> collectSectionCompositeDependencies(const SemanticSection& section)
{
    std::map<std::string, SemanticTypeRef> byKey;
    for (const auto& field : section.fields)
    {
        if (!field.resolvedType.compositeType)
        {
            continue;
        }
        const auto& ref = *field.resolvedType.compositeType;
        byKey.emplace(renderDefinitionDependencyKey(ref), ref);
    }

    std::vector<SemanticTypeRef> out;
    out.reserve(byKey.size());
    for (const auto& [_, ref] : byKey)
    {
        out.push_back(ref);
    }
    return out;
}

std::vector<SemanticTypeRef> collectDefinitionCompositeDependencies(const SemanticDefinition& def)
{
    std::map<std::string, SemanticTypeRef> byKey;
    const auto                             addSection = [&](const SemanticSection& section) {
        for (const auto& ref : collectSectionCompositeDependencies(section))
        {
            byKey.emplace(renderDefinitionDependencyKey(ref), ref);
        }
    };
    addSection(def.request);
    if (def.response)
    {
        addSection(*def.response);
    }

    std::vector<SemanticTypeRef> out;
    out.reserve(byKey.size());
    for (const auto& [_, ref] : byKey)
    {
        out.push_back(ref);
    }
    return out;
}

}  // namespace llvmdsdl
