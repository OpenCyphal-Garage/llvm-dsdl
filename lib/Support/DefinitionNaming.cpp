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

namespace llvmdsdl
{

const DefinitionNamePolicy& definitionNamePolicy(const CodegenNamingLanguage language)
{
    // C flattens the namespace into the identifier because it has nowhere else to put it. Rust does
    // the same and then re-checks the result. The other four let the language carry the namespace:
    // C++ in a real namespace, Go/TypeScript/Python in a per-namespace module.
    static constexpr DefinitionNamePolicy kC{"__", TypeNameVersioning::Never, false};
    static constexpr DefinitionNamePolicy kCpp{"", TypeNameVersioning::OnlyWhenAmbiguous, false};
    static constexpr DefinitionNamePolicy kRust{"_", TypeNameVersioning::Always, true};
    static constexpr DefinitionNamePolicy kModuleScoped{"", TypeNameVersioning::Always, false};

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
                                     const bool                        shortNameIsAmbiguous)
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

    const bool versioned = (policy.versioning == TypeNameVersioning::Always) ||
                           ((policy.versioning == TypeNameVersioning::OnlyWhenAmbiguous) && shortNameIsAmbiguous);
    if (versioned)
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
