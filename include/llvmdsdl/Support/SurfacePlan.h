//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The surface plan: the scopes one target's output opens and every name declared in them, as
/// `allocateSurface` allocates them.
///
/// The plan is the tree `dsdl.surface` holds, before there is a module to hold it. Discovery, the
/// naming manifest and the lowering all allocate through `allocateSurface`, so each of them reads
/// the one answer. It takes a definition's parts rather than the semantic model, so the front end
/// can call it.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_SURFACE_PLAN_H
#define LLVMDSDL_SUPPORT_SURFACE_PLAN_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"

#include "llvmdsdl/Support/BodyNaming.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SectionScopes.h"

namespace llvmdsdl
{

/// @brief What a scope of the plan is, as `dsdl.scope` states it.
enum class SurfaceScopeKind : std::uint8_t
{
    Root,
    Namespace,
    Module,
    Package,
    File,
    Type,
};

/// @brief What a declaration of the plan declares, as `dsdl.decl` states it.
enum class SurfaceDeclKind : std::uint8_t
{
    Field,
    Constant,
    Option,
    Entry,
    Accessor,
    Helper,
    Method,
    Wrapper,
    Alias,
    Tag,
    Import,
    Module,
    Guard,
};

/// @brief The class of names a declaration is made in.
enum class NameClass : std::uint8_t
{
    Value,
    Type,
    Module,
    Tag,
    Macro,
};

/// @brief A class of names as one language keeps them apart, where several classes may be one.
enum class NamePartition : std::uint8_t
{
    Values,
    Types,
    Modules,
    Tags,
    Macros,
};

/// @brief The partition @p nameClass falls in, as @p classes keep names apart.
/// @param[in] classes The language's name classes.
/// @param[in] nameClass The class.
/// @return The partition, or none where the language has no such class.
[[nodiscard]] std::optional<NamePartition> namePartition(const NameClasses& classes, NameClass nameClass);

/// @brief Whether a declaration is part of the output's interface.
enum class SurfaceVisibility : std::uint8_t
{
    Public,
    Private,
};

/// @brief Whether a name comes from a definition or from the generator.
enum class NameOrigin : std::uint8_t
{
    Definition,
    Generated,
};

/// @brief The entity a scope or a declaration stands for, by its DSDL identity.
struct SurfaceEntity final
{
    /// @brief The definition's key, `ns.Name.1.0`, which is also its schema's symbol.
    std::string schema;

    /// @brief `request` or `response` for a service's section; empty for a message.
    std::string section;

    /// @brief The field or constant, where the declaration names one.
    std::string member;

    /// @brief The lowered function, where the declaration names one.
    std::string function;

    friend bool operator==(const SurfaceEntity&, const SurfaceEntity&) = default;
};

/// @brief One entry of a scope: a scope nested in it, or a declaration made in it.
struct SurfaceItem final
{
    /// @brief Whether @ref index is a scope's; otherwise it is a declaration's.
    bool scope{};

    /// @brief The position in @ref SurfacePlan::scopes or @ref SurfacePlan::decls.
    std::size_t index{};
};

/// @brief One scope of the plan.
struct SurfaceScope final
{
    SurfaceScopeKind kind{};

    /// @brief The scope's name, as its language declares it.
    std::string name;

    /// @brief The file the scope is written to, relative to the output root; empty where none is
    ///        its own.
    std::string path;

    /// @brief The scope's parent; none for the root.
    std::optional<std::size_t> parent;

    /// @brief The definition a type scope declares.
    std::optional<SurfaceEntity> of;

    /// @brief The scopes and declarations in it, in the order they are allocated.
    std::vector<SurfaceItem> items;
};

/// @brief One declaration of the plan.
struct SurfaceDecl final
{
    /// @brief The name, as its language declares it.
    std::string name;

    SurfaceDeclKind kind{};

    NameClass nameClass{};

    SurfaceVisibility visibility{};

    NameOrigin origin{};

    /// @brief The entity the name stands for.
    std::optional<SurfaceEntity> of;

    /// @brief The scope the declaration is made in.
    std::size_t scope{};

    /// @brief For an import of a namespace, module or package, the scope it binds.
    std::optional<std::size_t> binds;
};

/// @brief A union option's declaration and the tag value that selects it.
struct OptionName final
{
    std::size_t   decl{};
    std::uint32_t tag{};
};

/// @brief Where one section's names are in the plan.
struct SectionNames final
{
    /// @brief `request` or `response` for a service's section; empty for a message.
    std::string section;

    /// @brief The section's type name.
    std::string typeName;

    /// @brief The type scope.
    std::size_t typeScope{};

    /// @brief Whether the section is a union.
    bool isUnion{};

    /// @brief Each DSDL field's declaration.
    llvm::StringMap<std::size_t> fields;

    /// @brief Each DSDL constant's declaration.
    llvm::StringMap<std::size_t> constants;

    /// @brief Each union option's tag, keyed by the option's DSDL name.
    llvm::StringMap<OptionName> options;
};

/// @brief Where one definition's names are in the plan.
struct DefinitionNames final
{
    /// @brief The definition's key, `ns.Name.1.0`.
    std::string key;

    /// @brief The definition's type name, which each section's is composed from.
    std::string typeName;

    /// @brief The stem of the file the definition is written to.
    std::string fileStem;

    /// @brief The file or module scope the definition is declared in.
    std::size_t fileScope{};

    /// @brief A service's own name, declared as an alias of its request; none for a message, or
    ///        where a section's type already has the name.
    std::optional<std::size_t> serviceAlias;

    /// @brief The namespace's components, each as the language names a namespace.
    std::vector<std::string> namespaceNames;

    std::optional<std::uint32_t> fixedPortId;

    /// @brief The message's one section, or a service's request and then its response.
    std::vector<SectionNames> sections;
};

/// @brief The scopes of one target's output and every name declared in them.
struct SurfacePlan final
{
    /// @brief Every scope; the first is the root.
    std::vector<SurfaceScope> scopes;

    std::vector<SurfaceDecl> decls;

    /// @brief Each definition's names, in the order the definitions were given.
    std::vector<DefinitionNames> definitions;
};

/// @brief One definition, as naming needs it.
struct DefinitionParts final
{
    DefinitionRef ref;

    std::optional<std::uint32_t> fixedPortId;

    /// @brief Whether the definition is a service, whose sections are its request and response.
    bool service{};

    /// @brief Whether the definition is deprecated.
    bool deprecated{};

    /// @brief A message's section, or a service's request.
    SectionParts request;

    /// @brief A service's response.
    std::optional<SectionParts> response;

    /// @brief The lowered functions the definition owns, in the module's order; empty for the
    ///        definition layer alone.
    std::vector<BodyParts> bodies;
};

/// @brief What a run fixes about the names it generates.
struct SurfaceOptions final
{
    /// @brief The generated package's name; empty where the caller names no package.
    std::string packageName;

    /// @brief Whether a type's name carries its version.
    TypeNameVersioning versioning{TypeNameVersioning::Unversioned};
};

/// @brief Allocates every name the definitions declare in one language.
///
/// The definition layer: the scopes that hold the definitions, their type scopes, and each field,
/// constant, array metadata constant and option tag. Within a scope the names are claimed in bands,
/// and a later band never moves a name an earlier one claimed: the language's reservations, then
/// the generator's own names, then the definitions' names in declaration order.
///
/// The body layer, where a definition's parts carry its lowered functions: each helper, and where
/// the bodies are compiled apart from their entry points, each body's link name. A helper is
/// claimed after the definition layer's names, in the band the language allocates helpers in.
///
/// Last, the local name a file imports each definition its fields hold under, where the language
/// imports a definition's type by name. A scope the language writes to a file of its own carries
/// the file's path.
/// @param[in] row The language's row.
/// @param[in] definitions The definitions, in the order the plan reports them.
/// @param[in] options What the run fixes.
/// @return The plan.
[[nodiscard]] SurfacePlan allocateSurface(const LanguageTraits&           row,
                                          llvm::ArrayRef<DefinitionParts> definitions,
                                          const SurfaceOptions&           options);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_SURFACE_PLAN_H
