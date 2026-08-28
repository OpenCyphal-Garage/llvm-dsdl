//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The values `--target-language` accepts.
///
/// One table, read by the usage line, the help text, and the predicates that decide what a value
/// means. Adding a lane is one row; nothing else in the tool spells the set out.
///
/// This is CLI vocabulary and stays in the tool. `ast`, `mlir` and `obj` are selectors rather than
/// naming languages -- `llvmdsdl::allOutputLanguages()` holds the six the library knows about, and
/// the unit tests hold this table against it.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TOOLS_DSDLC_TARGET_LANGUAGES_H
#define LLVMDSDL_TOOLS_DSDLC_TARGET_LANGUAGES_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

#include <algorithm>
#include <array>
#include <string>

namespace llvmdsdl::dsdlc
{

/// @brief What a `--target-language` value asks the tool to do.
enum class TargetLanguageKind
{
    /// @brief Prints an intermediate representation and generates nothing.
    Dump,

    /// @brief Runs a backend.
    Codegen,
};

/// @brief One accepted `--target-language` value.
struct TargetLanguage final
{
    /// @brief The value as typed on the command line.
    llvm::StringRef name;

    /// @brief Whether the value selects a backend or a dump.
    TargetLanguageKind kind;

    /// @brief Whether the backend writes a source tree the caller then builds.
    ///
    /// False for `obj`, which stages and compiles its own sources: those are an internal detail of
    /// the `.o`/`.a` it produces, not output the caller reads.
    bool emitsSourceTree;
};

/// @brief Every accepted `--target-language` value, in the order the help text lists them.
/// @return The table.
[[nodiscard]] inline llvm::ArrayRef<TargetLanguage> allTargetLanguages()
{
    static constexpr std::array<TargetLanguage, 9> kAll{{
        {"ast", TargetLanguageKind::Dump, false},
        {"mlir", TargetLanguageKind::Dump, false},
        {"c", TargetLanguageKind::Codegen, true},
        {"cpp", TargetLanguageKind::Codegen, true},
        {"rust", TargetLanguageKind::Codegen, true},
        {"go", TargetLanguageKind::Codegen, true},
        {"ts", TargetLanguageKind::Codegen, true},
        {"python", TargetLanguageKind::Codegen, true},
        {"obj", TargetLanguageKind::Codegen, false},
    }};
    return kAll;
}

/// @brief The table's names joined for display.
/// @param[in] separator Text placed between names.
/// @return The joined list.
[[nodiscard]] inline std::string renderTargetLanguages(const llvm::StringRef separator)
{
    std::string rendered;
    for (const auto& entry : allTargetLanguages())
    {
        if (!rendered.empty())
        {
            rendered.append(separator.data(), separator.size());
        }
        rendered.append(entry.name.data(), entry.name.size());
    }
    return rendered;
}

/// @brief Looks a value up in the table.
/// @param[in] language Value as typed on the command line.
/// @return The row, or `nullptr` when the value is not accepted.
[[nodiscard]] inline const TargetLanguage* findTargetLanguage(const llvm::StringRef language)
{
    const auto        all = allTargetLanguages();
    const auto* const it  = std::ranges::find(all, language, &TargetLanguage::name);
    return (it == all.end()) ? nullptr : &*it;
}

/// @brief Whether @p language is accepted at all.
/// @param[in] language Value as typed on the command line.
/// @return True when the table holds it.
[[nodiscard]] inline bool isKnownLanguage(const llvm::StringRef language)
{
    return findTargetLanguage(language) != nullptr;
}

/// @brief Whether @p language runs a backend rather than printing an intermediate representation.
/// @param[in] language Value as typed on the command line.
/// @return True for the codegen lanes.
[[nodiscard]] inline bool isCodegenLanguage(const llvm::StringRef language)
{
    const auto* const entry = findTargetLanguage(language);
    return (entry != nullptr) && entry->kind == TargetLanguageKind::Codegen;
}

/// @brief Whether @p language writes a source tree the caller then builds.
/// @param[in] language Value as typed on the command line.
/// @return True for the source-emitting backends.
[[nodiscard]] inline bool emitsSourceTree(const llvm::StringRef language)
{
    const auto* const entry = findTargetLanguage(language);
    return (entry != nullptr) && entry->emitsSourceTree;
}

}  // namespace llvmdsdl::dsdlc

#endif  // LLVMDSDL_TOOLS_DSDLC_TARGET_LANGUAGES_H
