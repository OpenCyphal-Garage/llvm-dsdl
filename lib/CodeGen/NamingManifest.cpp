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
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/SurfaceTree.h"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FormatVariadic.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

/// @brief @p name, qualified by @p holder and each namespace, module, package and type enclosing it,
///        as @p row joins a qualifier to the name it qualifies.
///
/// A file scope declares into the namespace around it, and the root is the package rather than a
/// scope a name is qualified by, so neither qualifies. A type encloses another only where a
/// definition's own type encloses its sections, and it qualifies them by its public name, the name a
/// type declared apart is published under.
std::string qualifiedName(const LanguageTraits&            row,
                          const SurfacePlan&               plan,
                          const DefinitionNames&           definition,
                          const std::optional<std::size_t> holder,
                          const llvm::StringRef            name)
{
    std::vector<std::string> parts{name.str()};
    for (std::optional<std::size_t> at = holder; at; at = plan.scopes[*at].parent)
    {
        const SurfaceScope& scope = plan.scopes[*at];
        switch (scope.kind)
        {
        case SurfaceScopeKind::Root:
        case SurfaceScopeKind::File:
            break;
        case SurfaceScopeKind::Type:
            parts.push_back(definition.typeName);
            break;
        case SurfaceScopeKind::Namespace:
        case SurfaceScopeKind::Module:
        case SurfaceScopeKind::Package:
            parts.push_back(scope.name);
            break;
        }
    }
    std::ranges::reverse(parts);
    return llvm::join(parts, row.classification.lookup.separator);
}

/// @brief Renders one section's attribute names.
///
/// @param[in] plan The language's plan.
/// @param[in] section Where the section's names are in @p plan.
/// @param[in] typeNameKey The key the section's type name is reported under.
/// @param[in] typeName The section's type name, as it is reported. Reported for each section because
///            it does not follow from the definition's own: Rust reaches a section through the
///            definition's module, so the name is the section word alone and a consumer cannot
///            derive it from the type name.
llvm::json::Object renderSection(const SurfacePlan&        plan,
                                 const SectionNames&       section,
                                 const llvm::StringLiteral typeNameKey,
                                 std::string               typeName)
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
    out[typeNameKey] = std::move(typeName);
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
llvm::json::Object renderDefinition(const LanguageTraits&  row,
                                    const SurfacePlan&     plan,
                                    const DefinitionNames& definition)
{
    // A type's name is reported as `type_name` where a consumer reaches the type from the name and the
    // namespace, and otherwise as `qualified_type_name`, qualified by every scope that encloses it.
    const bool                qualified = !row.composition.definitionName.typeNameReachesTheType;
    const llvm::StringLiteral key =
        qualified ? llvm::StringLiteral{"qualified_type_name"} : llvm::StringLiteral{"type_name"};
    const auto reported = [&](const std::optional<std::size_t> holder, const llvm::StringRef name) {
        return qualified ? qualifiedName(row, plan, definition, holder, name) : name.str();
    };
    // The definition's type is declared where its sections are, or encloses them where the language
    // declares a type that does.
    std::optional<std::size_t> holder = plan.scopes[definition.sections.front().typeScope].parent;
    if (holder && (plan.scopes[*holder].kind == SurfaceScopeKind::Type))
    {
        holder = plan.scopes[*holder].parent;
    }

    llvm::json::Object out;
    out[key]         = reported(holder, definition.typeName);
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
            renderSection(plan, section, key, reported(plan.scopes[section.typeScope].parent, section.typeName));
    }
    return out;
}

/// @brief @p key as a key its object owns: an object keyed by a reference holds the reference.
llvm::json::ObjectKey owned(const llvm::StringRef key)
{
    return llvm::json::ObjectKey(key.str());
}

/// @brief The object @p parent holds under @p key, made empty where it holds none.
llvm::json::Object& child(llvm::json::Object& parent, const llvm::StringRef key)
{
    if (llvm::json::Object* const found = parent.getObject(key))
    {
        return *found;
    }
    parent[owned(key)] = llvm::json::Object{};
    return *parent.getObject(key);
}

/// @brief The key of a section's object: `message` for a message's.
std::string sectionKey(const llvm::StringRef section)
{
    return section.empty() ? std::string("message") : section.str();
}

/// @brief What a lowered function does, as the manifest keys it.
llvm::StringRef functionKey(const PlanFunction function)
{
    switch (function)
    {
    case PlanFunction::Serialize:
        return "serialize";
    case PlanFunction::Deserialize:
        return "deserialize";
    case PlanFunction::Initialize:
        return "initialize";
    case PlanFunction::AppendWireImage:
        return "append_wire_image";
    case PlanFunction::WireImage:
        return "wire_image";
    case PlanFunction::ReadWireImage:
        return "read_wire_image";
    case PlanFunction::FromWireImage:
        return "from_wire_image";
    case PlanFunction::Get:
        return "get";
    case PlanFunction::Set:
        return "set";
    case PlanFunction::Helper:
        break;
    }
    return "helper";
}

/// @brief Adds to @p entry every declaration the surface makes for the definition keyed @p key.
///
/// A section's names go in its object, and a service's own in the definition's; a message has one
/// section, so its names are the message's. A generated declaration is keyed by the fact it states,
/// and a lowered function's by what it does. A helper declared in a section's type is the section's,
/// and one declared in the file the definition's.
/// @param[in] service Whether the definition is a service.
void renderTree(llvm::json::Object& entry, const SurfacePlan& plan, const std::string& key, const bool service)
{
    const auto owner = [&](const llvm::StringRef section) -> llvm::json::Object& {
        return (section.empty() && service) ? entry : child(entry, sectionKey(section));
    };
    // The file each section's type is declared in, whose imports are the definition's.
    std::optional<std::size_t> file;
    for (const SurfaceScope& scope : plan.scopes)
    {
        if ((scope.kind != SurfaceScopeKind::Type) || !scope.of || (scope.of->schema != key))
        {
            continue;
        }
        owner(scope.of->section)["declared_type"] = scope.name;
        for (std::optional<std::size_t> at = scope.parent; at && !file; at = plan.scopes[*at].parent)
        {
            if (!plan.scopes[*at].path.empty())
            {
                file          = at;
                entry["file"] = plan.scopes[*at].path;
            }
        }
    }
    llvm::json::Array                        helpers;
    std::map<std::string, llvm::json::Array> sectionHelpers;
    for (const SurfaceDecl& decl : plan.decls)
    {
        const bool imported = file && (decl.scope == *file) && (decl.kind == SurfaceDeclKind::Import);
        if (!decl.of || ((decl.of->schema != key) && !imported))
        {
            continue;
        }
        const std::optional<PlanSymbol> function =
            decl.of->function.empty() ? std::nullopt : parsePlanSymbol(decl.of->function);
        const llvm::StringRef fact = decl.fact ? generatedFactName(*decl.fact) : llvm::StringRef{};
        llvm::json::Object&   into = owner(decl.of->section);
        switch (decl.kind)
        {
        case SurfaceDeclKind::Field:
        case SurfaceDeclKind::Constant:
            // A declaration of a DSDL field or constant is in the entry's `fields` or `constants`.
            if (!decl.fact)
            {
                break;
            }
            if (decl.of->member.empty())
            {
                child(into, "generated")[owned(fact)] = decl.name;
            }
            else
            {
                child(child(into, "generated"), fact)[owned(decl.of->member)] = decl.name;
            }
            break;
        case SurfaceDeclKind::Entry:
            if (function)
            {
                child(into, "entry_points")[owned(functionKey(function->function))] = decl.name;
            }
            break;
        case SurfaceDeclKind::Accessor:
            if (function)
            {
                child(child(into, "accessors"), function->member)[owned(functionKey(function->function))] = decl.name;
            }
            break;
        case SurfaceDeclKind::Wrapper:
            if (decl.fact)
            {
                child(into, "wrappers")[owned(fact)] = decl.name;
            }
            else if (function && !function->member.empty())
            {
                child(child(into, "wrappers"), functionKey(function->function))[owned(function->member)] = decl.name;
            }
            else if (function)
            {
                child(into, "wrappers")[owned(functionKey(function->function))] = decl.name;
            }
            break;
        case SurfaceDeclKind::Method:
            if (decl.fact)
            {
                child(child(into, "methods"), fact)[owned(decl.of->member)] = decl.name;
            }
            break;
        case SurfaceDeclKind::Helper:
            if (plan.scopes[decl.scope].kind == SurfaceScopeKind::Type)
            {
                sectionHelpers[decl.of->section].push_back(decl.name);
            }
            else
            {
                helpers.push_back(decl.name);
            }
            break;
        case SurfaceDeclKind::Alias:
            into["alias"] = decl.name;
            break;
        case SurfaceDeclKind::Tag:
            into["tag"] = decl.name;
            break;
        case SurfaceDeclKind::Module:
            into["module"] = decl.name;
            break;
        case SurfaceDeclKind::Guard:
            child(entry, "guards")[owned(fact)] = decl.name;
            break;
        case SurfaceDeclKind::Import: {
            // A file imports a type, a package, or a function named after a type.
            llvm::StringRef what = "package";
            if (function)
            {
                what = functionKey(function->function);
            }
            else if (decl.nameClass == NameClass::Type)
            {
                what = "type";
            }
            child(child(entry, "imports"), decl.of->schema)[owned(what)] = decl.name;
            break;
        }
        case SurfaceDeclKind::Option:
            // In the entry's `union_options`.
            break;
        }
    }
    if (!helpers.empty())
    {
        entry["helpers"] = std::move(helpers);
    }
    for (auto& [section, names] : sectionHelpers)
    {
        owner(section)["helpers"] = std::move(names);
    }
}

}  // namespace

std::string renderNamingManifest(const SemanticModule&                 semantic,
                                 const llvm::ArrayRef<LanguageTraits>  languages,
                                 const llvm::StringRef                 toolVersion,
                                 const TypeNameVersioning              typeNameVersioning,
                                 const std::optional<Language>         target,
                                 const llvm::ArrayRef<ManifestSurface> surfaces)
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
        const SurfacePlan  plan      = allocateSurface(row,
                                                       definitions,
                                                       SurfaceOptions{.packageName   = {},
                                                                      .versioning    = typeNameVersioning,
                                                                      .accessorsOnly = false,
                                                                      .profile       = {}});
        const bool         generated = target && (*target == row.language) && !surfaces.empty();
        llvm::json::Object byType;
        for (std::size_t index = 0; index < plan.definitions.size(); ++index)
        {
            const DefinitionNames& definition = plan.definitions[index];
            llvm::json::Object     entry      = renderDefinition(row, plan, definition);
            // A generation run reports the whole surface its lowering wrote, each profile's apart
            // where it wrote several.
            if (generated && (surfaces.size() == 1))
            {
                renderTree(entry, surfaces.front().plan, definition.key, definitions[index].service);
            }
            else if (generated)
            {
                llvm::json::Object profiles;
                for (const ManifestSurface& surface : surfaces)
                {
                    llvm::json::Object names;
                    renderTree(names, surface.plan, definition.key, definitions[index].service);
                    profiles[owned(surface.profile)] = std::move(names);
                }
                entry["profiles"] = std::move(profiles);
            }
            byType[definition.key] = std::move(entry);
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
