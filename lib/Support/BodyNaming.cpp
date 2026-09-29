//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the names of a definition's lowered functions.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/BodyNaming.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/PlanSymbol.h"

namespace llvmdsdl
{
namespace
{

std::string collapseUnderscoreRuns(const llvm::StringRef name)
{
    std::string out;
    out.reserve(name.size());
    for (const char c : name)
    {
        if (c == '_' && !out.empty() && out.back() == '_')
        {
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string helperParts(const PlanSymbol& helper, const llvm::StringRef separator)
{
    std::string out = helper.helperKind;
    if (!helper.section.empty())
    {
        out += separator.str() + helper.section;
    }
    if (helper.step)
    {
        out += separator.str() + std::to_string(*helper.step);
    }
    if (helper.direction != PlanHelperDirection::None)
    {
        out += separator.str() + ((helper.direction == PlanHelperDirection::Serialize) ? "ser" : "deser");
    }
    return out;
}

/// @brief The versioned type name of the section @p symbol belongs to.
std::string versionedSectionTypeName(const Language language, const PlanSymbol& symbol)
{
    llvm::SmallVector<llvm::StringRef, 8> parts;
    llvm::StringRef(symbol.schema.fullName).split(parts, '.');
    const std::vector<std::string> namespaceComponents(parts.begin(), std::prev(parts.end()));
    return renderSectionTypeName(language,
                                 renderDefinitionTypeName(language,
                                                          namespaceComponents,
                                                          parts.back(),
                                                          symbol.schema.major,
                                                          symbol.schema.minor,
                                                          TypeNameVersioning::Versioned),
                                 symbol.section);
}

}  // namespace

std::string renderHelperBindingIdentifier(const Language language, const llvm::StringRef name)
{
    // C++ reserves any identifier containing a double underscore, so for that language the runs are
    // collapsed -- after the prefix is joined, since a name beginning with an underscore would make
    // a fresh pair at the seam. No other language reserves an interior double underscore.
    std::string joined = "mlir_" + codegenSanitizeIdentifier(language, name);
    if (languageTraits(language).classification.reservedUnderscores != ReservedUnderscores::LeadingAndInterior)
    {
        return joined;
    }

    return collapseUnderscoreRuns(joined);
}

std::string renderHelperBindingIdentifier(const Language language, const PlanSymbol& helper)
{
    std::string fullName = helper.schema.fullName;
    std::ranges::replace(fullName, '.', '_');
    const std::string version = std::to_string(helper.schema.major) + "_" + std::to_string(helper.schema.minor);
    std::string       parts   = helperParts(helper, "__");
    const std::size_t kindEnd = helper.helperKind.size();
    // The definition sits between the kind and what tells the helper from its siblings.
    parts.insert(kindEnd, "__" + fullName + "_" + version);
    return renderHelperBindingIdentifier(language, "llvmdsdl_plan_" + parts);
}

std::string renderScopeLocalHelperName(const Language        language,
                                       const PlanSymbol&     helper,
                                       const llvm::StringRef qualifier)
{
    std::string name = collapseUnderscoreRuns(helperParts(helper, "_"));
    while (!name.empty() && name.front() == '_')
    {
        name.erase(name.begin());
    }
    while (!name.empty() && name.back() == '_')
    {
        name.pop_back();
    }
    if (!qualifier.empty())
    {
        name = qualifier.str() + "_" + name;
    }

    return codegenProjectIdentifier(language, IdentifierRole::InternalFunctionName, name);
}

std::string renderLoweredLinkName(const Language language, const PlanSymbol& symbol)
{
    const std::string type = versionedSectionTypeName(language, symbol);
    switch (symbol.function)
    {
    case PlanFunction::Serialize:
        return renderLoweredEntryPointName(language, type, EntryPoint::Serialize);
    case PlanFunction::Deserialize:
        return renderLoweredEntryPointName(language, type, EntryPoint::Deserialize);
    case PlanFunction::Initialize:
        return renderLoweredEntryPointName(language, type, EntryPoint::Initialize);
    case PlanFunction::AppendWireImage:
        return renderLoweredEntryPointName(language, type, EntryPoint::AppendWireImage);
    case PlanFunction::WireImage:
        return renderLoweredEntryPointName(language, type, EntryPoint::WireImage);
    case PlanFunction::ReadWireImage:
        return renderLoweredEntryPointName(language, type, EntryPoint::ReadWireImage);
    case PlanFunction::Get:
        return renderLoweredAccessorName(language, type, AccessorVerb::Get, symbol.member);
    case PlanFunction::Set:
        return renderLoweredAccessorName(language, type, AccessorVerb::Set, symbol.member);
    case PlanFunction::Helper:
        break;
    }
    std::string helper = "llvmdsdl_plan_" + symbol.helperKind + "__" + type;
    if (symbol.step)
    {
        helper += "__" + std::to_string(*symbol.step);
    }
    if (symbol.direction != PlanHelperDirection::None)
    {
        helper += (symbol.direction == PlanHelperDirection::Serialize) ? "__ser" : "__deser";
    }
    return helper;
}

llvm::StringMap<std::string> declareHelperNames(const Language                  language,
                                                const llvm::ArrayRef<BodyParts> bodies,
                                                NamingScope&                    scope,
                                                const llvm::StringRef           qualifier)
{
    llvm::StringMap<std::string> names;
    for (const BodyParts& body : bodies)
    {
        if ((body.plan.function != PlanFunction::Helper) || body.unreferenced)
        {
            continue;
        }
        // Keyed by the symbol, so two helpers whose names compose alike are two claims and the later
        // one takes an ordinal. The marker goes on after the scope has allocated the name, because
        // the projection a scope applies folds a leading underscore away. Every helper takes the same
        // marker, so two that the scope kept apart stay apart.
        const std::string declared = scope.declare(IdentifierRole::InternalFunctionName,
                                                   body.symbol,
                                                   renderScopeLocalHelperName(language, body.plan, qualifier));
        names[body.symbol] =
            (languageTraits(language).classification.internalLinkage == InternalLinkage::UnderscorePrefix)
                ? "_" + declared
                : declared;
    }
    return names;
}

}  // namespace llvmdsdl
