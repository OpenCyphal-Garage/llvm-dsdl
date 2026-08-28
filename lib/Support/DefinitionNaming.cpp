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
#include "llvmdsdl/Support/NamingPolicy.h"
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <utility>

namespace llvmdsdl
{

const DefinitionNamePolicy& definitionNamePolicy(const CodegenNamingLanguage language)
{
    // C flattens the namespace into the identifier because it has nowhere else to put it. Rust does
    // the same and then re-checks the result. The other four let the language carry the namespace:
    // C++ in a real namespace, Go/TypeScript/Python in a per-namespace module.
    static constexpr DefinitionNamePolicy kC{"__", false};
    static constexpr DefinitionNamePolicy kCpp{"", false};
    static constexpr DefinitionNamePolicy kRust{"_", true};
    static constexpr DefinitionNamePolicy kModuleScoped{"", false};

    switch (language)
    {
    case CodegenNamingLanguage::C:
        return kC;
    case CodegenNamingLanguage::Cpp:
        return kCpp;
    case CodegenNamingLanguage::Rust:
        return kRust;
    case CodegenNamingLanguage::Go:
    case CodegenNamingLanguage::TypeScript:
    case CodegenNamingLanguage::Python:
        return kModuleScoped;
    }
    return kModuleScoped;
}

std::string renderDefinitionTypeName(const CodegenNamingLanguage       language,
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

    if (versioning == TypeNameVersioning::Versioned)
    {
        out += "_" + std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
    }

    if (policy.reprojectComposed)
    {
        out = codegenProjectIdentifier(language, IdentifierRole::TypeName, out);
    }
    return out;
}

std::string renderDefinitionFileStem(const CodegenNamingLanguage language,
                                     const llvm::StringRef       shortName,
                                     const std::uint32_t         majorVersion,
                                     const std::uint32_t         minorVersion)
{
    return codegenProjectIdentifier(language, IdentifierRole::FileStem, shortName) + "_" +
           std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
}

std::string renderIncludeGuard(const CodegenNamingLanguage language,
                               const llvm::StringRef       prefix,
                               const llvm::StringRef       fullName,
                               const std::uint32_t         majorVersion,
                               const std::uint32_t         minorVersion,
                               const llvm::StringRef       suffix)
{
    const std::string composed = prefix.str() + fullName.str() + "_" + std::to_string(majorVersion) + "_" +
                                 std::to_string(minorVersion) + suffix.str();
    return codegenProjectIdentifier(language, IdentifierRole::MacroName, composed);
}

std::pair<std::string, std::string> renderVersionSentinelMacros(const CodegenNamingLanguage language,
                                                                const llvm::StringRef       fullName,
                                                                const std::uint32_t         majorVersion,
                                                                const std::uint32_t         minorVersion)
{
    // The generic one carries no version -- that is the whole point of it.
    const std::string generic =
        codegenProjectIdentifier(language, IdentifierRole::MacroName, "LLVMDSDL_SELECTED_" + fullName.str() + "_");
    const std::string specific =
        renderIncludeGuard(language, "LLVMDSDL_SELECTED_", fullName, majorVersion, minorVersion, "_");
    return {generic, specific};
}

std::string renderDefinitionSymbolBase(const llvm::StringRef fullName,
                                       const std::uint32_t   majorVersion,
                                       const std::uint32_t   minorVersion)
{
    std::string out = fullName.str();
    for (char& c : out)
    {
        if (c == '.')
        {
            c = '_';
        }
    }
    return out + "_" + std::to_string(majorVersion) + "_" + std::to_string(minorVersion);
}

std::string renderSectionTypeSuffix(const CodegenNamingLanguage language, const llvm::StringRef sectionName)
{
    if ((sectionName != "request") && (sectionName != "response"))
    {
        return "";
    }
    const llvm::StringRef separator = (language == CodegenNamingLanguage::C) ? "__" : "_";
    return separator.str() + ((sectionName == "request") ? "Request" : "Response");
}

std::string renderSectionSymbolSuffix(const llvm::StringRef sectionName)
{
    if (sectionName == "request")
    {
        return "__request";
    }
    if (sectionName == "response")
    {
        return "__response";
    }
    return "";
}

}  // namespace llvmdsdl
