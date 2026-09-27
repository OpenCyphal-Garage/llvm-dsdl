//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the check for a definition that meets a name another definition generates.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/GeneratedNameCollisions.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/PlanSymbol.h"

namespace llvmdsdl
{
namespace
{

/// @brief What claimed a name, for the diagnostic.
struct Claim final
{
    /// @brief Full DSDL name of the definition, or of the namespace.
    std::string fullName;

    /// @brief Section that claimed it: `request`, `response`, or empty.
    std::string section;

    /// @brief Source file, so the diagnostic points at something the user can open.
    std::string filePath;

    /// @brief For a namespace, the full name of a definition it holds; empty otherwise.
    std::string heldBy;

    /// @brief Whether the backend composes the name beside the type, rather than naming a type.
    bool generated{};
};

std::string describe(const Claim& claim)
{
    if (!claim.heldBy.empty())
    {
        return "namespace '" + claim.fullName + "', which holds '" + claim.heldBy + "'";
    }
    const std::string owner = claim.section.empty() ? "'" + claim.fullName + "'"
                                                    : "the " + claim.section + " section of '" + claim.fullName + "'";
    return claim.generated ? "a name generated for " + owner : owner;
}

/// @brief Calls @p claim with every name @p language declares beside one section's type.
///
/// A body compiled apart from its entry point is linked under the versioned type name whatever the
/// type name is, so @p versionedTypeName names those.
void forEachGeneratedName(const LanguageTraits&                              language,
                          const SemanticSection&                             section,
                          const std::string&                                 typeName,
                          const std::string&                                 versionedTypeName,
                          const llvm::function_ref<void(const std::string&)> claim)
{
    const Composition&       composition = language.composition;
    const FreeFunctionNames& free        = composition.freeFunctions;

    if (composition.constants == ConstantsScope::Enclosing)
    {
        for (const llvm::StringRef token : codegenGeneratedConstantTokens())
        {
            claim(renderEnclosedConstantName(typeName, token.str() + composition.generatedConstantSuffix.str()));
        }
        for (const std::string& name : makeSectionConstantScope(language.language, section, {}).assigned())
        {
            claim(renderEnclosedConstantName(typeName, name));
        }
    }
    if (composition.constants == ConstantsScope::Package)
    {
        for (const std::string& name : makeGoConstantScope(section, typeName).assigned())
        {
            claim(name);
        }
    }

    if (!free.entryPointJoin.empty())
    {
        claim(renderEntryPointName(language.language, typeName, EntryPoint::Serialize));
        claim(renderEntryPointName(language.language, typeName, EntryPoint::Deserialize));
        if (free.initializer)
        {
            claim(renderEntryPointName(language.language, typeName, EntryPoint::Initialize));
        }
    }
    if (!free.loweredBodySuffix.empty())
    {
        claim(renderLoweredEntryPointName(language.language, versionedTypeName, EntryPoint::Serialize));
        claim(renderLoweredEntryPointName(language.language, versionedTypeName, EntryPoint::Deserialize));
        claim(renderLoweredEntryPointName(language.language, versionedTypeName, EntryPoint::Initialize));
        // A lowered accessor is named by the member as the plan names it.
        for (const auto& field : section.fields)
        {
            if (!field.isPadding)
            {
                claim(renderLoweredAccessorName(language.language, versionedTypeName, AccessorVerb::Get, field.name));
                claim(renderLoweredAccessorName(language.language, versionedTypeName, AccessorVerb::Set, field.name));
            }
        }
        if (section.isUnion)
        {
            claim(renderLoweredAccessorName(language.language,
                                            versionedTypeName,
                                            AccessorVerb::Get,
                                            kPlanUnionTagMember));
            claim(renderLoweredAccessorName(language.language,
                                            versionedTypeName,
                                            AccessorVerb::Set,
                                            kPlanUnionTagMember));
        }
    }

    if ((free.accessors == AccessorNaming::None) && !free.unionOptionFunctions)
    {
        return;
    }
    const NamingScope        fields = makeSectionFieldScope(language.language, section);
    std::vector<std::string> members;
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            members.push_back(fields.get(IdentifierRole::FieldName, field.name));
        }
    }
    if (free.unionOptionFunctions && section.isUnion)
    {
        for (const std::string& member : members)
        {
            claim(renderAccessorName(language.language, typeName, AccessorVerb::Is, member));
            claim(renderAccessorName(language.language, typeName, AccessorVerb::Select, member));
        }
    }
    if (free.accessors != AccessorNaming::None)
    {
        if (section.isUnion)
        {
            members.push_back(unionTagMemberName(language.language).str());
        }
        for (const std::string& member : members)
        {
            claim(renderAccessorName(language.language, typeName, AccessorVerb::Get, member));
            claim(renderAccessorName(language.language, typeName, AccessorVerb::Set, member));
        }
    }
}

/// @brief Calls @p claim with every name @p language declares beside a service's own name.
void forEachServiceName(const LanguageTraits&                              language,
                        const std::string&                                 baseTypeName,
                        const llvm::function_ref<void(const std::string&)> claim)
{
    const Composition& composition = language.composition;
    for (const llvm::StringRef token : codegenGeneratedConstantTokens())
    {
        claim((composition.constants == ConstantsScope::Package)
                  ? goConstantName({baseTypeName, token})
                  : renderEnclosedConstantName(baseTypeName, token.str() + composition.generatedConstantSuffix.str()));
    }
    const FreeFunctionNames& free = composition.freeFunctions;
    if (!free.entryPointJoin.empty())
    {
        claim(renderEntryPointName(language.language, baseTypeName, EntryPoint::Serialize));
        claim(renderEntryPointName(language.language, baseTypeName, EntryPoint::Deserialize));
        if (free.initializer)
        {
            claim(renderEntryPointName(language.language, baseTypeName, EntryPoint::Initialize));
        }
    }
}

}  // namespace

void checkGeneratedNameCollisions(const SemanticModule&                module,
                                  const llvm::ArrayRef<LanguageTraits> outputLanguages,
                                  const TypeNameVersioning             versioning,
                                  DiagnosticEngine&                    diagnostics)
{
    std::map<std::string, Claim> claimed;
    std::set<std::string>        reported;

    const auto record =
        [&](const LanguageTraits& language, const std::string& scope, const std::string& name, const Claim& claim) {
            const std::string key     = std::string(language.name) + ":" + scope + ":" + name;
            const auto [it, inserted] = claimed.emplace(key, claim);
            if (inserted)
            {
                return;
            }
            const Claim& earlier = it->second;
            // A pair of type names or namespaces is the scoped check's, and two versions of one
            // definition are the same DSDL type and D20's business.
            if ((!earlier.generated && !claim.generated) ||
                (earlier.heldBy.empty() && claim.heldBy.empty() && (earlier.fullName == claim.fullName)))
            {
                return;
            }
            if (!reported.insert(key).second)
            {
                return;
            }
            // Named type first: it is the name a user chose, and the diagnostic is placed at its file.
            const Claim& first  = claim.generated ? earlier : claim;
            const Claim& second = claim.generated ? claim : earlier;
            diagnostics.error({first.filePath, 1, 1},
                              "name collision in generated output: " + describe(first) + " and " + describe(second) +
                                  (second.heldBy.empty() ? "" : ",") + " both emit '" + name +
                                  "' for target language '" + std::string(language.name) + "'; rename one of them");
        };

    for (const auto& def : module.definitions)
    {
        const auto& info = def.info;
        for (const auto& language : outputLanguages)
        {
            if (!language.composition.definitionsShareNamespaceScope)
            {
                continue;
            }
            for (const ScopedTypeName& type :
                 scopedTypeNames(language, info, def.isService, def.request.deprecated, versioning))
            {
                const bool namespaceName = !type.namespaceName.empty();
                record(language,
                       type.scope,
                       type.name,
                       Claim{namespaceName ? type.namespaceName : info.fullName,
                             type.section,
                             info.filePath,
                             namespaceName ? info.fullName : "",
                             false});
            }

            const std::string scope    = sharedScopeOf(language, info);
            const auto        claimFor = [&](const std::string& section) {
                return [&, section](const std::string& name) {
                    record(language, scope, name, Claim{info.fullName, section, info.filePath, "", true});
                };
            };
            const auto renderBase = [&](const TypeNameVersioning scheme) {
                return renderDefinitionTypeName(language.language,
                                                info.namespaceComponents,
                                                info.shortName,
                                                info.majorVersion,
                                                info.minorVersion,
                                                scheme);
            };
            const std::string base          = renderBase(versioning);
            const std::string versionedBase = renderBase(TypeNameVersioning::Versioned);
            if (!def.isService)
            {
                forEachGeneratedName(language, def.request, base, versionedBase, claimFor(""));
                continue;
            }
            forEachServiceName(language, base, claimFor(""));
            for (const llvm::StringRef section : {llvm::StringRef("request"), llvm::StringRef("response")})
            {
                const SemanticSection* held = &def.request;
                if (section == "response")
                {
                    held = def.response ? &*def.response : nullptr;
                }
                if (held != nullptr)
                {
                    forEachGeneratedName(language,
                                         *held,
                                         renderSectionTypeName(language.language, base, section),
                                         renderSectionTypeName(language.language, versionedBase, section),
                                         claimFor(section.str()));
                }
            }
        }
    }
}

}  // namespace llvmdsdl
