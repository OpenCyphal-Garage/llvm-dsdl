//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements names composed from a definition's identity.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <cassert>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <utility>

namespace llvmdsdl
{

const DefinitionNamePolicy& definitionNamePolicy(const Language language)
{
    return languageTraits(language).composition.definitionName;
}

std::string renderDefinitionTypeName(const Language                    language,
                                     const llvm::ArrayRef<std::string> namespaceComponents,
                                     const llvm::StringRef             shortName,
                                     const std::uint32_t               majorVersion,
                                     const std::uint32_t               minorVersion,
                                     const TypeNameVersioning          versioning)
{
    const DefinitionNamePolicy& policy = definitionNamePolicy(language);

    std::string out;
    if (!policy.namespaceJoin.empty())
    {
        for (const auto& component : namespaceComponents)
        {
            if (!out.empty())
            {
                out += policy.namespaceJoin;
            }
            out += codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component);
        }
        if (!out.empty())
        {
            out += policy.namespaceJoin;
        }
    }
    out += codegenProjectIdentifier(language, IdentifierRole::TypeName, shortName);

    if ((versioning == TypeNameVersioning::Versioned) && policy.versionInTypeName)
    {
        out += "_" + std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
    }
    return out;
}

std::string renderDefinitionFileStem(const Language        language,
                                     const llvm::StringRef shortName,
                                     const std::uint32_t   majorVersion,
                                     const std::uint32_t   minorVersion)
{
    // Projected as one name rather than a projected short name with the version appended. A short
    // name that strops -- `Break`, which reaches Rust's keyword `break` -- gains a trailing `_`,
    // and the separator after it made the `__` that `non_snake_case` reports in a module name. The
    // composed name carries the version, so it is not the keyword and needs no escape.
    //
    // The two orders differ wherever the projected short name would end in an underscore, which is
    // stropping and also a DSDL name that ends in one: `Break_` reaches `break_1_0` here and
    // `break__1_0` the other way round. That is the same fold, and it is the point -- a stem is one
    // identifier, so its separators normalise once. Two short names that fold onto one stem are a
    // collision, which `Discovery` composes the same name to find.
    const std::string composed =
        shortName.str() + "_" + std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
    return codegenProjectIdentifier(language, IdentifierRole::FileStem, composed);
}

namespace
{

/// @brief The suffix a language that scopes both sections under the service's name appends.
///
/// Go, TypeScript and Python join the two parts with nothing between them: each of them writes a
/// type name in PascalCase, and an underscore inside one is what `ST1003` and `N801` report and
/// what `naming-convention` rejects. C separates with `__`, which is the separator it flattens a
/// whole namespace with; C++ keeps the single underscore it has always had.
std::string renderSectionTypeSuffix(const Language language, const llvm::StringRef sectionName)
{
    if ((sectionName != "request") && (sectionName != "response"))
    {
        return "";
    }
    return languageTraits(language).composition.sectionJoin.str() +
           ((sectionName == "request") ? "Request" : "Response");
}

}  // namespace

std::string renderSectionTypeName(const Language        language,
                                  const llvm::StringRef baseTypeName,
                                  const llvm::StringRef sectionName)
{
    if ((sectionName != "request") && (sectionName != "response"))
    {
        return baseTypeName.str();
    }
    if (languageTraits(language).composition.sectionNamedAlone)
    {
        return codegenProjectIdentifier(language, IdentifierRole::TypeName, sectionName);
    }
    return baseTypeName.str() + renderSectionTypeSuffix(language, sectionName);
}

namespace
{

llvm::StringRef entryPointVerb(const EntryPoint entryPoint)
{
    switch (entryPoint)
    {
    case EntryPoint::Serialize:
        return "serialize";
    case EntryPoint::Deserialize:
        return "deserialize";
    case EntryPoint::Initialize:
        return "initialize";
    }
    return "";
}

/// @brief The verb as a joined name spells it, then as a concatenated one does.
std::pair<llvm::StringRef, llvm::StringRef> accessorVerb(const AccessorVerb verb)
{
    switch (verb)
    {
    case AccessorVerb::Get:
        return {"get", "Get"};
    case AccessorVerb::Set:
        return {"set", "Set"};
    case AccessorVerb::Is:
        return {"is", "Is"};
    case AccessorVerb::Select:
        return {"select", "Select"};
    }
    return {};
}

}  // namespace

std::string renderEntryPointName(const Language language, const llvm::StringRef typeName, const EntryPoint entryPoint)
{
    const llvm::StringRef join = languageTraits(language).composition.freeFunctions.entryPointJoin;
    assert(!join.empty() && "the language's entry points are not free functions");
    return typeName.str() + join.str() + entryPointVerb(entryPoint).str() + "_";
}

std::string renderAccessorName(const Language        language,
                               const llvm::StringRef typeName,
                               const AccessorVerb    verb,
                               const llvm::StringRef member)
{
    const FreeFunctionNames& names    = languageTraits(language).composition.freeFunctions;
    const auto [joined, concatenated] = accessorVerb(verb);
    switch (names.accessors)
    {
    case AccessorNaming::Joined:
        return typeName.str() + names.entryPointJoin.str() + joined.str() + "_" + member.str() + "_";
    case AccessorNaming::Concatenated:
        return typeName.str() + concatenated.str() + member.str();
    case AccessorNaming::None:
        break;
    }
    assert(false && "the language's accessors are not free functions");
    return "";
}

std::string renderLoweredEntryPointName(const Language        language,
                                        const llvm::StringRef versionedTypeName,
                                        const EntryPoint      entryPoint)
{
    const llvm::StringRef suffix = languageTraits(language).composition.freeFunctions.loweredBodySuffix;
    assert(!suffix.empty() && "the language's bodies are not compiled apart from its entry points");
    return renderEntryPointName(language, versionedTypeName, entryPoint) + suffix.str();
}

std::string renderLoweredAccessorName(const Language        language,
                                      const llvm::StringRef versionedTypeName,
                                      const AccessorVerb    verb,
                                      const llvm::StringRef member)
{
    const llvm::StringRef suffix = languageTraits(language).composition.freeFunctions.loweredBodySuffix;
    assert(!suffix.empty() && "the language's bodies are not compiled apart from its accessors");
    return renderAccessorName(language, versionedTypeName, verb, member) + suffix.str();
}

std::string renderEnclosedConstantName(const llvm::StringRef typeName, const llvm::StringRef constant)
{
    return typeName.str() + "_" + constant.str();
}

std::string renderCTagSpelling(const llvm::StringRef typeName)
{
    return "struct " + typeName.str();
}

std::string renderDeclaredTypeName(const llvm::StringRef typeName, const bool deprecated)
{
    return deprecated ? typeName.str() + "_" : typeName.str();
}

}  // namespace llvmdsdl
