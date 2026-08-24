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
/// from a definition's *parts* -- its full name, its short name, its version -- which is a different
/// question and, until this file existed, one that every caller answered for itself.
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

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/NamingPolicy.h"

namespace llvmdsdl
{

/// @brief Whether a language's type name carries the definition's version.
enum class TypeNameVersioning : std::uint8_t
{
    /// @brief Never suffixed. Two versions of one DSDL type therefore reach one identifier.
    Never,
    /// @brief Suffixed only when the short name has more than one version in the compiled set.
    ///
    /// This makes the identifier a function of the invocation rather than of the definition: adding
    /// a sibling version renames the type a caller was already writing against.
    OnlyWhenAmbiguous,
    /// @brief Always suffixed, so the identifier follows from the definition alone.
    Always,
};

/// @brief How one language composes a definition's type name.
struct DefinitionNamePolicy final
{
    /// @brief Separator joining the namespace components into the type name.
    ///
    /// Empty where the namespace is carried by the language instead -- C++ has real namespaces, and
    /// Go, TypeScript and Python put the type in a per-namespace module.
    llvm::StringRef namespaceJoin;

    /// @brief Whether the version is part of the type name.
    TypeNameVersioning versioning;

    /// @brief Whether to re-project the whole composed name once it has been assembled.
    ///
    /// Rust flattens the namespace into the identifier and then re-projects, so that the joined
    /// result is checked against the language a second time rather than only its parts.
    bool reprojectComposed;
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
/// @param[in] shortNameIsAmbiguous True when another version of this type is in the compiled set.
///            Consulted only under @ref TypeNameVersioning::OnlyWhenAmbiguous.
/// @return The type name.
[[nodiscard]] std::string renderDefinitionTypeName(CodegenNamingLanguage       language,
                                                   llvm::ArrayRef<std::string> namespaceComponents,
                                                   llvm::StringRef             shortName,
                                                   std::uint32_t               majorVersion,
                                                   std::uint32_t               minorVersion,
                                                   bool                        shortNameIsAmbiguous);

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

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_DEFINITION_NAMING_H
