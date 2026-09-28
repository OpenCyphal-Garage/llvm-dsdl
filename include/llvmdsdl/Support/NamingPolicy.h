//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared naming-policy helpers for backend code generation.
///
/// This interface centralises language-specific identifier sanitization and
/// common case projections (snake/pascal/upper-snake) used by emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_NAMING_POLICY_H
#define LLVMDSDL_SUPPORT_NAMING_POLICY_H

#include <optional>
#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/GeneratedFact.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/PlanSymbol.h"

namespace llvmdsdl
{

/// @brief What an identifier is going to be used as.
///
/// Every language answers the same question -- how do I name a field? a constant? a file? -- with a
/// different projection and a different reserved set, so the role is the axis the policy table is
/// indexed on. See docs/development/identifier-stropping.md section 4.1.
enum class IdentifierRole
{
    /// @brief A struct, class, or type alias name.
    TypeName,

    /// @brief A struct member, object key, or attribute name.
    FieldName,

    /// @brief A constant, enumerator, or `#define` value name.
    ConstantName,

    /// @brief A generated free function or method name.
    FunctionName,

    /// @brief A generated function that only the definition's own code calls.
    ///
    /// Separate from @ref FunctionName because two of these languages say in the name itself that a
    /// function is not part of the package's surface: Go by the case of its first letter, Python by
    /// a leading underscore. Rust and TypeScript say it by leaving off `pub` and `export`, and
    /// spell the name as they would any other.
    InternalFunctionName,

    /// @brief A local or parameter inside a generated body.
    LocalName,

    /// @brief A namespace, package, or module identifier.
    NamespaceName,

    /// @brief An output file name without its extension.
    FileStem,

    /// @brief A C/C++ preprocessor token, including the include guard.
    MacroName,
};

/// @brief The case projection a role receives before escaping.
enum class CaseStyle
{
    /// @brief Leave the source spelling alone.
    Preserve,

    /// @brief Fold to snake_case (@ref canonicalSnakeCase).
    Snake,

    /// @brief Fold to PascalCase.
    Pascal,

    /// @brief Fold to camelCase.
    Camel,

    /// @brief Fold to the PascalCase Go exports things under.
    ///
    /// Distinct from @ref Pascal in what it does to a word it is given: `Pascal` upper-cases a
    /// word's first letter and leaves the rest, so `FULL_NAME` reaches `FULLNAME`. This recases
    /// each word, and upper-cases the ones Go writes as initialisms, so `FULL_NAME` reaches
    /// `FullName` and `unique_id` reaches `UniqueID`, which is what `ST1003` asks for.
    GoExported,

    /// @brief @ref GoExported with its first word in lower case, which is how Go says unexported.
    ///
    /// The whole of the first word is lowered, an initialism included, so `id_list` reaches
    /// `idList` rather than `iDList`.
    GoUnexported,
};

/// @brief How one role is named in one language.
///
/// The three booleans are independent of the case style. The backends use every combination: a
/// C macro token is escaped but not stropped, a C header stem is neither, and a Go constant
/// is both and then uppercased.
struct RolePolicy final
{
    /// @brief Case projection applied first.
    CaseStyle caseStyle = CaseStyle::Preserve;

    /// @brief Whether characters outside `[A-Za-z0-9_]` and a leading digit are escaped.
    bool escape = true;

    /// @brief Whether a result that collides with a language keyword is escaped.
    bool strop = true;

    /// @brief Whether the final result is upper-cased (constants and macros).
    bool upper = false;
};

/// @brief The complete naming policy for one target language.
class LanguageNamingPolicy final
{
public:
    /// @brief Builds the policy for @p language.
    /// @param[in] language Naming language.
    explicit LanguageNamingPolicy(Language language);

    /// @brief Returns how @p role is named in this language.
    /// @param[in] role Identifier role.
    /// @return The role's policy.
    [[nodiscard]] const RolePolicy& roleFor(IdentifierRole role) const;

    /// @brief Names the generated code already claims for @p role in this language.
    ///
    /// A DSDL attribute that projects onto one of these would redeclare something the backend emits
    /// for every type, so the projection escapes it. Contrast @ref NamingScope's reservations, which
    /// one particular scope claims; these are claimed for every type the backend emits, which is why
    /// they are policy rather than context.
    /// @param[in] role Identifier role.
    /// @return The claimed names, or an empty list.
    [[nodiscard]] llvm::ArrayRef<llvm::StringRef> runtimeOwned(IdentifierRole role) const;

    /// @brief Every keyword in this language, in unspecified order.
    ///
    /// Exists so a test can state a property of the whole table rather than probe names it already
    /// guessed: the one-pass escape in the pipeline is only sound while no keyword ends in `_`, and
    /// that is a claim about all of them.
    /// @return The language's keywords.
    [[nodiscard]] std::vector<llvm::StringRef> keywords() const;

private:
    Language language_;
};

/// @brief Returns the naming policy for @p language.
/// @param[in] language Naming language.
/// @return A reference to the language's policy, valid for the process lifetime.
[[nodiscard]] const LanguageNamingPolicy& codegenNamingPolicy(Language language);

/// @brief A projected identifier and whether producing it needed an escape.
struct ProjectedIdentifier final
{
    /// @brief The identifier to emit.
    std::string identifier;

    /// @brief True when a keyword, a claimed name, or an illegal character forced a change beyond
    ///        the role's case projection.
    ///
    /// Case projection alone does not count: every backend renames `FooBar` to `foo_bar` or
    /// `FOO_BAR` as a matter of course, and reporting that would bury the cases a reader needs to
    /// know about under the ones they already expect.
    bool escaped = false;

    /// @brief True when the name landed in a namespace the language reserves and had to be encoded.
    ///
    /// C reserves every identifier beginning with two underscores, or with one underscore and a
    /// capital; C++ reserves any identifier containing two underscores anywhere. A trailing `_`
    /// cannot repair either, so the offending underscores are encoded instead.
    ///
    /// The encoding is always applied, which keeps this function total and its result legal in every
    /// target. Whether the *definition* is accepted is a separate question, asked once per run by
    /// the driver: by default a name that needs this is rejected, and `--encode-reserved-identifiers`
    /// is what accepts it. Emitters therefore never have to know which mode is in force.
    bool reservedNamespaceEncoded = false;
};

/// @brief Projects @p name and reports whether an escape was needed.
/// @param[in] language Naming language.
/// @param[in] role What the identifier will be used as.
/// @param[in] name Source name.
/// @return The identifier and the escape flag.
[[nodiscard]] ProjectedIdentifier codegenProjectIdentifierDetailed(Language        language,
                                                                   IdentifierRole  role,
                                                                   llvm::StringRef name);

/// @brief Projects @p name into the identifier @p role calls for in @p language.
///
/// This is the shared pipeline: case projection, then escaping, then stropping, then the optional
/// final upper-casing. The case-explicit helpers below are the same pipeline with the style passed
/// in rather than looked up.
/// @param[in] language Naming language.
/// @param[in] role What the identifier will be used as.
/// @param[in] name Source name.
/// @return The language-safe identifier for that role.
[[nodiscard]] std::string codegenProjectIdentifier(Language language, IdentifierRole role, llvm::StringRef name);

/// @brief Returns true when an identifier is a keyword in the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return True when the identifier is reserved.
bool codegenIsKeyword(Language language, llvm::StringRef name);

/// @brief True when @p identifier lies in a namespace @p language reserves for the implementation.
///
/// C reserves a leading `__` and a leading `_` before a capital; C++ reserves those and any
/// identifier containing `__`. The other four reserve no such namespace and always answer false.
///
/// The projection escapes such names on the way in. This asks the question of an identifier that is
/// already projected; a caller composing one -- a disambiguation suffix, a generated prefix -- uses it
/// to avoid composing its way into the namespace.
/// @param[in] language Naming language.
/// @param[in] identifier An identifier, already projected.
/// @return True when it is reserved.
[[nodiscard]] bool codegenIsReservedNamespaceIdentifier(Language language, llvm::StringRef identifier);

/// @brief Sanitizes one identifier for the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return Language-safe identifier.
std::string codegenSanitizeIdentifier(Language language, llvm::StringRef name);

/// @brief Repairs an identifier that would otherwise begin with a digit.
///
/// No target accepts one, so the repair is the same everywhere and takes no language. The
/// projections below apply it as one of their stages; a caller that has already cased a name and
/// only needs it to be spellable calls this alone.
/// @param[in] name Identifier to repair.
/// @return @p name, with a leading underscore where it began with a digit.
[[nodiscard]] std::string escapeIdentifierStart(llvm::StringRef name);

/// @brief Projects text into snake_case and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe snake_case identifier.
std::string codegenToSnakeCaseIdentifier(Language language, llvm::StringRef name);

/// @brief Projects text into PascalCase and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe PascalCase identifier.
std::string codegenToPascalCaseIdentifier(Language language, llvm::StringRef name);

/// @brief Projects text into UPPER_SNAKE_CASE and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe UPPER_SNAKE_CASE identifier.
std::string codegenToUpperSnakeCaseIdentifier(Language language, llvm::StringRef name);

/// @brief The tokens a definition's generated constants are named by, in every language.
///
/// A language whose constants are named by the token alone claims them against a DSDL name through
/// @ref runtimeOwnedNames. Go composes each with the type's name, so the claim there is on what the
/// composition produces and the emitter reserves that instead.
/// @return The tokens, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<llvm::StringRef> codegenGeneratedConstantTokens();

/// @brief The name a language declares a union's tag under, as a member of the union's type.
///
/// The plan names the tag `_tag_`, which no DSDL field can be named, and each language spells that
/// as a member of its own.
/// @param[in] language Naming language.
/// @return The member's name, valid for the process lifetime.
[[nodiscard]] llvm::StringRef unionTagMemberName(Language language);

/// @brief A name the generator declares, and the fact it states.
struct GeneratedName final
{
    GeneratedFact fact{};

    /// @brief The name; for a fact stated once for each variable-length array field, what the
    ///        field's name follows.
    llvm::StringRef name;
};

/// @brief The members the generator declares in a section's type beside those of its fields and
///        constants, in the order the type declares them.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The members, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<GeneratedName> generatedTypeMembers(Language language);

/// @brief The data members the generator declares in a section's type beside its fields.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The members, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<GeneratedName> generatedDataMembers(Language language);

/// @brief The constants the generator declares for a service beside its sections' types, each
///        named by the service's name and the name here, as the language names a constant declared
///        beside a type.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The constants, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<GeneratedName> generatedServiceConstants(Language language);

/// @brief A constant the generator declares in a definition's module, and the fact it states.
struct ModuleConstantName final
{
    GeneratedFact fact{};

    llvm::StringRef name;

    /// @brief The section whose fact the constant states: empty for a message's, and none for one
    ///        the definition states whatever its sections.
    std::optional<llvm::StringRef> section;
};

/// @brief The constants the generator declares in each definition's module, where a language
///        declares a type's constants in the module.
/// @param[in] language Naming language.
/// @return The constants, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<ModuleConstantName> generatedModuleConstants(Language language);

/// @brief A macro the generator guards a file with, and the fact it states.
struct GuardName final
{
    GeneratedFact fact{};

    /// @brief What comes before the definition's type name.
    llvm::StringRef prefix;

    /// @brief What comes after it.
    llvm::StringRef suffix;

    /// @brief Whether the type name is the one carrying the version, whatever the run's scheme.
    bool versioned{};
};

/// @brief The macros the generator guards a definition's file with, each named by its prefix, the
///        definition's type name and its suffix, projected as a macro.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The macros, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<GuardName> generatedFileGuards(Language language);

/// @brief How an entry point of a section's type is named.
struct EntryPointName final
{
    /// @brief What the body does.
    PlanFunction function{};

    /// @brief The entry point's name; beside the type, what comes before the type's name.
    llvm::StringRef name;

    /// @brief Beside the type, what comes after the type's name.
    llvm::StringRef suffix;

    /// @brief Whether the entry point is a function beside the type rather than a member of it.
    bool beside{};
};

/// @brief The entry points a language declares for a section's type.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The entry points, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<EntryPointName> entryPointNames(Language language);

/// @brief A function wrapping one of a section's entry points, and the fact it states.
struct WrapperName final
{
    GeneratedFact fact{};

    /// @brief What the entry point it wraps does.
    PlanFunction wraps{};

    /// @brief The wrapper's name; beside the type, what comes before the type's name.
    llvm::StringRef name;

    /// @brief Whether the wrapper is a function beside the type rather than a member of it.
    bool beside{};
};

/// @brief The functions a language declares for a section's type wrapping its entry points.
///
/// A language whose emitter names these itself has none here.
/// @param[in] language Naming language.
/// @return The wrappers, valid for the process lifetime.
[[nodiscard]] llvm::ArrayRef<WrapperName> generatedWrappers(Language language);

/// @brief The verbs a member accessor's name begins with, joined to the member's name by `_`.
struct AccessorVerbs final
{
    llvm::StringRef getter;
    llvm::StringRef setter;

    /// @brief Whether a union's tag is claimed ahead of the fields' accessors rather than after them.
    bool tagFirst{};

    /// @brief Whether an accessor is keyed on its field's name as the type declares it, rather than
    ///        as DSDL writes it.
    bool keyedByDeclaredName{};
};

/// @brief The verbs of @p language's member accessors. They are allocated with the type's fields
///        where the language keeps no class of names for fields, and apart from them where it does.
///
/// None where the language's emitter names its accessors itself.
/// @param[in] language Naming language.
/// @return The verbs.
[[nodiscard]] std::optional<AccessorVerbs> memberAccessorVerbs(Language language);

/// @brief Projects @p name as Go names something it exports.
/// @param[in] name The source name.
/// @return The identifier.
[[nodiscard]] std::string codegenToGoExportedIdentifier(llvm::StringRef name);

/// @brief Projects @p name as Go names something it does not export.
/// @param[in] name The source name.
/// @return The identifier.
[[nodiscard]] std::string codegenToGoUnexportedIdentifier(llvm::StringRef name);

/// @brief One region of generated code in which identifiers must not collide.
///
/// A scope is a struct body, a namespace directory, a module's top level -- anywhere two names
/// landing on one identifier would be a redeclaration. Names are declared in source order and the
/// scope hands back the identifier each one gets, appending `_2`, `_3`, ... when a projection is
/// many-to-one. Roles share one pool because they share one C++/Go/TypeScript scope: a Go field and
/// a Go method on the same struct cannot both be `Serialize`.
class NamingScope final
{
public:
    /// @brief Opens a scope for @p language.
    /// @param[in] language Naming language.
    /// @param[in] reserved Identifiers already claimed in this scope by generated code.
    explicit NamingScope(Language language, llvm::ArrayRef<llvm::StringRef> reserved = {});

    /// @brief Claims an identifier for @p sourceName in @p role.
    ///
    /// Declaring the same (role, name) twice returns the first assignment rather than claiming a
    /// second identifier, so a caller that walks a section more than once stays consistent.
    /// @param[in] role What the identifier will be used as.
    /// @param[in] sourceName The DSDL name.
    /// @return The identifier assigned to it.
    std::string declare(IdentifierRole role, llvm::StringRef sourceName);

    /// @brief Claims @p candidate for @p sourceName in @p role, rather than the projection of it.
    ///
    /// For a name composed of parts that are projected apart, where the composition can fold two
    /// sources onto one name: @p sourceName is what keeps them distinct here, and @p candidate is
    /// what the scope allocates from. The caller has already projected it.
    /// @param[in] role What the identifier will be used as.
    /// @param[in] sourceName What distinguishes this name from its siblings.
    /// @param[in] candidate The name to claim, or to take an ordinal from where it is taken.
    /// @return The identifier assigned to it.
    std::string declare(IdentifierRole role, llvm::StringRef sourceName, llvm::StringRef candidate);

    /// @brief Returns the identifier already assigned to (@p role, @p sourceName).
    /// @param[in] role What the identifier is used as.
    /// @param[in] sourceName The DSDL name.
    /// @return The assigned identifier, or the projection of @p sourceName if it was never declared.
    [[nodiscard]] std::string get(IdentifierRole role, llvm::StringRef sourceName) const;

    /// @brief Returns every identifier the scope has assigned.
    /// @return The identifiers, sorted.
    [[nodiscard]] std::vector<std::string> assigned() const;

private:
    /// @brief Key for the assignment map: one source name may appear in two roles.
    [[nodiscard]] static std::string keyOf(IdentifierRole role, llvm::StringRef sourceName);

    Language                     language_;
    llvm::StringSet<>            used_;
    llvm::StringMap<std::string> assigned_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_NAMING_POLICY_H
