//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
///
/// @file
/// @brief The vocabulary: the concepts generated code needs a library type for, and the bindings
///        that name one.
///
/// A concept is a role generated code needs filled -- a span over bytes -- stated here as the
/// operations generated code performs on it. Which library fills the role is a binding: a
/// language, the profiles it applies to, a type spelling, the headers that declare it, and the
/// spelling of any operation the library does differently from the standard library. Bindings
/// are data, read from `--vocabulary` files, and the backends never name a library themselves.
///
/// Resolution takes the built-in bindings first and each file in command-line order; a later file
/// replaces an earlier binding of the same concept. A profile that leaves a concept unbound is an
/// error naming the concept and the flag.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CODEGEN_VOCABULARY_H
#define LLVMDSDL_CODEGEN_VOCABULARY_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl::vocabulary
{

/// @brief A role generated code needs a library type for.
enum class Concept : std::uint8_t
{
    /// @brief A view over a run of bytes: a pointer and a count. The generated code takes a buffer
    ///        in one, answers a nested type's bytes in one, and declares a view member as one.
    Span,
};

/// @brief One operation generated code performs on a concept's type.
struct Operation final
{
    /// @brief The name a binding overrides the operation under.
    llvm::StringRef name;

    /// @brief The standard library's spelling, which a binding that omits the operation takes.
    llvm::StringRef spelling;

    /// @brief The placeholders the spelling must use, `self` among them.
    llvm::ArrayRef<llvm::StringRef> placeholders;
};

/// @brief What a concept is: its name in a binding, the placeholders its type spelling takes, and
///        its operations.
struct ConceptSpec final
{
    Concept                         id{};
    llvm::StringRef                 name;
    llvm::ArrayRef<llvm::StringRef> typePlaceholders;
    llvm::ArrayRef<Operation>       operations;
};

/// @brief Every concept.
llvm::ArrayRef<ConceptSpec> concepts();

/// @brief The specification of @p concept.
const ConceptSpec& spec(Concept role);

/// @brief The concept a binding names as @p name, if there is one.
std::optional<Concept> conceptNamed(llvm::StringRef name);

/// @brief A target language: its profiles, and the concepts a binding may name for it. A concept
///        absent from `bindable` is fixed in that language.
struct LanguageSpec final
{
    llvm::StringRef                 name;
    llvm::ArrayRef<llvm::StringRef> profiles;
    llvm::ArrayRef<Concept>         bindable;
};

/// @brief Every language that has profiles or bindable concepts.
llvm::ArrayRef<LanguageSpec> languages();

/// @brief The language named @p name, or null.
const LanguageSpec* languageNamed(llvm::StringRef name);

/// @brief How one library spells a concept.
struct Binding final
{
    /// @brief The type, with the concept's placeholders in braces: `std::span<{element}>`.
    std::string type;

    /// @brief The headers the type needs, as the operand of an `#include`: `<span>` or
    ///        `"cetl/pf20/span.hpp"`.
    std::vector<std::string> includes;

    /// @brief The operations the library spells differently from the standard library, by name.
    std::vector<std::pair<std::string, std::string>> operations;

    /// @brief Where the binding came from: the file's path, or `built-in`.
    std::string origin;
};

/// @brief The bindings one file states.
struct File final
{
    std::string                              path;
    std::string                              language;
    std::vector<std::string>                 profiles;
    std::vector<std::pair<Concept, Binding>> bindings;
};

/// @brief Parses and validates one file's text.
/// @param[in] text The file's contents.
/// @param[in] path Where they came from, for diagnostics and the bindings' origin.
/// @return The file, or the first error found in it.
llvm::Expected<File> parseFile(llvm::StringRef text, llvm::StringRef path);

/// @brief A placeholder and what it stands for.
using Placeholders = llvm::ArrayRef<std::pair<llvm::StringRef, llvm::StringRef>>;

/// @brief The bindings resolved for one language and profile: every bindable concept has one.
class Vocabulary final
{
public:
    /// @brief The binding of @p concept.
    [[nodiscard]] const Binding& binding(Concept role) const;

    /// @brief The type of @p concept with its placeholders filled.
    [[nodiscard]] std::string type(Concept role, Placeholders placeholders) const;

    /// @brief The operation @p name of @p concept with its placeholders filled.
    [[nodiscard]] std::string operation(Concept role, llvm::StringRef name, Placeholders placeholders) const;

    /// @brief The headers @p concept's type needs.
    [[nodiscard]] llvm::ArrayRef<std::string> includes(Concept role) const;

private:
    friend class Set;
    std::vector<std::pair<Concept, Binding>> bindings_;
};

/// @brief The bindings loaded for one target language: the built-in ones and each file passed, in
///        order.
class Set final
{
public:
    /// @brief Loads the built-in bindings of @p language and then each of @p paths.
    /// @param[in] language The target language, which every file must name.
    /// @param[in] paths The `--vocabulary` files, in command-line order.
    /// @return The set, or the first error: a file that cannot be read or fails validation, a file
    ///         for another language, or any file at all for a language that binds no concept.
    static llvm::Expected<Set> load(llvm::StringRef language, llvm::ArrayRef<std::string> paths);

    /// @brief Resolves the bindings for @p profile.
    /// @return The vocabulary, or an error naming a concept nothing binds for that profile.
    [[nodiscard]] llvm::Expected<Vocabulary> resolve(llvm::StringRef profile) const;

    /// @brief The files loaded, in order, after the built-in bindings.
    [[nodiscard]] llvm::ArrayRef<File> files() const;

private:
    const LanguageSpec* language_{nullptr};
    std::vector<File>   files_;
};

}  // namespace llvmdsdl::vocabulary

#endif  // LLVMDSDL_CODEGEN_VOCABULARY_H
