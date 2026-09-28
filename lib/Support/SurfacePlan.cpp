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

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/ImportNameScope.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SectionScopes.h"

namespace llvmdsdl
{
namespace
{

/// @brief The kind of scope a DSDL namespace component opens, or none where it opens no scope.
///
/// A module is a definition's own, so a namespace that the language opens as a module is a
/// namespace scope in the plan: the two are told apart where a namespace and a definition's module
/// of one name are both declared.
std::optional<SurfaceScopeKind> namespaceScopeKind(const NamespaceForm form)
{
    switch (form)
    {
    case NamespaceForm::Joined:
        return std::nullopt;
    case NamespaceForm::Namespace:
    case NamespaceForm::Module:
        return SurfaceScopeKind::Namespace;
    case NamespaceForm::Package:
        return SurfaceScopeKind::Package;
    }
    return std::nullopt;
}

/// @brief The directory, under the output directory, @p row writes a package's source files in.
std::string sourceDirectoryOf(const LanguageTraits& row, const llvm::StringRef packageName)
{
    std::string directory = row.composition.sourceDirectory.str();
    if (row.composition.packageDirectory && !packageName.empty())
    {
        llvm::SmallVector<llvm::StringRef, 4> components;
        packageName.split(components, '.');
        for (const llvm::StringRef component : components)
        {
            directory += component.str() + "/";
        }
    }
    return directory;
}

/// @brief The order a file's imports are claimed in: by the definition's full name, then its version.
std::string importOrder(const DefinitionRef& ref)
{
    std::string fullName;
    for (const std::string& component : ref.namespaceComponents)
    {
        fullName += component + ".";
    }
    return fullName + ref.shortName + ":" + std::to_string(ref.majorVersion) + ":" + std::to_string(ref.minorVersion);
}

/// @brief Builds one language's plan, a definition at a time.
class Allocator final
{
public:
    Allocator(const LanguageTraits&                 row,
              const llvm::ArrayRef<DefinitionParts> definitions,
              const SurfaceOptions&                 options)
        : row_(row)
        , options_(options)
        , sourceDirectory_(sourceDirectoryOf(row, options.packageName))
    {
        for (const DefinitionParts& definition : definitions)
        {
            deprecated_[renderDefinitionKey(definition.ref)] = definition.deprecated;
        }
        plan_.scopes.push_back(SurfaceScope{.kind   = SurfaceScopeKind::Root,
                                            .name   = options.packageName,
                                            .path   = {},
                                            .parent = std::nullopt,
                                            .of     = std::nullopt,
                                            .items  = {}});
        if (!row.composition.rootFile.empty())
        {
            plan_.scopes.front().path = sourceDirectory_ + row.composition.rootFile.str();
        }
    }

    void allocate(const DefinitionParts& definition)
    {
        const Language  language = row_.language;
        DefinitionNames names;
        names.key      = renderDefinitionKey(definition.ref);
        names.typeName = renderDefinitionTypeName(language,
                                                  definition.ref.namespaceComponents,
                                                  definition.ref.shortName,
                                                  definition.ref.majorVersion,
                                                  definition.ref.minorVersion,
                                                  options_.versioning);
        names.fileStem = renderDefinitionFileStem(language,
                                                  definition.ref.shortName,
                                                  definition.ref.majorVersion,
                                                  definition.ref.minorVersion);
        for (const std::string& component : definition.ref.namespaceComponents)
        {
            names.namespaceNames.push_back(
                codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component));
        }
        names.fixedPortId = definition.fixedPortId;

        // A definition's file, and a namespace's own, is written to the namespace's directory.
        const std::vector<std::string>& directories =
            row_.composition.directoriesProjected ? names.namespaceNames : definition.ref.namespaceComponents;
        std::string directory = sourceDirectory_;
        std::size_t space     = 0;
        if (const std::optional<SurfaceScopeKind> kind = namespaceScopeKind(row_.composition.namespaces))
        {
            for (const auto& [name, component] : llvm::zip_equal(names.namespaceNames, directories))
            {
                directory += component + "/";
                space = namespaceScope(space, *kind, name, directory);
            }
        }
        else
        {
            for (const std::string& component : directories)
            {
                directory += component + "/";
            }
        }
        // A definition that shares its namespace's scope is a file of it; one that does not is a
        // module of its own.
        const SurfaceScopeKind fileKind =
            row_.composition.definitionsShareNamespaceScope ? SurfaceScopeKind::File : SurfaceScopeKind::Module;
        const std::size_t file  = openScope(space, fileKind, names.fileStem, std::nullopt);
        names.fileScope         = file;
        plan_.scopes[file].path = directory + names.fileStem + row_.composition.fileExtension.str();

        if (definition.service)
        {
            allocateSection(names, file, "request", definition.request, definition.deprecated);
            if (definition.response)
            {
                allocateSection(names, file, "response", *definition.response, definition.deprecated);
            }
        }
        else
        {
            allocateSection(names, file, "", definition.request, definition.deprecated);
        }
        // A service reached by its own name means its request. Where a section's type is already
        // called that -- a service named `Request` in a language that names a section alone -- the
        // alias would declare the name twice and stand for itself.
        if (definition.service && !declaresName(file, names.typeName))
        {
            names.serviceAlias = declare(file,
                                         names.typeName,
                                         SurfaceDeclKind::Alias,
                                         NameClass::Type,
                                         NameOrigin::Generated,
                                         SurfaceEntity{names.key, "", "", ""});
        }
        allocateBodies(names, space, file, definition.bodies);
        allocateImports(definition, file);
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
            SurfaceScope{.kind = kind, .name = std::move(name), .path = {}, .parent = parent, .of = of, .items = {}});
        plan_.scopes[parent].items.push_back(SurfaceItem{.scope = true, .index = index});
        return index;
    }

    /// @brief The scope a namespace component opens in @p parent: opened by the first definition in
    ///        it and found by the others. It is written to @p directory's namespace file, where the
    ///        language writes one.
    std::size_t namespaceScope(const std::size_t      parent,
                               const SurfaceScopeKind kind,
                               const std::string&     name,
                               const std::string&     directory)
    {
        for (const SurfaceItem& item : plan_.scopes[parent].items)
        {
            if (item.scope && (plan_.scopes[item.index].kind == kind) && (plan_.scopes[item.index].name == name))
            {
                return item.index;
            }
        }
        const std::size_t scope = openScope(parent, kind, name, std::nullopt);
        if (!row_.composition.namespaceFile.empty())
        {
            plan_.scopes[scope].path = directory + row_.composition.namespaceFile.str();
        }
        return scope;
    }

    std::size_t declare(const std::size_t       scope,
                        std::string             name,
                        const SurfaceDeclKind   kind,
                        const NameClass         nameClass,
                        const NameOrigin        origin,
                        SurfaceEntity           of,
                        const SurfaceVisibility visibility = SurfaceVisibility::Public)
    {
        const std::size_t index = plan_.decls.size();
        plan_.decls.push_back(SurfaceDecl{.name       = std::move(name),
                                          .kind       = kind,
                                          .nameClass  = nameClass,
                                          .visibility = visibility,
                                          .origin     = origin,
                                          .of         = std::move(of),
                                          .scope      = scope,
                                          .binds      = std::nullopt});
        plan_.scopes[scope].items.push_back(SurfaceItem{.scope = false, .index = index});
        return index;
    }

    /// @brief Declares one section's type and its members. A deprecated type is declared under a
    ///        name of its own where the language does that, and its public name is an alias of it.
    void allocateSection(DefinitionNames&    names,
                         const std::size_t   file,
                         const std::string&  sectionName,
                         const SectionParts& parts,
                         const bool          deprecated)
    {
        const Language language = row_.language;
        SectionNames   section;
        section.section = sectionName;
        section.typeName =
            sectionName.empty() ? names.typeName : renderSectionTypeName(language, names.typeName, sectionName);
        section.isUnion = parts.isUnion;
        const auto of   = [&](const std::string& member) { return SurfaceEntity{names.key, sectionName, member, ""}; };
        const bool declaredApart = deprecated && row_.composition.deprecatedTypeDeclaredApart;
        section.typeScope =
            openScope(file, SurfaceScopeKind::Type, renderDeclaredTypeName(section.typeName, declaredApart), of(""));

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
        if (declaredApart)
        {
            (void)
                declare(file, section.typeName, SurfaceDeclKind::Alias, NameClass::Type, NameOrigin::Generated, of(""));
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

    /// @brief The class of names @p item is named in: a type scope's name is a type, and a namespace's
    ///        or a module's is a module.
    [[nodiscard]] NameClass classOf(const SurfaceItem& item) const
    {
        if (!item.scope)
        {
            return plan_.decls[item.index].nameClass;
        }
        return (plan_.scopes[item.index].kind == SurfaceScopeKind::Type) ? NameClass::Type : NameClass::Module;
    }

    /// @brief Whether @p scope already holds a scope or a declaration named @p name.
    [[nodiscard]] bool declaresName(const std::size_t scope, const std::string& name) const
    {
        return std::ranges::any_of(plan_.scopes[scope].items, [&](const SurfaceItem& item) {
            return (item.scope ? plan_.scopes[item.index].name : plan_.decls[item.index].name) == name;
        });
    }

    /// @brief What a lowered function declares, by what it does.
    static SurfaceDeclKind bodyKind(const PlanFunction function)
    {
        switch (function)
        {
        case PlanFunction::Serialize:
        case PlanFunction::Deserialize:
        case PlanFunction::Initialize:
            return SurfaceDeclKind::Entry;
        case PlanFunction::Get:
        case PlanFunction::Set:
            return SurfaceDeclKind::Accessor;
        case PlanFunction::Helper:
            break;
        }
        return SurfaceDeclKind::Helper;
    }

    /// @brief Declares the names the definition's lowered functions take.
    void allocateBodies(const DefinitionNames&        names,
                        const std::size_t             space,
                        const std::size_t             file,
                        const std::vector<BodyParts>& bodies)
    {
        const Language language = row_.language;
        const auto     of       = [&](const BodyParts& body) {
            return SurfaceEntity{names.key, body.plan.section, body.plan.member, body.symbol};
        };
        switch (row_.composition.helpers)
        {
        case HelperNaming::LinkName:
            // Every lowered function is linked, a helper nothing calls included.
            for (const BodyParts& body : bodies)
            {
                (void) declare(file,
                               renderLoweredLinkName(language, body.plan),
                               bodyKind(body.plan.function),
                               NameClass::Value,
                               NameOrigin::Generated,
                               of(body));
            }
            return;
        case HelperNaming::Binding:
            for (const BodyParts& body : bodies)
            {
                if ((body.plan.function == PlanFunction::Helper) && !body.unreferenced)
                {
                    (void) declare(file,
                                   renderHelperBindingIdentifier(language, body.plan),
                                   SurfaceDeclKind::Helper,
                                   NameClass::Value,
                                   NameOrigin::Generated,
                                   of(body));
                }
            }
            return;
        case HelperNaming::Package:
            declareHelpers(names,
                           file,
                           bodies,
                           packagePools_.try_emplace(space, language).first->second,
                           names.typeName);
            return;
        case HelperNaming::Module: {
            NamingScope pool(language);
            declareHelpers(names, file, bodies, pool, {});
            return;
        }
        }
    }

    /// @brief Declares each helper among @p bodies under the name @p pool allocates it.
    void declareHelpers(const DefinitionNames&        names,
                        const std::size_t             file,
                        const std::vector<BodyParts>& bodies,
                        NamingScope&                  pool,
                        const llvm::StringRef         qualifier)
    {
        const llvm::StringMap<std::string> helpers = declareHelperNames(row_.language, bodies, pool, qualifier);
        for (const BodyParts& body : bodies)
        {
            const auto found = helpers.find(body.symbol);
            if (found != helpers.end())
            {
                (void) declare(file,
                               found->second,
                               SurfaceDeclKind::Helper,
                               NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{names.key, body.plan.section, body.plan.member, body.symbol},
                               SurfaceVisibility::Private);
            }
        }
    }

    /// @brief Declares the local name @p definition's file imports each definition it holds under.
    ///
    /// Band 5. What the file declares in the class an import is made in is reserved first, so the
    /// import is what moves. A field holding a definition as a view names no type of it.
    void allocateImports(const DefinitionParts& definition, const std::size_t file)
    {
        // Go's package imports, and TypeScript's, which bring a type's functions with it, are named
        // by their emitters.
        if (row_.composition.imports != ImportNaming::Type)
        {
            return;
        }
        const NameClasses&                 classes   = row_.classification.nameClasses;
        const std::optional<NamePartition> partition = namePartition(classes, NameClass::Type);
        ImportNameScope                    scope(row_.language);
        for (const SurfaceItem& item : plan_.scopes[file].items)
        {
            if (namePartition(classes, classOf(item)) == partition)
            {
                scope.reserve(item.scope ? plan_.scopes[item.index].name : plan_.decls[item.index].name);
            }
        }
        std::map<std::string, DefinitionRef> composites;
        for (const SectionParts* section : {&definition.request, definition.response ? &*definition.response : nullptr})
        {
            if (section == nullptr)
            {
                continue;
            }
            for (const FieldParts& field : section->fields)
            {
                if (field.composite && !field.view)
                {
                    composites.try_emplace(importOrder(*field.composite), *field.composite);
                }
            }
        }
        const std::string own = renderDefinitionKey(definition.ref);
        for (const auto& [order, ref] : composites)
        {
            const std::string key   = renderDefinitionKey(ref);
            const auto        found = deprecated_.find(key);
            // The tree names a definition the module holds, and a definition does not import itself.
            if ((key == own) || (found == deprecated_.end()))
            {
                continue;
            }
            const bool        apart    = found->second && row_.composition.deprecatedTypeDeclaredApart;
            const std::string exported = renderDeclaredTypeName(renderDefinitionTypeName(row_.language,
                                                                                         ref.namespaceComponents,
                                                                                         ref.shortName,
                                                                                         ref.majorVersion,
                                                                                         ref.minorVersion,
                                                                                         options_.versioning),
                                                                apart);
            (void) declare(file,
                           scope.claim(ref, exported, apart),
                           SurfaceDeclKind::Import,
                           NameClass::Type,
                           NameOrigin::Definition,
                           SurfaceEntity{key, "", "", ""},
                           SurfaceVisibility::Private);
        }
    }

    const LanguageTraits& row_;
    const SurfaceOptions& options_;
    SurfacePlan           plan_;

    /// @brief The directory the source files are written in, ending in `/` where it is not empty.
    std::string sourceDirectory_;

    /// @brief Whether each definition is deprecated, by its key.
    llvm::StringMap<bool> deprecated_;

    /// @brief The helper pool of each package, by the package's scope.
    std::map<std::size_t, NamingScope> packagePools_;
};

}  // namespace

std::optional<NamePartition> namePartition(const NameClasses& classes, const NameClass nameClass)
{
    switch (nameClass)
    {
    case NameClass::Value:
        return NamePartition::Values;
    case NameClass::Type:
        return classes.typesApartFromValues ? NamePartition::Types : NamePartition::Values;
    case NameClass::Module:
        return classes.modulesAmongTypes ? namePartition(classes, NameClass::Type) : NamePartition::Modules;
    case NameClass::Tag:
        return classes.tags ? std::optional<NamePartition>(NamePartition::Tags) : std::nullopt;
    case NameClass::Macro:
        return classes.macros ? std::optional<NamePartition>(NamePartition::Macros) : std::nullopt;
    }
    return std::nullopt;
}

SurfacePlan allocateSurface(const LanguageTraits&                 row,
                            const llvm::ArrayRef<DefinitionParts> definitions,
                            const SurfaceOptions&                 options)
{
    Allocator allocator(row, definitions, options);
    for (const DefinitionParts& definition : definitions)
    {
        allocator.allocate(definition);
    }
    return allocator.take();
}

}  // namespace llvmdsdl
