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
#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/NamingPolicy.h"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/FormatVariadic.h>
#include <cstdint>
#include <string>
#include <utility>

namespace llvmdsdl
{
namespace
{

/// @brief Renders one section's attribute names.
///
/// @param[in] language Naming language.
/// @param[in] section The section being reported.
/// @param[in] sectionTypeName The generated type name the section's constants are prefixed with,
///            which decides whether they are in reach of the module's own names. The emitters build
///            their scope from it, so the manifest has to as well or it reports a name that is not
///            the one written.
/// @param[in] reportTypeName Whether the section's type name is the shared projection, and so is
///            the whole answer rather than part of one. Reported because it does not follow from
///            the definition's own: Rust reaches a section through the definition's module, so the
///            name is the section word alone and a consumer cannot derive it from the type name.
llvm::json::Object renderSection(const CodegenNamingLanguage language,
                                 const SemanticSection&      section,
                                 const std::string&          sectionTypeName,
                                 const bool                  reportTypeName)
{
    const NamingScope fieldScope = makeSectionFieldScope(language, section);

    // Go's constants carry the type they belong to, so the name is one identifier rather than a
    // prefix a consumer joins to a token, and it is built by the scope the emitter builds.
    const bool        goLike = language == CodegenNamingLanguage::Go;
    const NamingScope constScope =
        goLike ? makeGoConstantScope(section, sectionTypeName)
               : makeSectionConstantScope(
                     language,
                     section,
                     codegenProjectIdentifier(language, IdentifierRole::ConstantName, sectionTypeName));

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
        constants[constant.name] =
            goLike ? constScope.get(IdentifierRole::ConstantName, goConstantKey({sectionTypeName, constant.name}))
                   : constScope.get(IdentifierRole::ConstantName, constant.name);
    }

    llvm::json::Object out;
    if (reportTypeName)
    {
        out["type_name"] = sectionTypeName;
    }
    out["fields"]    = std::move(fields);
    out["constants"] = std::move(constants);

    // A union option's tag is the one fact about it a caller cannot read off the type: the name maps
    // to a member, and the member says nothing about which tag value selects it. The manifest is
    // where a build integration reads a generated name without reimplementing the projection, so it
    // is where the tag belongs too.
    if (section.isUnion)
    {
        llvm::json::Object options;
        for (const auto& field : section.fields)
        {
            if (field.isPadding)
            {
                continue;
            }
            llvm::json::Object option;
            option["name"] =
                goLike ? constScope.get(IdentifierRole::ConstantName,
                                        goConstantKey({sectionTypeName, field.name, "OPTION_TAG"}))
                       : constScope.get(IdentifierRole::MacroName, unionOptionTagName(language, field.name));
            option["tag"]       = static_cast<std::int64_t>(field.unionOptionIndex);
            options[field.name] = std::move(option);
        }
        out["union_options"] = std::move(options);
    }
    return out;
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

    // The same name the emitters prefix a section's constants with. Reported only for the three
    // languages whose type symbol is this projection, but needed for the scope in every language.
    const std::string typeName = renderDefinitionTypeName(language,
                                                          def.info.namespaceComponents,
                                                          def.info.shortName,
                                                          def.info.majorVersion,
                                                          def.info.minorVersion,
                                                          typeNameVersioning);

    const bool reportType = definitionNamePolicy(language).typeNameReachesTheType;

    llvm::json::Object out;
    if (reportType)
    {
        out["type_name"] = typeName;
    }
    // Exact for every backend: the FileStem role returns the raw short name for C and C++ and the
    // folded one for the other four -- both go through this one call.
    out["file_stem"] =
        renderVersionedFileStem(language, def.info.shortName, def.info.majorVersion, def.info.minorVersion);
    out["namespace"] = std::move(namespaceParts);
    if (def.info.fixedPortId)
    {
        out["fixed_port_id"] = static_cast<std::int64_t>(*def.info.fixedPortId);
    }
    if (def.isService)
    {
        out["request"] =
            renderSection(language, def.request, renderSectionTypeName(language, typeName, "request"), reportType);
        if (def.response.has_value())
        {
            out["response"] = renderSection(language,
                                            *def.response,
                                            renderSectionTypeName(language, typeName, "response"),
                                            reportType);
        }
    }
    else
    {
        out["message"] = renderSection(language, def.request, typeName, reportType);
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
