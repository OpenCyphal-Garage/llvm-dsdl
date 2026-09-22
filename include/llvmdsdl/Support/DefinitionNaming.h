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

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

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
    /// brings both into one translation unit -- which the generated headers detect and refuse. Go
    /// shares a package across versions, so it cannot be generated at all and says so.
    Unversioned,

    /// @brief The version is part of the type name, so every version can be used at once.
    Versioned,
};

/// @brief How one language composes a definition's type name.
struct DefinitionNamePolicy final
{
    /// @brief Separator joining the namespace components into the type name.
    ///
    /// Empty where the namespace is carried by the language instead -- C++ has real namespaces, and
    /// Go, TypeScript and Python put the type in a per-namespace module.
    llvm::StringRef namespaceJoin;

    /// @brief Whether to re-project the whole composed name once it has been assembled.
    ///
    /// A language that flattens the namespace into the identifier re-projects, so that the joined
    /// result is checked against the language a second time rather than only its parts.
    bool reprojectComposed{};

    /// @brief Whether @ref TypeNameVersioning::Versioned puts the version in the type name.
    ///
    /// Rust reaches a definition through a module named for the definition and its version, so two
    /// versions are already two paths and the name has nothing to add. The suffix would also be a
    /// run of underscores in a position where `non_camel_case_types` reports one.
    bool versionInTypeName{true};

    /// @brief Whether a consumer can reach the generated type from this name and the namespace.
    ///
    /// The naming manifest reports the type name under this, and the naming golden pins it, so the
    /// answer is stated once here rather than by each of them. It is true where the language carries
    /// the namespace itself and the name is the definition's own: Rust in a module, Go, TypeScript
    /// and Python in a per-namespace one.
    ///
    /// C is false because its namespace is joined into the identifier, so the namespace the manifest
    /// reports beside the name would double it. C++ is false as it always has been, and the reason
    /// once given for it -- that its emitter builds a namespace-qualified symbol of its own -- is not
    /// what `cppTypeName` does. Whether C++ should report is a question for the phase that takes C++;
    /// see `CLEAN_CODE.md`.
    bool typeNameReachesTheType{true};
};

/// @brief Returns how @p language composes a definition's type name.
/// @param[in] language Naming language.
/// @return The policy, valid for the process lifetime.
[[nodiscard]] const DefinitionNamePolicy& definitionNamePolicy(CodegenNamingLanguage language);

/// @brief Renders the type name for one definition in @p language.
/// @param[in] language Naming language.
/// @param[in] namespaceComponents Namespace components, outermost first.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @param[in] versioning Whether the version is part of the name.
/// @return The type name.
[[nodiscard]] std::string renderDefinitionTypeName(CodegenNamingLanguage       language,
                                                   llvm::ArrayRef<std::string> namespaceComponents,
                                                   llvm::StringRef             shortName,
                                                   std::uint32_t               majorVersion,
                                                   std::uint32_t               minorVersion,
                                                   TypeNameVersioning          versioning);

/// @brief Renders the output file stem for one definition, without an extension.
///
/// Callers append their own extension and any role suffix (`_abi`, `_c_shim`). The stem is projected
/// under @ref IdentifierRole::FileStem, which is verbatim in C and C++ and snake_case elsewhere.
/// @param[in] language Naming language.
/// @param[in] shortName Unqualified DSDL type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return The stem.
[[nodiscard]] std::string renderDefinitionFileStem(CodegenNamingLanguage language,
                                                   llvm::StringRef       shortName,
                                                   std::uint32_t         majorVersion,
                                                   std::uint32_t         minorVersion);

/// @brief Renders an include-guard macro for one definition's generated header.
///
/// Every generated header guards on `<prefix><FULL_NAME>_<major>_<minor><suffix>`, upper-cased and
/// escaped as a macro token. The prefix is what keeps one definition's several headers apart -- the
/// object backend emits four for a single type.
/// @param[in] language Naming language.
/// @param[in] prefix Leading discriminator, including its trailing separator.
/// @param[in] fullName Dot-separated DSDL full name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @param[in] suffix Trailing discriminator, including its leading separator.
/// @return The guard macro.
[[nodiscard]] std::string renderIncludeGuard(CodegenNamingLanguage language,
                                             llvm::StringRef       prefix,
                                             llvm::StringRef       fullName,
                                             std::uint32_t         majorVersion,
                                             std::uint32_t         minorVersion,
                                             llvm::StringRef       suffix);

/// @brief Renders the sentinel macros that detect two versions of one type in one translation unit.
///
/// Under @ref TypeNameVersioning::Unversioned two versions of a DSDL type reach one identifier. In C
/// and C++ that is legal to *generate* -- the two live in separate headers, and generating both is
/// ordinary -- and only breaks if a consumer includes both. The first header to be included defines
/// the generic sentinel and its own specific one; a header for a different version then finds the
/// generic set and its own missing, and stops with a message naming the flag rather than a cascade
/// of redefinition errors from deep inside generated code.
/// @param[in] language Naming language.
/// @param[in] fullName Dot-separated DSDL full name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return A pair of macro names: the generic one, then the one specific to this version.
[[nodiscard]] std::pair<std::string, std::string> renderVersionSentinelMacros(CodegenNamingLanguage language,
                                                                              llvm::StringRef       fullName,
                                                                              std::uint32_t         majorVersion,
                                                                              std::uint32_t         minorVersion);

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
[[nodiscard]] std::string renderSectionTypeName(CodegenNamingLanguage language,
                                                llvm::StringRef       baseTypeName,
                                                llvm::StringRef       sectionName);

/// @brief The prefix every helper symbol `build-dsdl-plan-bodies` synthesises begins with.
///
/// The pass appends the helper's kind, the schema symbol and a role suffix to reach a whole symbol.
/// A backend that shortens the symbol for its own scope strips this from the front, so the pass that
/// writes it and the backends that read it name it here rather than each spelling the literal.
inline constexpr llvm::StringLiteral kPlanHelperSymbolPrefix{"llvmdsdl_plan_"};

/// @brief Renders the linkage-symbol base for one definition.
///
/// `uavcan.node.Heartbeat` at 1.0 gives `uavcan_node_Heartbeat_1_0`. Callers append
/// @ref renderSectionSymbolSuffix and their own role suffix to reach a whole symbol.
///
/// This is the one composed name whose copies drift silently. The C backend's generated
/// implementation defines `<base><section>__serialize_ir_` and its header declares it, from two
/// different libraries; a difference between them is a link error at best and, for the inline
/// wrappers, nothing at all until someone links two versions together.
/// @param[in] fullName Dot-separated DSDL full name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return The symbol base.
[[nodiscard]] std::string renderDefinitionSymbolBase(llvm::StringRef fullName,
                                                     std::uint32_t   majorVersion,
                                                     std::uint32_t   minorVersion);

/// @brief Renders the suffix distinguishing a service section's symbols from a message's.
///
/// A message has no section and takes no suffix, so its symbols keep the shape they had before
/// services existed.
/// @param[in] sectionName Section name: `request`, `response`, or empty for a message.
/// @return The suffix, or an empty string.
[[nodiscard]] std::string renderSectionSymbolSuffix(llvm::StringRef sectionName);

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
