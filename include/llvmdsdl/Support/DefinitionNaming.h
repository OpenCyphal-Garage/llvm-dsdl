//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Names composed from a definition's identity rather than from a single DSDL name.
///
/// `NamingPolicy.h` answers how one name is spelled in one language. This answers the names built
/// from a definition's *parts*.
///
/// The signatures take parts rather than a `DiscoveredDefinition` on purpose: `llvmdsdlFrontend`
/// links only `llvmdsdlSupport`, so anything phrased in terms of the frontend's own types would be
/// unusable from `Discovery`, which is one of the places that most needs to agree with the emitters.
/// Overloads taking the richer types live in `CodeGen/DefinitionPathProjection.h`.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_DEFINITION_NAMING_H
#define LLVMDSDL_SUPPORT_DEFINITION_NAMING_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

/// @brief Whether generated type names carry the definition's version.
///
/// This is a property of the *consuming* code, not of the corpus. Code that speaks one version of a type reads better
/// with `p::ns::Bar`; code that deliberately handles two versions side by side needs `Bar_1_0` and `Bar_2_0` to keep
/// them apart in its own source. Under either, the name follows from the definition alone -- neither depends on what
/// else happened to be in the invocation.
enum class TypeNameVersioning : std::uint8_t
{
    /// @brief The version is not part of the type name.
    ///
    /// Two versions of one DSDL type then reach one identifier, and what that costs depends on what
    /// scopes the type. Rust, TypeScript and Python give each version its own module and are
    /// unaffected. C and C++ share a scope across versions, so the two collide only if a consumer
    /// brings both into one translation unit: C's headers detect it and stop with a message naming
    /// the flag, and C++ reports the redefinition. Go shares a package across versions, so it cannot
    /// be generated at all and says so.
    Unversioned,

    /// @brief The version is part of the type name, so every version can be used at once.
    Versioned,
};

/// @brief A definition's identity, as naming reads it.
struct DefinitionRef final
{
    /// @brief The namespace's components, outermost first.
    std::vector<std::string> namespaceComponents;

    /// @brief The unqualified DSDL type name.
    std::string shortName;

    std::uint32_t majorVersion{};

    std::uint32_t minorVersion{};
};

/// @brief The key a definition is reported and referred to by: `ns.Name.1.0`, which is also its
///        schema's symbol.
/// @param[in] ref The definition.
/// @return The key.
[[nodiscard]] std::string renderDefinitionKey(const DefinitionRef& ref);

/// @brief Returns how @p language composes a definition's type name.
/// @param[in] language Naming language.
/// @return The policy, valid for the process lifetime.
[[nodiscard]] const DefinitionNamePolicy& definitionNamePolicy(Language language);

/// @brief Renders the type name for one definition in @p language.
/// @param[in] language Naming language.
/// @param[in] namespaceComponents Namespace components, outermost first.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @param[in] versioning Whether the version is part of the name.
/// @return The type name.
[[nodiscard]] std::string renderDefinitionTypeName(Language                    language,
                                                   llvm::ArrayRef<std::string> namespaceComponents,
                                                   llvm::StringRef             shortName,
                                                   std::uint32_t               majorVersion,
                                                   std::uint32_t               minorVersion,
                                                   TypeNameVersioning          versioning);

/// @brief The name a definition's file stem is projected from: the short name and the version.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return The name, as in `Break_1_0`.
[[nodiscard]] std::string renderDefinitionFileStemSource(llvm::StringRef shortName,
                                                         std::uint32_t   majorVersion,
                                                         std::uint32_t   minorVersion);

/// @brief Renders the output file stem for one definition, without an extension.
///
/// Callers append their own extension and any role suffix (`_abi`, `_c_shim`). The stem is projected
/// under @ref IdentifierRole::FileStem, which is verbatim in C and C++ and snake_case elsewhere.
/// @param[in] language Naming language.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return The stem, and whether projecting it escaped it.
[[nodiscard]] ProjectedIdentifier renderDefinitionFileStemDetailed(Language        language,
                                                                   llvm::StringRef shortName,
                                                                   std::uint32_t   majorVersion,
                                                                   std::uint32_t   minorVersion);

/// @brief @ref renderDefinitionFileStemDetailed's identifier.
/// @param[in] language Naming language.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return The file stem.
[[nodiscard]] std::string renderDefinitionFileStem(Language        language,
                                                   llvm::StringRef shortName,
                                                   std::uint32_t   majorVersion,
                                                   std::uint32_t   minorVersion);

/// @brief Names the generated type of one section of a definition.
///
/// A message is its own type and takes @p baseTypeName unchanged. A service declares a type per
/// section, and where that type goes depends on what the language scopes it with. C, C++, Go,
/// TypeScript and Python reach both sections through the name of the service, so the section is a
/// suffix on it. Rust reaches them through the definition's own module, which names the service
/// already, so the section alone is the name and `list_0_2::Request` is the whole path.
///
/// This exists so that the emitter, the frontend's collision check and the naming manifest compose
/// one answer rather than three.
/// @param[in] language Naming language.
/// @param[in] baseTypeName The definition's type name, from @ref renderDefinitionTypeName.
/// @param[in] sectionName Section name: `request`, `response`, or empty for a message.
/// @return The section's type name.
[[nodiscard]] std::string renderSectionTypeName(Language        language,
                                                llvm::StringRef baseTypeName,
                                                llvm::StringRef sectionName);

/// @brief An operation a section's type is serialised or initialised through.
enum class EntryPoint : std::uint8_t
{
    Serialize,
    Deserialize,
    Initialize,
    AppendWireImage,
    WireImage,
    ReadWireImage,
    FromWireImage,
};

/// @brief What a free function beside a section's type does with one of its members.
enum class AccessorVerb : std::uint8_t
{
    /// @brief Reads a field.
    Get,

    /// @brief Writes a field.
    Set,

    /// @brief Tests whether a union holds an option.
    Is,

    /// @brief Makes an option the one a union holds.
    Select,
};

/// @brief Names a free entry point of a section's type.
///
/// `List_Request__serialize_` in C and `List_Request_serialize_` in C++. The emitters, the symbols
/// stamped on a plan and the shared-scope collision check each name one through this.
/// @param[in] language Naming language, whose row has an entry point join.
/// @param[in] typeName The section's type name, from @ref renderSectionTypeName.
/// @param[in] entryPoint The operation.
/// @return The function's name.
[[nodiscard]] std::string renderEntryPointName(Language language, llvm::StringRef typeName, EntryPoint entryPoint);

/// @brief Names a free function that reads, writes, tests or selects one member of a section's type.
///
/// `List_Request__get_path_` in C, `ListRequestGetPath` in Go and `getListRequestPath` in TypeScript.
/// @param[in] language Naming language, whose row names its accessors as free functions.
/// @param[in] typeName The section's type name, from @ref renderSectionTypeName.
/// @param[in] verb What the function does.
/// @param[in] member The member's name as the language declares it.
/// @return The function's name.
[[nodiscard]] std::string renderAccessorName(Language        language,
                                             llvm::StringRef typeName,
                                             AccessorVerb    verb,
                                             llvm::StringRef member);

/// @brief Names the body a free entry point wraps, where the body is compiled apart from it.
///
/// `List_Request_0_2__serialize_ir_` in C. The body is linked under this name, so it carries the
/// version whatever the type name does: two versions of one definition are two bodies to a linker.
/// @param[in] language Naming language, whose row has a lowered body suffix.
/// @param[in] versionedTypeName The section's type name under @ref TypeNameVersioning::Versioned.
/// @param[in] entryPoint The operation.
/// @return The body's name.
[[nodiscard]] std::string renderLoweredEntryPointName(Language        language,
                                                      llvm::StringRef versionedTypeName,
                                                      EntryPoint      entryPoint);

/// @brief Names the body a free accessor wraps, where the body is compiled apart from it.
///
/// `List_Request_0_2__get_path_ir_` in C.
/// @param[in] language Naming language, whose row has a lowered body suffix.
/// @param[in] versionedTypeName The section's type name under @ref TypeNameVersioning::Versioned.
/// @param[in] verb What the accessor does.
/// @param[in] member The member as the plan names it: a DSDL field's name, or the union's tag.
/// @return The body's name.
[[nodiscard]] std::string renderLoweredAccessorName(Language        language,
                                                    llvm::StringRef versionedTypeName,
                                                    AccessorVerb    verb,
                                                    llvm::StringRef member);

/// @brief Names a constant declared beside a type, in the scope that encloses it.
///
/// C declares every constant of a type this way, `List_Request_EXTENT_BYTES_`, and C++ the
/// constants of a service's own name, `List_FULL_NAME`.
/// @param[in] typeName The type's name.
/// @param[in] constant The constant's name, with any suffix the language puts on it.
/// @return The constant's name.
[[nodiscard]] std::string renderEnclosedConstantName(llvm::StringRef typeName, llvm::StringRef constant);

/// @brief Names a section's constant, or its union's option tag, as the language declares it.
///
/// Where the language declares a type's constants in the type's own scope, the declared name is the
/// one the section's scope allocates. C declares them in the scope enclosing the type, and TypeScript
/// and Python at module scope, each carrying the type's name: `ns__List__Request_EXTENT_BYTES_`,
/// `LIST_REQUEST_EXTENT_BYTES`. A language that declares them in the package's scope composes the
/// whole name in that scope, and has no use for this.
/// @param[in] language The language.
/// @param[in] sectionTypeName The section's type name.
/// @param[in] allocated The name the section's constant scope allocates.
/// @return The declared name.
[[nodiscard]] std::string renderDeclaredConstantName(Language        language,
                                                     llvm::StringRef sectionTypeName,
                                                     llvm::StringRef allocated);

/// @brief Renders how generated C names a composite type where it is used: `struct <typeName>`.
///
/// The typedef carries `__attribute__((deprecated))` when the definition is deprecated; the tag
/// carries no attribute. Naming the type through the tag keeps generated code -- serialiser
/// signatures, implementations, embedding structs -- clean under `-Werror`, while user code naming
/// the typedef is diagnosed. The header emitter, the plan-body builder and the object lowering all
/// agree on this spelling through this one function.
/// @param[in] typeName The C typedef name, which is also the struct tag.
/// @return The tag spelling.
[[nodiscard]] std::string renderCTagSpelling(llvm::StringRef typeName);

/// @brief Renders the name a C++ or Rust definition's struct is declared under.
///
/// A deprecated definition is declared as `<typeName>_`, and `<typeName>` becomes a deprecated alias
/// of it: `using X [[deprecated]] = X_;`, `#[deprecated] pub type X = X_;`. Generated code names the
/// struct, so its own prototypes, impl blocks, free functions and embedding structs never mention the
/// deprecated name; user code names the alias and is diagnosed. Deprecating the struct itself cannot
/// achieve this: GCC reports every mention outside the struct's own members and rustc every mention
/// including its impl blocks, from a deprecated context or not.
/// @param[in] typeName The public type name.
/// @param[in] deprecated Whether the definition is deprecated.
/// @return The declared name.
[[nodiscard]] std::string renderDeclaredTypeName(llvm::StringRef typeName, bool deprecated);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_DEFINITION_NAMING_H
