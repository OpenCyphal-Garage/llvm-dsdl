//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the naming manifest: the map from DSDL names to generated identifiers.
///
/// Everything here is derived from the same engine and the same section scopes the emitters use, so
/// the manifest reports what a backend writes rather than a second opinion about it.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NamingManifest.h"

#include "llvmdsdl/CodeGen/DefinitionPathProjection.h"
#include "llvmdsdl/CodeGen/SectionNaming.h"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <sstream>

namespace llvmdsdl
{
namespace
{

/// @brief Renders one section's attribute names.
llvm::json::Object renderSection(const CodegenNamingLanguage language, const SemanticSection& section)
{
    const NamingScope fieldScope = makeSectionFieldScope(language, section);
    const NamingScope constScope = makeSectionConstantScope(language, section);

    llvm::json::Object fields;
    for (const auto& field : section.fields)
    {
        if (!field.isPadding)
        {
            fields[field.name] = fieldScope.get(IdentifierRole::FieldName, field.name);
        }
    }
    llvm::json::Object constants;
    for (const auto& constant : section.constants)
    {
        constants[constant.name] = constScope.get(IdentifierRole::ConstantName, constant.name);
    }

    llvm::json::Object out;
    out["fields"]    = std::move(fields);
    out["constants"] = std::move(constants);
    return out;
}

/// @brief True when @p language's generated type symbol is the shared short-name projection.
///
/// Go, TypeScript and Python name the type after the short name and version. C, C++ and Rust build a
/// namespace-qualified symbol in their own emitters (`mangleSymbol`, `cppTypeName`, `rustTypeName`),
/// for which the shared projection is only part of the answer, so the manifest omits the key.
bool typeSymbolIsSharedProjection(const CodegenNamingLanguage language)
{
    return language == CodegenNamingLanguage::Go || language == CodegenNamingLanguage::TypeScript ||
           language == CodegenNamingLanguage::Python;
}

/// @brief Renders one definition under one language.
llvm::json::Object renderDefinition(const CodegenNamingLanguage language,
                                    const SemanticDefinition&   def,
                                    const TypeNameVersioning    typeNameVersioning)
{
    llvm::json::Array namespaceParts;
    for (const auto& component : def.info.namespaceComponents)
    {
        namespaceParts.push_back(codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component));
    }

    llvm::json::Object out;
    if (typeSymbolIsSharedProjection(language))
    {
        out["type_name"] = renderDefinitionTypeName(language,
                                                    def.info.namespaceComponents,
                                                    def.info.shortName,
                                                    def.info.majorVersion,
                                                    def.info.minorVersion,
                                                    typeNameVersioning);
    }
    // Exact for every backend: C and C++ name headers after the raw short name, which is what the
    // FileStem role returns for them, and the other four fold it -- both go through this one call.
    out["file_stem"] =
        renderVersionedFileStem(language, def.info.shortName, def.info.majorVersion, def.info.minorVersion);
    out["namespace"] = std::move(namespaceParts);
    if (def.isService)
    {
        out["request"] = renderSection(language, def.request);
        if (def.response.has_value())
        {
            out["response"] = renderSection(language, *def.response);
        }
    }
    else
    {
        out["message"] = renderSection(language, def.request);
    }
    return out;
}

}  // namespace

std::string renderNamingManifest(const SemanticModule&                semantic,
                                 const llvm::ArrayRef<OutputLanguage> languages,
                                 const llvm::StringRef                toolVersion,
                                 const TypeNameVersioning             typeNameVersioning)
{
    llvm::json::Object root;
    root["version"]              = 1;
    root["tool"]                 = toolVersion.str();
    root["type_name_versioning"] = (typeNameVersioning == TypeNameVersioning::Versioned) ? "versioned" : "unversioned";

    llvm::json::Object byLanguage;
    for (const auto& [language, languageName] : languages)
    {
        llvm::json::Object byType;
        for (const auto& def : semantic.definitions)
        {
            byType[def.info.fullName + "." + std::to_string(def.info.majorVersion) + "." +
                   std::to_string(def.info.minorVersion)] = renderDefinition(language, def, typeNameVersioning);
        }
        byLanguage[languageName.str()] = std::move(byType);
    }
    root["languages"] = std::move(byLanguage);

    std::string              text;
    llvm::raw_string_ostream stream(text);
    stream << llvm::formatv("{0:2}", llvm::json::Value(std::move(root))) << "\n";
    return stream.str();
}

}  // namespace llvmdsdl
