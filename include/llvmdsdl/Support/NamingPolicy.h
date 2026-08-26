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
/// This interface centralizes language-specific identifier sanitization and
/// common case projections (snake/pascal/upper-snake) used by emitters.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_NAMING_POLICY_H
#define LLVMDSDL_SUPPORT_NAMING_POLICY_H

#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringRef.h"

namespace llvmdsdl
{

/// @brief Target language for identifier naming projection.
enum class CodegenNamingLanguage
{
    /// @brief C naming policy.
    C,

    /// @brief C++ naming policy.
    Cpp,

    /// @brief Rust naming policy.
    Rust,

    /// @brief Go naming policy.
    Go,

    /// @brief TypeScript naming policy.
    TypeScript,

    /// @brief Python naming policy.
    Python,
};

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
};

/// @brief How one role is named in one language.
///
/// The three booleans are separate from the case style because the backends use every combination of
/// them: a C macro token is escaped but not stropped, a C header stem is neither, and a Go constant
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
    explicit LanguageNamingPolicy(CodegenNamingLanguage language);

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
    CodegenNamingLanguage language_;
};

/// @brief Returns the naming policy for @p language.
/// @param[in] language Naming language.
/// @return A reference to the language's policy, valid for the process lifetime.
[[nodiscard]] const LanguageNamingPolicy& codegenNamingPolicy(CodegenNamingLanguage language);

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
[[nodiscard]] ProjectedIdentifier codegenProjectIdentifierDetailed(CodegenNamingLanguage language,
                                                                   IdentifierRole        role,
                                                                   llvm::StringRef       name);

/// @brief Projects @p name into the identifier @p role calls for in @p language.
///
/// This is the shared pipeline: case projection, then escaping, then stropping, then the optional
/// final upper-casing. The case-explicit helpers below are the same pipeline with the style passed
/// in rather than looked up, and exist because the emitters have not been migrated to roles yet.
/// @param[in] language Naming language.
/// @param[in] role What the identifier will be used as.
/// @param[in] name Source name.
/// @return The language-safe identifier for that role.
[[nodiscard]] std::string codegenProjectIdentifier(CodegenNamingLanguage language,
                                                   IdentifierRole        role,
                                                   llvm::StringRef       name);

/// @brief Returns true when an identifier is a keyword in the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return True when the identifier is reserved.
bool codegenIsKeyword(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief True when @p identifier lies in a namespace @p language reserves for the implementation.
///
/// C reserves a leading `__` and a leading `_` before a capital; C++ reserves those and any
/// identifier containing `__`. The other four reserve no such namespace and always answer false.
///
/// The projection escapes such names on the way in. This asks the question of an identifier that is
/// already projected, which is what a caller composing one -- a disambiguation suffix, a generated
/// prefix -- needs in order not to compose its way into the namespace.
/// @param[in] language Naming language.
/// @param[in] identifier An identifier, already projected.
/// @return True when it is reserved.
[[nodiscard]] bool codegenIsReservedNamespaceIdentifier(CodegenNamingLanguage language, llvm::StringRef identifier);

/// @brief Sanitizes one identifier for the target language.
/// @param[in] language Naming language.
/// @param[in] name Candidate identifier.
/// @return Language-safe identifier.
std::string codegenSanitizeIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into snake_case and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe snake_case identifier.
std::string codegenToSnakeCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into PascalCase and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe PascalCase identifier.
std::string codegenToPascalCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

/// @brief Projects text into UPPER_SNAKE_CASE and sanitizes for the target language.
/// @param[in] language Naming language.
/// @param[in] name Source text.
/// @return Language-safe UPPER_SNAKE_CASE identifier.
std::string codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage language, llvm::StringRef name);

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
    explicit NamingScope(CodegenNamingLanguage language, llvm::ArrayRef<llvm::StringRef> reserved = {});

    /// @brief Claims an identifier for @p sourceName in @p role.
    ///
    /// Declaring the same (role, name) twice returns the first assignment rather than claiming a
    /// second identifier, so a caller that walks a section more than once stays consistent.
    /// @param[in] role What the identifier will be used as.
    /// @param[in] sourceName The DSDL name.
    /// @return The identifier assigned to it.
    std::string declare(IdentifierRole role, llvm::StringRef sourceName);

    /// @brief Returns the identifier already assigned to (@p role, @p sourceName).
    /// @param[in] role What the identifier is used as.
    /// @param[in] sourceName The DSDL name.
    /// @return The assigned identifier, or the projection of @p sourceName if it was never declared.
    [[nodiscard]] std::string get(IdentifierRole role, llvm::StringRef sourceName) const;

private:
    /// @brief Key for the assignment map: one source name may appear in two roles.
    [[nodiscard]] static std::string keyOf(IdentifierRole role, llvm::StringRef sourceName);

    CodegenNamingLanguage        language_;
    llvm::StringSet<>            used_;
    llvm::StringMap<std::string> assigned_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_NAMING_POLICY_H
