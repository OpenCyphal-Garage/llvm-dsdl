//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The classification: one row per language dsdlc generates.
///
/// A row states three things. What the language can express, which is the table in
/// `CLEAN_CODE.md` and a claim about the language rather than about this compiler. What the
/// generated interface hands a body, which the lowering folds against. And how the output composes
/// its declarations today, which the per-language phases of `CLEAN_CODE.md` move towards what the
/// language can express; where the two differ, the difference is that work.
///
/// Code that needs any of these reads the row. A language compared by name anywhere else is what
/// `tools/check_language_classification.py` refuses.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_LANGUAGE_TRAITS_H
#define LLVMDSDL_SUPPORT_LANGUAGE_TRAITS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/BodyInterface.h"
#include "llvmdsdl/Support/Language.h"

namespace llvmdsdl
{

/// @brief The scopes a language opens below the file.
struct ScopesBelowFile final
{
    /// @brief A namespace, which any number of files may add to.
    bool namespaces{};

    /// @brief A class or struct, which scopes the names declared in it.
    bool classes{};

    /// @brief A module a file or a block declares.
    bool modules{};
};

/// @brief How a language attaches an operation to a type.
enum class MethodForm
{
    /// @brief It does not: an operation is a free function.
    None,

    /// @brief As a member declared in the type.
    Member,

    /// @brief In an implementation block apart from the type's declaration.
    Impl,

    /// @brief By a receiver, declared anywhere in the type's package.
    Receiver,
};

/// @brief How a language keeps a declaration out of what its users can reach.
enum class InternalLinkage
{
    /// @brief `static` at file scope.
    Static,

    /// @brief A private member of the type.
    PrivateMember,

    /// @brief Private unless marked otherwise.
    PrivateByDefault,

    /// @brief The case of the name's first letter.
    LowerCaseInitial,

    /// @brief A leading underscore, by convention.
    UnderscorePrefix,

    /// @brief Left out of what the module exports.
    NotExported,
};

/// @brief How a language's idiom reports that an operation failed.
enum class ErrorConvention
{
    /// @brief A status code the caller tests.
    StatusCode,

    /// @brief A value that holds either the result or the error.
    Result,

    /// @brief The result beside an error value.
    ValueAndError,

    /// @brief An exception.
    Exception,
};

/// @brief Where a type's constants are declared.
enum class ConstantsScope
{
    /// @brief The scope enclosing the type, each name carrying the type's.
    Enclosing,

    /// @brief The type's own scope.
    Type,

    /// @brief The package's scope, each name carrying the type's.
    Package,

    /// @brief The module's scope, each name carrying the type's.
    Module,
};

/// @brief The identifiers a language reserves to the implementation by their underscores.
enum class ReservedUnderscores
{
    /// @brief None.
    None,

    /// @brief One beginning `__`, or `_` and a capital.
    Leading,

    /// @brief Those, and one holding `__` anywhere.
    LeadingAndInterior,
};

/// @brief What a language can express.
///
/// The table in `CLEAN_CODE.md`, *Language classification*. A column grows here when a phase needs
/// to read it; a preference, such as which containers a profile uses, stays with the profile.
struct Classification final
{
    /// @brief The scopes the language opens below the file.
    ScopesBelowFile scopes{};

    /// @brief Whether a type can be declared inside another.
    bool nestedTypes{};

    /// @brief How an operation attaches to a type.
    MethodForm methods{};

    /// @brief How a declaration is kept from the language's users.
    InternalLinkage internalLinkage{};

    /// @brief How the language's idiom reports a failure.
    ErrorConvention errors{};

    /// @brief Where the language declares a type's constants.
    ConstantsScope typeConstants{};

    /// @brief Whether a type's constants and its fields are one namespace, so the two can collide.
    bool constantsShareFieldNamespace{};

    /// @brief The identifiers the language reserves by their underscores.
    ReservedUnderscores reservedUnderscores{};
};

/// @brief How one language composes a definition's type name.
struct DefinitionNamePolicy final
{
    /// @brief Separator joining the namespace components into the type name.
    ///
    /// Empty where the language carries the namespace itself.
    llvm::StringRef namespaceJoin;

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

/// @brief How the output composes a language's declarations today.
struct Composition final
{
    /// @brief How a definition's type name is composed.
    DefinitionNamePolicy definitionName{};

    /// @brief What joins a service's name to its section's, as in `List__Request`.
    llvm::StringRef sectionJoin;

    /// @brief Whether a section is named alone, since the definition's own module already encloses it.
    bool sectionNamedAlone{};

    /// @brief Whether the definitions of one DSDL namespace are generated into one scope.
    ///
    /// False where each definition and version is a module of its own, so two of them may spell a
    /// type the same way without meeting.
    bool definitionsShareNamespaceScope{};

    /// @brief Where a type's constants are declared.
    ConstantsScope constants{};

    /// @brief What ends the name of a constant the generator composes, such as `_OPTION_TAG`.
    ///
    /// C's generated constants are macros ending in `_`, which keeps them apart from a DSDL
    /// constant, since DSDL reserves only names that both start and end with one.
    llvm::StringRef generatedConstantSuffix;

    /// @brief Whether each array field has constants stating its capacity and whether it varies.
    ///
    /// A language whose containers state their own capacity has no such constant.
    bool arrayMetadataConstants{};

    /// @brief Whether a deprecated definition's type is declared under a name of its own.
    bool deprecatedTypeDeclaredApart{};
};

/// @brief One language's row.
struct LanguageTraits final
{
    /// @brief The language.
    Language language{};

    /// @brief Its `--target-language` spelling, which diagnostics use.
    llvm::StringRef name;

    /// @brief What it can express.
    Classification classification{};

    /// @brief What its generated interface hands a body.
    BodyInterface body{};

    /// @brief How the output composes its declarations today.
    Composition composition{};
};

/// @brief Every row, in the order of @ref Language.
/// @return The table, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<LanguageTraits> allLanguageTraits();

/// @brief The row of @p language.
/// @param[in] language The language.
/// @return The row, valid for the process lifetime.
[[nodiscard]] const LanguageTraits& languageTraits(Language language);

/// @brief The row whose `--target-language` spelling is @p name.
/// @param[in] name The spelling.
/// @return The row, or null where no language is spelt so.
[[nodiscard]] const LanguageTraits* languageTraitsNamed(llvm::StringRef name);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_LANGUAGE_TRAITS_H
