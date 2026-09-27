//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/ImportNameScope.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

ImportNameScope::ImportNameScope(const Language language, Brought brought)
    : language_(language)
    , brought_(std::move(brought))
{
}

void ImportNameScope::reserve(const llvm::StringRef name)
{
    declared_.insert(name.str());
}

std::string ImportNameScope::claim(const DefinitionRef& ref, const std::string& exported, const bool deprecated)
{
    const std::string key = renderDefinitionKey(ref);
    if (const auto found = claims_.find(key); found != claims_.end())
    {
        return found->second;
    }
    // A candidate is composed from the raw parts and projected once, so the projection decides the
    // casing of the whole name rather than of each part separately. Projecting a part on its own
    // leaves its separator behind: a namespace component the language claims contributes
    // `Default_`, and a deprecated dependency's exported name ends in its marker. The marker is
    // applied after the projection, since it is a suffix the projection would fold away.
    const auto compose = [&](const std::string& raw) {
        return renderDeclaredTypeName(codegenProjectIdentifier(language_, IdentifierRole::TypeName, raw), deprecated);
    };
    std::string local = exported;
    for (std::size_t depth = 1; taken(local) && (depth <= ref.namespaceComponents.size()); ++depth)
    {
        std::string raw;
        for (const auto& component : llvm::ArrayRef<std::string>(ref.namespaceComponents).take_back(depth))
        {
            raw += component + "_";
        }
        local = compose(raw + ref.shortName);
    }
    for (unsigned ordinal = 2U; taken(local); ++ordinal)
    {
        local = compose(ref.shortName + "_" + std::to_string(ordinal));
    }
    for (std::string& name : broughtBy(local))
    {
        imported_.insert(std::move(name));
    }
    claims_[key] = local;
    return local;
}

std::string ImportNameScope::localName(const DefinitionRef& ref, const std::string& exported) const
{
    const auto found = claims_.find(renderDefinitionKey(ref));
    return (found == claims_.end()) ? exported : found->second;
}

std::vector<std::string> ImportNameScope::broughtBy(const std::string& local) const
{
    return brought_ ? brought_(local) : std::vector<std::string>{local};
}

bool ImportNameScope::taken(const std::string& local) const
{
    return llvm::any_of(broughtBy(local),
                        [&](const std::string& name) { return declared_.contains(name) || imported_.contains(name); });
}

}  // namespace llvmdsdl
