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
/// The manifest renders each language's surface plan, which is the allocation the emitters read, so
/// it reports what a backend writes rather than a second opinion about it.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NamingManifest.h"

#include "llvmdsdl/CodeGen/SectionNaming.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FormatVariadic.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

std::optional<llvm::StringLiteral> typeNameKey(const Language language)
{
    const DefinitionNamePolicy& policy = definitionNamePolicy(language);
    if (policy.typeNameReachesTheType)
    {
        return llvm::StringLiteral{"type_name"};
    }
    if (!policy.namespaceJoin.empty())
    {
        return llvm::StringLiteral{"qualified_type_name"};
    }
    return std::nullopt;
}

/// @brief Renders one section's attribute names.
///
/// @param[in] plan The language's plan.
/// @param[in] section Where the section's names are in @p plan.
/// @param[in] typeNameKey The key the section's type name is reported under, or nothing where it is
///            not reported. Reported for each section because it does not follow from the
///            definition's own: Rust reaches a section through the definition's module, so the name
///            is the section word alone and a consumer cannot derive it from the type name.
llvm::json::Object renderSection(const SurfacePlan&                       plan,
                                 const SectionNames&                      section,
                                 const std::optional<llvm::StringLiteral> typeNameKey)
{
    llvm::json::Object fields;
    for (const auto& field : section.fields)
    {
        fields[field.getKey().str()] = plan.decls[field.getValue()].name;
    }
    llvm::json::Object constants;
    for (const auto& constant : section.constants)
    {
        constants[constant.getKey().str()] = plan.decls[constant.getValue()].name;
    }

    llvm::json::Object out;
    if (typeNameKey)
    {
        out[*typeNameKey] = section.typeName;
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
        for (const auto& entry : section.options)
        {
            llvm::json::Object option;
            option["name"]                = plan.decls[entry.getValue().decl].name;
            option["tag"]                 = static_cast<std::int64_t>(entry.getValue().tag);
            options[entry.getKey().str()] = std::move(option);
        }
        out["union_options"] = std::move(options);
    }
    return out;
}

/// @brief Renders one definition under one language.
llvm::json::Object renderDefinition(const Language language, const SurfacePlan& plan, const DefinitionNames& definition)
{
    const std::optional<llvm::StringLiteral> reportType = typeNameKey(language);

    llvm::json::Object out;
    if (reportType)
    {
        out[*reportType] = definition.typeName;
    }
    out["file_stem"] = definition.fileStem;
    llvm::json::Array namespaceParts;
    for (const std::string& component : definition.namespaceNames)
    {
        namespaceParts.push_back(component);
    }
    out["namespace"] = std::move(namespaceParts);
    if (definition.fixedPortId)
    {
        out["fixed_port_id"] = static_cast<std::int64_t>(*definition.fixedPortId);
    }
    for (const SectionNames& section : definition.sections)
    {
        out[section.section.empty() ? std::string("message") : section.section] =
            renderSection(plan, section, reportType);
    }
    return out;
}

}  // namespace

std::string renderNamingManifest(const SemanticModule&                semantic,
                                 const llvm::ArrayRef<LanguageTraits> languages,
                                 const llvm::StringRef                toolVersion,
                                 const TypeNameVersioning             typeNameVersioning)
{
    llvm::json::Object root;
    root["version"]              = 1;
    root["tool"]                 = toolVersion.str();
    root["type_name_versioning"] = (typeNameVersioning == TypeNameVersioning::Versioned) ? "versioned" : "unversioned";

    std::vector<DefinitionParts> definitions;
    definitions.reserve(semantic.definitions.size());
    for (const SemanticDefinition& definition : semantic.definitions)
    {
        definitions.push_back(definitionParts(definition));
    }

    llvm::json::Object byLanguage;
    for (const LanguageTraits& row : languages)
    {
        const SurfacePlan  plan = allocateSurface(row,
                                                  definitions,
                                                  SurfaceOptions{.packageName   = {},
                                                                 .versioning    = typeNameVersioning,
                                                                 .accessorsOnly = false,
                                                                 .profile       = {}});
        llvm::json::Object byType;
        for (const DefinitionNames& definition : plan.definitions)
        {
            byType[definition.key] = renderDefinition(row.language, plan, definition);
        }
        byLanguage[row.name.str()] = std::move(byType);
    }
    root["languages"] = std::move(byLanguage);

    std::string              text;
    llvm::raw_string_ostream stream(text);
    stream << llvm::formatv("{0:2}", llvm::json::Value(std::move(root))) << "\n";
    return stream.str();
}

}  // namespace llvmdsdl
