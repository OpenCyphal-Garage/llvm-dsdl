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
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/GeneratedFact.h"
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

/// @brief Whether a section states @p fact, which the generator declares a member of its type for.
/// @param[in] parts The section.
/// @param[in] message Whether the section is a message's rather than a service's.
/// @param[in] fixedPortId The message's fixed port-ID, where it has one.
bool states(const GeneratedFact                fact,
            const SectionParts&                parts,
            const bool                         message,
            const std::optional<std::uint32_t> fixedPortId)
{
    switch (fact)
    {
    case GeneratedFact::UnionOptionCount:
        return parts.isUnion;
    case GeneratedFact::HasFixedPortId:
        return message;
    case GeneratedFact::FixedPortId:
        return fixedPortId.has_value();
    case GeneratedFact::FullName:
    case GeneratedFact::FullNameAndVersion:
    case GeneratedFact::IsDeprecated:
    case GeneratedFact::ExtentBytes:
    case GeneratedFact::SerializationBufferSizeBytes:
    case GeneratedFact::WireFlat:
    case GeneratedFact::WireFlatReason:
    case GeneratedFact::HostImage:
    case GeneratedFact::HostImageReason:
    case GeneratedFact::MemoryMode:
    case GeneratedFact::InlineThresholdBytes:
    case GeneratedFact::PoolClass:
    case GeneratedFact::ArrayCapacity:
    case GeneratedFact::ArrayIsVariableLength:
    case GeneratedFact::UnionTag:
    case GeneratedFact::Placeholder:
    case GeneratedFact::IncludeGuard:
    case GeneratedFact::SelectedType:
    case GeneratedFact::SelectedVersion:
    case GeneratedFact::OptionTest:
    case GeneratedFact::OptionSelect:
    case GeneratedFact::Serialize:
    case GeneratedFact::Deserialize:
    case GeneratedFact::Initialize:
    case GeneratedFact::AppendWireImage:
    case GeneratedFact::WireImage:
    case GeneratedFact::FromWireImage:
        break;
    }
    return true;
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
        allocateFileGuards(names, definition.ref, file);

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
        if (definition.service)
        {
            allocateServiceConstants(names, file);
        }
        allocateMembers(names, definition);
        allocateServiceEntryPoints(names, definition);
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

    std::size_t declare(const std::size_t                  scope,
                        std::string                        name,
                        const SurfaceDeclKind              kind,
                        const NameClass                    nameClass,
                        const NameOrigin                   origin,
                        SurfaceEntity                      of,
                        const SurfaceVisibility            visibility = SurfaceVisibility::Public,
                        const std::optional<GeneratedFact> fact       = std::nullopt)
    {
        const std::size_t index = plan_.decls.size();
        plan_.decls.push_back(SurfaceDecl{.name       = std::move(name),
                                          .kind       = kind,
                                          .nameClass  = nameClass,
                                          .visibility = visibility,
                                          .origin     = origin,
                                          .of         = std::move(of),
                                          .fact       = fact,
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
        // A language that keeps structure tags as a class of their own names each type's tag as
        // the type.
        if (row_.classification.nameClasses.tags)
        {
            (void) declare(file,
                           plan_.scopes[section.typeScope].name,
                           SurfaceDeclKind::Tag,
                           NameClass::Tag,
                           NameOrigin::Definition,
                           of(""));
        }

        // An accessors-only run declares the type's accessors and none of its data members.
        if (!options_.accessorsOnly)
        {
            const NamingScope fields = makeSectionFieldScope(language, parts);
            for (const FieldParts& field : parts.fields)
            {
                if (!field.padding)
                {
                    section.fields[field.name] = declare(section.typeScope,
                                                         fields.get(IdentifierRole::FieldName, field.name),
                                                         SurfaceDeclKind::Field,
                                                         NameClass::Field,
                                                         NameOrigin::Definition,
                                                         of(field.name));
                }
            }
            allocateDataMembers(section, parts, of);
        }
        allocateTypeMembers(section, parts, of, sectionName.empty() ? names.fixedPortId : std::nullopt);
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

    /// @brief Declares the data members the generator adds to a section's type: a union's tag, and
    ///        the member a structure with no fields holds.
    template <typename Of>
    void allocateDataMembers(const SectionNames& section, const SectionParts& parts, const Of& of)
    {
        const bool empty = llvm::all_of(parts.fields, [](const FieldParts& field) { return field.padding; });
        for (const GeneratedName& member : generatedDataMembers(row_.language))
        {
            const bool stated = (member.fact == GeneratedFact::UnionTag) ? parts.isUnion : (!parts.isUnion && empty);
            if (stated)
            {
                (void) declare(section.typeScope,
                               member.name.str(),
                               SurfaceDeclKind::Field,
                               NameClass::Field,
                               NameOrigin::Generated,
                               of(""),
                               SurfaceVisibility::Public,
                               member.fact);
            }
        }
    }

    /// @brief Declares the members the generator adds to a section's type beside its constants,
    ///        each where the section states its fact.
    /// @param[in] fixedPortId The fixed port-ID, where the section is a message with one.
    template <typename Of>
    void allocateTypeMembers(const SectionNames&                section,
                             const SectionParts&                parts,
                             const Of&                          of,
                             const std::optional<std::uint32_t> fixedPortId)
    {
        const Language    language  = row_.language;
        const NameClass   nameClass = row_.composition.constantsAreMacros ? NameClass::Macro : NameClass::Value;
        const bool        message   = section.section.empty();
        const std::size_t scope     = (row_.composition.constants == ConstantsScope::Type)
                                          ? section.typeScope
                                          : *plan_.scopes[section.typeScope].parent;
        for (const GeneratedName& member : generatedTypeMembers(language))
        {
            if (member.fact == GeneratedFact::PoolClass)
            {
                for (const auto& [field, name] : poolClassConstantNames(language, parts))
                {
                    (void) declare(scope,
                                   name,
                                   SurfaceDeclKind::Constant,
                                   nameClass,
                                   NameOrigin::Generated,
                                   of(field),
                                   SurfaceVisibility::Public,
                                   member.fact);
                }
                continue;
            }
            if (states(member.fact, parts, message, fixedPortId))
            {
                (void) declare(scope,
                               (row_.composition.constants == ConstantsScope::Package)
                                   ? makeGoConstantScope(parts, section.typeName)
                                         .get(IdentifierRole::ConstantName,
                                              goGeneratedConstantKey(section.typeName, member.name))
                                   : renderDeclaredConstantName(language, section.typeName, member.name),
                               SurfaceDeclKind::Constant,
                               nameClass,
                               NameOrigin::Generated,
                               of(""),
                               SurfaceVisibility::Public,
                               member.fact);
            }
        }
    }

    /// @brief Declares the macros that guard a definition's file. One saying a translation unit holds
    ///        a version of the definition is declared where type names carry no version, which
    ///        makes two versions one name.
    void allocateFileGuards(const DefinitionNames& names, const DefinitionRef& ref, const std::size_t file)
    {
        const Language    language  = row_.language;
        const std::string versioned = renderDefinitionTypeName(language,
                                                               ref.namespaceComponents,
                                                               ref.shortName,
                                                               ref.majorVersion,
                                                               ref.minorVersion,
                                                               TypeNameVersioning::Versioned);
        for (const GuardName& guard : generatedFileGuards(language))
        {
            const bool selection =
                (guard.fact == GeneratedFact::SelectedType) || (guard.fact == GeneratedFact::SelectedVersion);
            if (selection && (options_.versioning == TypeNameVersioning::Versioned))
            {
                continue;
            }
            (void) declare(file,
                           codegenProjectIdentifier(language,
                                                    IdentifierRole::MacroName,
                                                    guard.prefix.str() +
                                                        (guard.versioned ? versioned : names.typeName) +
                                                        guard.suffix.str()),
                           SurfaceDeclKind::Guard,
                           NameClass::Macro,
                           NameOrigin::Generated,
                           SurfaceEntity{names.key, "", "", ""},
                           SurfaceVisibility::Public,
                           guard.fact);
        }
    }

    /// @brief Declares a service's own constants beside its sections' types, each named after the
    ///        service.
    void allocateServiceConstants(const DefinitionNames& names, const std::size_t file)
    {
        const Language language = row_.language;
        // Beside the type rather than in it: a language that declares a type's constants in the
        // type names these as a module declares a type's constants.
        const auto named = [&](const llvm::StringRef token) {
            switch (row_.composition.constants)
            {
            case ConstantsScope::Type:
                return codegenProjectIdentifier(language, IdentifierRole::ConstantName, names.typeName) + "_" +
                       token.str();
            case ConstantsScope::Package:
                return goConstantName({names.typeName, token});
            case ConstantsScope::Enclosing:
            case ConstantsScope::Module:
                break;
            }
            return renderDeclaredConstantName(language, names.typeName, token);
        };
        for (const GeneratedName& constant : generatedServiceConstants(language))
        {
            if ((constant.fact != GeneratedFact::FixedPortId) || names.fixedPortId)
            {
                (void) declare(file,
                               named(constant.name),
                               SurfaceDeclKind::Constant,
                               row_.composition.constantsAreMacros ? NameClass::Macro : NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{names.key, "", "", ""},
                               SurfaceVisibility::Public,
                               constant.fact);
            }
        }
    }

    /// @brief Declares the members of each section's type its lowered functions are: the entry
    ///        points, the functions that wrap them, and the accessors.
    void allocateMembers(const DefinitionNames& names, const DefinitionParts& definition)
    {
        const Language                       language = row_.language;
        const std::optional<AccessorVerbs>   verbs    = memberAccessorVerbs(language);
        const llvm::ArrayRef<EntryPointName> entries  = entryPointNames(language);
        const auto                           of       = [&](const BodyParts& body) {
            return SurfaceEntity{names.key, body.plan.section, body.plan.member, body.symbol};
        };
        for (const SectionNames& section : names.sections)
        {
            const SectionParts& parts     = (section.section == "response") ? *definition.response : definition.request;
            const auto          inSection = [&](const BodyParts& body) { return body.plan.section == section.section; };
            for (const EntryPointName& entry : entries)
            {
                for (const BodyParts& body : definition.bodies)
                {
                    if (inSection(body) && (body.plan.function == entry.function))
                    {
                        (void) declare(entry.beside ? names.fileScope : section.typeScope,
                                       entry.beside ? entry.name.str() + section.typeName : entry.name.str(),
                                       SurfaceDeclKind::Entry,
                                       NameClass::Value,
                                       NameOrigin::Generated,
                                       of(body));
                    }
                }
            }
            for (const WrapperName& wrapper : generatedWrappers(language))
            {
                for (const BodyParts& body : definition.bodies)
                {
                    if (inSection(body) && (body.plan.function == wrapper.wraps))
                    {
                        (void) declare(section.typeScope,
                                       wrapper.name.str(),
                                       SurfaceDeclKind::Wrapper,
                                       NameClass::Value,
                                       NameOrigin::Generated,
                                       of(body),
                                       SurfaceVisibility::Public,
                                       wrapper.fact);
                    }
                }
            }
            allocateFreeFunctions(names, section, parts, definition.bodies);
            if (!verbs)
            {
                continue;
            }
            // The accessors are allocated in a pool of their own, the union's tag first, so a union
            // whose options collide with nothing keeps its accessors' names and a colliding option is
            // the side that moves. A pool is keyed on the name handed to it, so the verb's separator
            // is unconditional: `_tag_` and a field `tag_` compose `get__tag_` and `get_tag_`, two
            // keys the projection folds onto one name, which the pool tells apart.
            const NamingScope fields = makeSectionFieldScope(language, parts);
            NamingScope       pool(language);
            const auto        key = [&](const llvm::StringRef verb, const llvm::StringRef member) {
                const std::string name =
                    (member == kPlanUnionTagMember) ? member.str() : fields.get(IdentifierRole::FieldName, member);
                return verb.str() + "_" + name;
            };
            std::vector<llvm::StringRef> members;
            if (parts.isUnion)
            {
                members.push_back(kPlanUnionTagMember);
            }
            for (const FieldParts& field : parts.fields)
            {
                if (!field.padding)
                {
                    members.emplace_back(field.name);
                }
            }
            for (const llvm::StringRef member : members)
            {
                (void) pool.declare(IdentifierRole::FunctionName, key(verbs->getter, member));
                (void) pool.declare(IdentifierRole::FunctionName, key(verbs->setter, member));
            }
            for (const BodyParts& body : definition.bodies)
            {
                const bool getter = body.plan.function == PlanFunction::Get;
                if (inSection(body) && (getter || (body.plan.function == PlanFunction::Set)))
                {
                    (void) declare(section.typeScope,
                                   pool.get(IdentifierRole::FunctionName,
                                            key(getter ? verbs->getter : verbs->setter, body.plan.member)),
                                   SurfaceDeclKind::Accessor,
                                   NameClass::Value,
                                   NameOrigin::Generated,
                                   of(body));
                }
            }
        }
    }

    /// @brief Declares the free functions beside a section's type, where the language compiles the
    ///        bodies apart from them: a function wrapping each body a caller reaches, and a union's
    ///        test and selector of each option, which take the object an accessors-only run has not.
    void allocateFreeFunctions(const DefinitionNames&        names,
                               const SectionNames&           section,
                               const SectionParts&           parts,
                               const std::vector<BodyParts>& bodies)
    {
        const Language           language = row_.language;
        const FreeFunctionNames& free     = row_.composition.freeFunctions;
        const std::size_t        file     = *plan_.scopes[section.typeScope].parent;
        const NamingScope        fields   = makeSectionFieldScope(language, parts);
        const auto               member   = [&](const llvm::StringRef name) {
            return (name == kPlanUnionTagMember) ? unionTagMemberName(language).str()
                                                 : fields.get(IdentifierRole::FieldName, name);
        };
        // Where the bodies are not compiled apart and the accessors are free functions, each
        // accessor is its body.
        if (free.loweredBodySuffix.empty())
        {
            if (free.accessors == AccessorNaming::None)
            {
                return;
            }
            for (const BodyParts& body : bodies)
            {
                const bool getter = body.plan.function == PlanFunction::Get;
                if ((body.plan.section == section.section) && (getter || (body.plan.function == PlanFunction::Set)))
                {
                    (void) declare(file,
                                   renderAccessorName(language,
                                                      section.typeName,
                                                      getter ? AccessorVerb::Get : AccessorVerb::Set,
                                                      member(body.plan.member)),
                                   SurfaceDeclKind::Accessor,
                                   NameClass::Value,
                                   NameOrigin::Generated,
                                   SurfaceEntity{names.key, body.plan.section, body.plan.member, body.symbol});
                }
            }
            return;
        }
        for (const BodyParts& body : bodies)
        {
            if (body.plan.section != section.section)
            {
                continue;
            }
            std::optional<std::string> name;
            switch (body.plan.function)
            {
            case PlanFunction::Serialize:
                name = renderEntryPointName(language, section.typeName, EntryPoint::Serialize);
                break;
            case PlanFunction::Deserialize:
                name = renderEntryPointName(language, section.typeName, EntryPoint::Deserialize);
                break;
            case PlanFunction::Initialize:
                if (free.initializer)
                {
                    name = renderEntryPointName(language, section.typeName, EntryPoint::Initialize);
                }
                break;
            case PlanFunction::Get:
            case PlanFunction::Set:
                if (free.accessors != AccessorNaming::None)
                {
                    name = renderAccessorName(language,
                                              section.typeName,
                                              (body.plan.function == PlanFunction::Get) ? AccessorVerb::Get
                                                                                        : AccessorVerb::Set,
                                              member(body.plan.member));
                }
                break;
            case PlanFunction::Helper:
                break;
            }
            if (name)
            {
                (void) declare(file,
                               *name,
                               SurfaceDeclKind::Wrapper,
                               NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{names.key, body.plan.section, body.plan.member, body.symbol});
            }
        }
        if (!free.unionOptionFunctions || !parts.isUnion || options_.accessorsOnly)
        {
            return;
        }
        for (const FieldParts& field : parts.fields)
        {
            if (field.padding)
            {
                continue;
            }
            for (const auto& [verb, fact] : {std::pair{AccessorVerb::Is, GeneratedFact::OptionTest},
                                             std::pair{AccessorVerb::Select, GeneratedFact::OptionSelect}})
            {
                (void) declare(file,
                               renderAccessorName(language, section.typeName, verb, member(field.name)),
                               SurfaceDeclKind::Method,
                               NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{names.key, section.section, field.name, ""},
                               SurfaceVisibility::Public,
                               fact);
            }
        }
    }

    /// @brief Declares a service's own entry points beside its sections' types, where the language
    ///        writes each as a free function: each calls its request's.
    void allocateServiceEntryPoints(const DefinitionNames& names, const DefinitionParts& definition)
    {
        const FreeFunctionNames& free = row_.composition.freeFunctions;
        if (!definition.service || free.loweredBodySuffix.empty())
        {
            return;
        }
        for (const auto& [function, entryPoint, fact] :
             {std::tuple{PlanFunction::Serialize, EntryPoint::Serialize, GeneratedFact::Serialize},
              std::tuple{PlanFunction::Deserialize, EntryPoint::Deserialize, GeneratedFact::Deserialize},
              std::tuple{PlanFunction::Initialize, EntryPoint::Initialize, GeneratedFact::Initialize}})
        {
            const bool lowered = llvm::any_of(definition.bodies, [&](const BodyParts& body) {
                return (body.plan.section == "request") && (body.plan.function == function);
            });
            if (lowered && ((function != PlanFunction::Initialize) || free.initializer))
            {
                (void) declare(names.fileScope,
                               renderEntryPointName(row_.language, names.typeName, entryPoint),
                               SurfaceDeclKind::Wrapper,
                               NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{names.key, "", "", ""},
                               SurfaceVisibility::Public,
                               fact);
            }
        }
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
                                   of(field.name),
                                   SurfaceVisibility::Public,
                                   (kind == ArrayMetadataKind::Capacity) ? GeneratedFact::ArrayCapacity
                                                                         : GeneratedFact::ArrayIsVariableLength);
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
                               loweredFunctionKind(body.plan.function),
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

    /// @brief Declares the local names @p definition's file imports what it takes from other
    ///        definitions' files under.
    ///
    /// Band 5. A field holding a definition as a view names no type of it, and an accessors-only
    /// file names no other definition's type: a composite's getter answers its bytes.
    void allocateImports(const DefinitionParts& definition, const std::size_t file)
    {
        if (options_.accessorsOnly)
        {
            return;
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
        switch (row_.composition.imports)
        {
        case ImportNaming::Type:
            allocateTypeImports(definition, file, composites);
            return;
        case ImportNaming::Package:
            allocatePackageImports(definition, file, composites);
            return;
        case ImportNaming::None:
        case ImportNaming::TypeAndFunctions:
            // TypeScript's imports, which bring a type's functions with it, are named by its emitter.
            return;
        }
    }

    /// @brief Declares the local name the file imports each definition's type under. What the file
    ///        declares in the class an import is made in is reserved first, so the import is what
    ///        moves.
    void allocateTypeImports(const DefinitionParts&                      definition,
                             const std::size_t                           file,
                             const std::map<std::string, DefinitionRef>& composites)
    {
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

    /// @brief Declares the local name the file imports each other package it takes a definition
    ///        from under: `pkg_` and the package's namespace, which a later import meeting it
    ///        follows with `_1`, `_2` and so on. An import names the package through the first
    ///        definition the file takes from it.
    void allocatePackageImports(const DefinitionParts&                      definition,
                                const std::size_t                           file,
                                const std::map<std::string, DefinitionRef>& composites)
    {
        const Language language = row_.language;
        const auto     package  = [&](const DefinitionRef& ref) {
            std::string path;
            for (const std::string& component : ref.namespaceComponents)
            {
                path += (path.empty() ? "" : "/") +
                        codegenProjectIdentifier(language, IdentifierRole::NamespaceName, component);
            }
            return path;
        };
        const std::string     own = package(definition.ref);
        std::set<std::string> used;
        std::set<std::string> imported;
        for (const auto& [order, ref] : composites)
        {
            const std::string path = package(ref);
            if (path.empty() || (path == own))
            {
                continue;
            }
            const std::string base  = "pkg_" + codegenProjectIdentifier(language,
                                                                        IdentifierRole::NamespaceName,
                                                                        llvm::join(ref.namespaceComponents, "_"));
            std::string       alias = (base == "pkg_") ? std::string("pkg_dep") : base;
            const std::string first = alias;
            for (std::size_t suffix = 1; used.contains(alias); ++suffix)
            {
                alias = first + "_" + std::to_string(suffix);
            }
            used.insert(alias);
            const std::string key = renderDefinitionKey(ref);
            if (imported.insert(path).second && deprecated_.contains(key))
            {
                (void) declare(file,
                               alias,
                               SurfaceDeclKind::Import,
                               NameClass::Value,
                               NameOrigin::Generated,
                               SurfaceEntity{key, "", "", ""},
                               SurfaceVisibility::Private);
            }
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

SurfaceDeclKind loweredFunctionKind(const PlanFunction function)
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
    case NameClass::Field:
        return classes.fieldsApart ? NamePartition::Fields : NamePartition::Values;
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
