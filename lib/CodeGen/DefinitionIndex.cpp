//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared semantic-definition lookup index for code generation.
///
/// This component centralises keyed definition lookups used by emitter contexts.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/DefinitionIndex.h"

#include <cstdint>
#include <set>
#include <string>

#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{
namespace
{

/// @brief The key a definition is indexed under: its full name and version.
std::string typeKey(const std::string& name, const std::uint32_t major, const std::uint32_t minor)
{
    return name + ":" + std::to_string(major) + ":" + std::to_string(minor);
}

/// @brief Whether @p section holds a view; @p visiting ends a walk DSDL forbids from reaching itself.
bool holdsView(const DefinitionIndex& index, const SemanticSection& section, std::set<const SemanticSection*>& visiting)
{
    if (!visiting.insert(&section).second)
    {
        return false;
    }
    bool holds = false;
    for (const auto& field : section.fields)
    {
        if (field.heldAsView)
        {
            holds = true;
            break;
        }
        if (field.resolvedType.compositeType)
        {
            const SemanticDefinition* const nested = index.find(*field.resolvedType.compositeType);
            if ((nested != nullptr) && holdsView(index, nested->request, visiting))
            {
                holds = true;
                break;
            }
        }
    }
    visiting.erase(&section);
    return holds;
}

}  // namespace

DefinitionIndex::DefinitionIndex(const SemanticModule& semantic)
{
    for (const auto& def : semantic.definitions)
    {
        byKey_.emplace(typeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion), &def);
    }
}

const SemanticDefinition* DefinitionIndex::find(const SemanticTypeRef& ref) const
{
    const auto it = byKey_.find(typeKey(ref.fullName, ref.majorVersion, ref.minorVersion));
    if (it == byKey_.end())
    {
        return nullptr;
    }
    return it->second;
}

bool DefinitionIndex::holdsView(const SemanticSection& section) const
{
    std::set<const SemanticSection*> visiting;
    return llvmdsdl::holdsView(*this, section, visiting);
}

}  // namespace llvmdsdl
