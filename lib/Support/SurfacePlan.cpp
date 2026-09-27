//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements `allocateSurface`.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/SurfacePlan.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/SectionScopes.h"

namespace llvmdsdl
{
namespace
{

/// @brief The kind of scope a DSDL namespace component opens, or none where it opens no scope.
std::optional<SurfaceScopeKind> namespaceScopeKind(const NamespaceForm form)
{
    switch (form)
    {
    case NamespaceForm::Joined:
        return std::nullopt;
    case NamespaceForm::Namespace:
        return SurfaceScopeKind::Namespace;
    case NamespaceForm::Module:
        return SurfaceScopeKind::Module;
    case NamespaceForm::Package:
        return SurfaceScopeKind::Package;
    }
    return std::nullopt;
}

/// @brief The key a definition is reported and referred to by: `ns.Name.1.0`.
std::string definitionKey(const DefinitionParts& definition)
{
    std::string key;
    for (const std::string& component : definition.namespaceComponents)
    {
        key += component + ".";
    }
    return key + definition.shortName + "." + std::to_string(definition.majorVersion) + "." +
           std::to_string(definition.minorVersion);
}

/// @brief Builds one language's plan, a definition at a time.
class Allocator final
{
public:
    Allocator(const LanguageTraits& row, const SurfaceOptions& options)
        : row_(row)
        , options_(options)
    {
        plan_.scopes.push_back(SurfaceScope{.kind   = SurfaceScopeKind::Root,
                                            .name   = options.packageName,
                                            .parent = std::nullopt,
                                            .of     = std::nullopt,
                                            .items  = {}});
    }

    void allocate(const DefinitionParts& definition)
    {
        const Language  language = row_.language;
        DefinitionNames names;
        names.key      = definitionKey(definition);
        names.typeName = renderDefinitionTypeName(language,
                                                  definition.namespaceComponents,
                                                  definition.shortName,
                                                  definition.majorVersion,
                                                  definition.minorVersion,
                                                  options_.versioning);
        names.fileStem =
            renderDefinitionFileStem(language, definition.shortName, definition.majorVersion, definition.minorVersion);
        for (const std::string& component : definition.namespaceComponents)
        {
            names.namespaceNames.push_back(
                codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component));
        }
        names.fixedPortId = definition.fixedPortId;

        std::size_t space = 0;
        if (const std::optional<SurfaceScopeKind> kind = namespaceScopeKind(row_.composition.namespaces))
        {
            for (const std::string& name : names.namespaceNames)
            {
                space = namespaceScope(space, *kind, name);
            }
        }
        // A definition that shares its namespace's scope is a file of it; one that does not is a
        // module of its own.
        const SurfaceScopeKind fileKind =
            row_.composition.definitionsShareNamespaceScope ? SurfaceScopeKind::File : SurfaceScopeKind::Module;
        const std::size_t file = openScope(space, fileKind, names.fileStem, std::nullopt);

        if (definition.service)
        {
            allocateSection(names, file, "request", definition.request);
            if (definition.response)
            {
                allocateSection(names, file, "response", *definition.response);
            }
        }
        else
        {
            allocateSection(names, file, "", definition.request);
        }
        plan_.definitions.push_back(std::move(names));
    }

    SurfacePlan take()
    {
        return std::move(plan_);
    }

private:
    std::size_t openScope(const std::size_t                   parent,
                          const SurfaceScopeKind              kind,
                          std::string                         name,
                          const std::optional<SurfaceEntity>& of)
    {
        const std::size_t index = plan_.scopes.size();
        plan_.scopes.push_back(
            SurfaceScope{.kind = kind, .name = std::move(name), .parent = parent, .of = of, .items = {}});
        plan_.scopes[parent].items.push_back(SurfaceItem{.scope = true, .index = index});
        return index;
    }

    /// @brief The scope a namespace component opens in @p parent: opened by the first definition in
    ///        it and found by the others.
    std::size_t namespaceScope(const std::size_t parent, const SurfaceScopeKind kind, const std::string& name)
    {
        for (const SurfaceItem& item : plan_.scopes[parent].items)
        {
            if (item.scope && (plan_.scopes[item.index].kind == kind) && (plan_.scopes[item.index].name == name))
            {
                return item.index;
            }
        }
        return openScope(parent, kind, name, std::nullopt);
    }

    std::size_t declare(const std::size_t     scope,
                        std::string           name,
                        const SurfaceDeclKind kind,
                        const NameClass       nameClass,
                        const NameOrigin      origin,
                        SurfaceEntity         of)
    {
        const std::size_t index = plan_.decls.size();
        plan_.decls.push_back(SurfaceDecl{.name      = std::move(name),
                                          .kind      = kind,
                                          .nameClass = nameClass,
                                          .origin    = origin,
                                          .of        = std::move(of),
                                          .scope     = scope});
        plan_.scopes[scope].items.push_back(SurfaceItem{.scope = false, .index = index});
        return index;
    }

    void allocateSection(DefinitionNames&    names,
                         const std::size_t   file,
                         const std::string&  sectionName,
                         const SectionParts& parts)
    {
        const Language language = row_.language;
        SectionNames   section;
        section.section = sectionName;
        section.typeName =
            sectionName.empty() ? names.typeName : renderSectionTypeName(language, names.typeName, sectionName);
        section.isUnion   = parts.isUnion;
        const auto of     = [&](const std::string& member) { return SurfaceEntity{names.key, sectionName, member}; };
        section.typeScope = openScope(file, SurfaceScopeKind::Type, section.typeName, of(""));

        const NamingScope fields = makeSectionFieldScope(language, parts);
        for (const FieldParts& field : parts.fields)
        {
            if (!field.padding)
            {
                section.fields[field.name] = declare(section.typeScope,
                                                     fields.get(IdentifierRole::FieldName, field.name),
                                                     SurfaceDeclKind::Field,
                                                     NameClass::Value,
                                                     NameOrigin::Definition,
                                                     of(field.name));
            }
        }
        if (row_.composition.constants == ConstantsScope::Package)
        {
            allocatePackageConstants(section, file, parts, of);
        }
        else
        {
            allocateConstants(section, file, parts, of);
        }
        names.sections.push_back(std::move(section));
    }

    /// @brief Declares a section's constants where the type's own scope, the scope around the type
    ///        or the module holds them.
    template <typename Of>
    void allocateConstants(SectionNames& section, const std::size_t file, const SectionParts& parts, const Of& of)
    {
        const Language    language  = row_.language;
        const std::size_t scope     = (row_.composition.constants == ConstantsScope::Type) ? section.typeScope : file;
        const NameClass   nameClass = row_.composition.constantsAreMacros ? NameClass::Macro : NameClass::Value;
        const NamingScope pool      = makeSectionConstantScope(language,
                                                               parts,
                                                               codegenProjectIdentifier(language,
                                                                                        IdentifierRole::ConstantName,
                                                                                        section.typeName));
        const auto        declared  = [&](const std::string& allocated) {
            return renderDeclaredConstantName(language, section.typeName, allocated);
        };
        if (row_.composition.arrayMetadataConstants)
        {
            for (const FieldParts& field : parts.fields)
            {
                if (field.padding || !field.array)
                {
                    continue;
                }
                for (const ArrayMetadataKind kind : {ArrayMetadataKind::Capacity, ArrayMetadataKind::IsVariableLength})
                {
                    (void) declare(scope,
                                   declared(pool.get(IdentifierRole::MacroName,
                                                     arrayMetadataName(language, field.name, kind))),
                                   SurfaceDeclKind::Constant,
                                   nameClass,
                                   NameOrigin::Generated,
                                   of(field.name));
                }
            }
        }
        if (parts.isUnion)
        {
            for (const FieldParts& field : parts.fields)
            {
                if (!field.padding)
                {
                    const std::size_t decl =
                        declare(scope,
                                declared(pool.get(IdentifierRole::MacroName, unionOptionTagName(language, field.name))),
                                SurfaceDeclKind::Option,
                                nameClass,
                                NameOrigin::Generated,
                                of(field.name));
                    section.options[field.name] = OptionName{.decl = decl, .tag = field.unionOptionIndex};
                }
            }
        }
        for (const std::string& constant : parts.constants)
        {
            section.constants[constant] = declare(scope,
                                                  declared(pool.get(IdentifierRole::ConstantName, constant)),
                                                  SurfaceDeclKind::Constant,
                                                  nameClass,
                                                  NameOrigin::Definition,
                                                  of(constant));
        }
    }

    /// @brief Declares a section's constants in the package's scope, each named whole.
    template <typename Of>
    void allocatePackageConstants(SectionNames&       section,
                                  const std::size_t   file,
                                  const SectionParts& parts,
                                  const Of&           of)
    {
        const llvm::StringRef typeName = section.typeName;
        const NamingScope     pool     = makeGoConstantScope(parts, typeName);
        if (parts.isUnion)
        {
            for (const FieldParts& field : parts.fields)
            {
                if (!field.padding)
                {
                    const std::size_t decl      = declare(file,
                                                          pool.get(IdentifierRole::ConstantName,
                                                                   goConstantKey({typeName, field.name, "OPTION_TAG"})),
                                                          SurfaceDeclKind::Option,
                                                          NameClass::Value,
                                                          NameOrigin::Generated,
                                                          of(field.name));
                    section.options[field.name] = OptionName{.decl = decl, .tag = field.unionOptionIndex};
                }
            }
        }
        for (const std::string& constant : parts.constants)
        {
            section.constants[constant] =
                declare(file,
                        pool.get(IdentifierRole::ConstantName, goConstantKey({typeName, constant})),
                        SurfaceDeclKind::Constant,
                        NameClass::Value,
                        NameOrigin::Definition,
                        of(constant));
        }
    }

    const LanguageTraits& row_;
    const SurfaceOptions& options_;
    SurfacePlan           plan_;
};

}  // namespace

SurfacePlan allocateSurface(const LanguageTraits&                 row,
                            const llvm::ArrayRef<DefinitionParts> definitions,
                            const SurfaceOptions&                 options)
{
    Allocator allocator(row, options);
    for (const DefinitionParts& definition : definitions)
    {
        allocator.allocate(definition);
    }
    return allocator.take();
}

}  // namespace llvmdsdl
