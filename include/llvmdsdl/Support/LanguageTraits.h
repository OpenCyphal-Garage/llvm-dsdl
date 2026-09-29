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

#include <cstdint>

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

/// @brief How one scope keeps its names apart by what they name, so that a name may be declared once in
///        each class.
struct NameClasses final
{
    /// @brief Whether a type and a value may share a name.
    bool typesApartFromValues{};

    /// @brief Whether a namespace, module or package is named among the types of the scope that holds it.
    ///
    /// False where a module is reached by its path, and its name meets only the names of its siblings.
    bool modulesAmongTypes{};

    /// @brief Whether a structure's tag is a class of its own.
    bool tags{};

    /// @brief Whether the language has macros, whose one class spans the translation unit.
    bool macros{};

    /// @brief Whether a structure's fields are a class of their own, apart from its constants and
    ///        methods.
    bool fieldsApart{};

    /// @brief Whether a type's own name is among its members' names, so no member may take it.
    bool typeNameAmongMembers{};
};

/// @brief How a body reaches a member of the type it is declared in, where a lookup of the bare name
///        does not.
enum class MemberReach
{
    /// @brief It names none: a type's members are reached through a value.
    None,

    /// @brief Bare, through class-member lookup.
    Bare,

    /// @brief Through the name the language gives a body's own type, as `Self::`.
    SelfType,

    /// @brief Through the instance or the class the body is handed, as `self.` and `cls.`, and
    ///        through the type's name where it is handed neither.
    Instance,

    /// @brief Through the type's name, which is a value too.
    TypeName,
};

/// @brief How the language resolves a name written in a body, and how a qualified name is written.
///
/// The model `spellReference` repeats over a surface plan: which scopes a body sees, nearest first,
/// and the forms a name takes where the bare one does not reach the declaration.
struct Lookup final
{
    /// @brief What joins a qualifier to the name it qualifies; empty where a name is never qualified.
    llvm::StringRef separator;

    /// @brief What begins a path from the root; empty where the language writes none.
    llvm::StringRef rootPrefix;

    /// @brief Whether a path from the root is written without @ref rootPrefix where the root's first
    ///        component is not shadowed.
    bool rootPrefixOnlyWhenShadowed{};

    /// @brief Whether a body sees each namespace that encloses its own, nearest first.
    bool enclosingNamespaces{};

    /// @brief How a body reaches a member of its own type.
    MemberReach ownMembers{};

    /// @brief The name a body calls its own type by; empty where it writes the type's name.
    llvm::StringRef selfType;

    /// @brief The instance a method is handed; empty where the language hands none by name.
    llvm::StringRef selfInstance;

    /// @brief The class a class method is handed; empty where the language hands none by name.
    llvm::StringRef selfClass;
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

    /// @brief The identifiers the language reserves by their underscores.
    ReservedUnderscores reservedUnderscores{};

    /// @brief The classes one scope keeps names apart in.
    NameClasses nameClasses{};

    /// @brief How a name written in a body is resolved.
    Lookup lookup{};
};

/// @brief How a free function that reads or writes one member of a type is named.
enum class AccessorNaming
{
    /// @brief None is free: an accessor is a member of the type, or a method on it.
    None,

    /// @brief The type's name, the entry point join, the verb and the member, each ending in `_`:
    ///        `List_Request__get_path_`.
    Joined,

    /// @brief The type's name, the verb in title case and the member: `ListRequestGetPath`.
    Concatenated,

    /// @brief The verb, the type's name and the member with its first letter in upper case:
    ///        `getListRequestPath`.
    VerbFirst,
};

/// @brief How the free functions declared beside a section's type are named.
///
/// A free function is declared in the scope that holds the type, so its name carries the type's.
struct FreeFunctionNames final
{
    /// @brief What joins the type's name to an entry point, as in `List_Request__serialize_`.
    ///
    /// Empty where the entry points are members of the type or methods on it.
    llvm::StringRef entryPointJoin;

    /// @brief Whether the type is initialised through a free entry point as well.
    bool initializer{};

    /// @brief How a field's getter and setter are named.
    AccessorNaming accessors{};

    /// @brief Whether a union's option has a free test and selector, as in `Value__is_integer_`.
    bool unionOptionFunctions{};

    /// @brief What ends the name of the body a free entry point or accessor wraps, where the body is
    ///        compiled apart from it, as in `List_Request_0_2__serialize_ir_`.
    ///
    /// Empty where the body is the entry point itself.
    llvm::StringRef loweredBodySuffix;
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
    /// reports beside the name would double it; the manifest reports C's joined name as
    /// `qualified_type_name`. C++ is false as it always has been, and the reason
    /// once given for it -- that its emitter builds a namespace-qualified symbol of its own -- is not
    /// what `cppTypeName` does. Whether C++ should report is a question for the phase that takes C++;
    /// see `CLEAN_CODE.md`.
    bool typeNameReachesTheType{true};
};

/// @brief What encloses a service's sections.
enum class SectionEnclosure : std::uint8_t
{
    /// @brief Nothing: each section's type is declared beside the other, named after the service.
    None,

    /// @brief The definition's own module, so a section is named alone.
    Module,

    /// @brief A type named for the service, which holds the service's own facts, so a section is
    ///        named alone.
    ServiceType,
};

/// @brief How the output opens a DSDL namespace.
enum class NamespaceForm
{
    /// @brief It does not: the namespace is joined into each identifier.
    Joined,

    /// @brief As a namespace, which any number of files may add to.
    Namespace,

    /// @brief As a module, which a directory declares.
    Module,

    /// @brief As a package, which the files of one directory share.
    Package,
};

/// @brief How a lowered helper is named, and in what scope its name is allocated.
enum class HelperNaming
{
    /// @brief Under the link name of its section and version, as the bodies are compiled apart.
    LinkName,

    /// @brief By the definition's full name and version, in the namespace the definitions share.
    Binding,

    /// @brief In one scope for the package, each carrying its definition's type name.
    Package,

    /// @brief In one scope for the definition's own module.
    Module,
};

/// @brief How a file names what it takes from another definition's file.
enum class ImportNaming
{
    /// @brief It names nothing: it includes the other file, whose declarations are then its own.
    None,

    /// @brief It imports the other file's package, and names what it takes through the package.
    Package,

    /// @brief It imports the definition's type under a local name.
    Type,

    /// @brief It imports the definition's type under a local name, with the functions named after
    ///        the type.
    TypeAndFunctions,
};

/// @brief How a reference is spelt, where the language could spell it more than one way.
enum class Qualification
{
    /// @brief The shortest spelling the language's lookup resolves to the declaration.
    Shortest,

    /// @brief From the root, for a declaration outside the site's own type.
    Rooted,
};

/// @brief How the output composes a language's declarations today.
struct Composition final
{
    /// @brief How a definition's type name is composed.
    DefinitionNamePolicy definitionName{};

    /// @brief What joins a service's name to its section's, as in `List__Request`.
    llvm::StringRef sectionJoin;

    /// @brief What encloses a service's sections.
    SectionEnclosure sectionEnclosure{};

    /// @brief Whether the definitions of one DSDL namespace are generated into one scope.
    ///
    /// False where each definition and version is a module of its own, so two of them may spell a
    /// type the same way without meeting.
    bool definitionsShareNamespaceScope{};

    /// @brief How a DSDL namespace is opened.
    NamespaceForm namespaces{};

    /// @brief What ends the name of the file a definition's declarations are written to.
    llvm::StringRef fileExtension;

    /// @brief Whether that file's directories are the namespace's components as the language names
    ///        a namespace, rather than as DSDL writes them.
    bool directoriesProjected{};

    /// @brief The directory, under the output directory, the package's source files are written in;
    ///        empty where they are written in the output directory itself.
    llvm::StringRef sourceDirectory;

    /// @brief Whether the source files are written under the directories the package's name spells,
    ///        one per dotted component.
    bool packageDirectory{};

    /// @brief The file, in a namespace's directory, the namespace's own declarations are written to;
    ///        empty where the language writes none.
    llvm::StringRef namespaceFile;

    /// @brief The file, in the source directory, the package's own declarations are written to;
    ///        empty where the language writes none.
    llvm::StringRef rootFile;

    /// @brief How a file names what it takes from another definition's file.
    ImportNaming imports{};

    /// @brief How a lowered helper is named.
    HelperNaming helpers{};

    /// @brief How a reference is spelt.
    Qualification qualification{};

    /// @brief Whether a definition's file and a namespace's directory of one name are one module.
    ///
    /// Rust declares either as a `mod`, and Python imports either as its package's attribute.
    bool fileAndDirectoryAreOneModule{};

    /// @brief Whether a namespace is declared in the scope that holds its parent namespace's types.
    ///
    /// C++ declares `namespace Foo` beside `struct Foo`, and the two may not share a name.
    bool namespaceAndTypeShareScope{};

    /// @brief Where a type's constants are declared.
    ConstantsScope constants{};

    /// @brief Where a service's own constants are declared: in the type that encloses its sections,
    ///        named as a type's constants are, or beside its section types, named after the service
    ///        as a constant of that scope is named after its type's.
    ConstantsScope serviceConstants{};

    /// @brief Whether a type's constants, its array metadata and its option tags are macros.
    bool constantsAreMacros{};

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

    /// @brief How the free functions beside a section's type are named.
    FreeFunctionNames freeFunctions{};
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
