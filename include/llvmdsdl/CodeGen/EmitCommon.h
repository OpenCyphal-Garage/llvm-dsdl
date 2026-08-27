//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared emission helpers for file-write policy and type-key selection.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_EMITCOMMON_H
#define LLVMDSDL_CODEGEN_EMITCOMMON_H

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvmdsdl
{

/// @brief Output-file write policy shared by all code generators.
struct EmitWritePolicy final
{
    /// @brief Do not create or modify any files.
    bool dryRun{false};

    /// @brief Reject writes when destination file already exists.
    bool noOverwrite{false};

    /// @brief File mode applied after writing (POSIX-like bitmask).
    std::uint32_t fileMode{0444U};

    /// @brief Optional sink of absolute generated output paths.
    std::vector<std::string>* recordedOutputs{nullptr};

    /// @brief Optional sink of generated-output path to required type keys.
    std::unordered_map<std::string, std::vector<std::string>>* recordedOutputRequiredTypeKeys{nullptr};
};

/// @brief Returns a canonical type key for one discovered definition.
/// @param[in] info Definition metadata.
/// @return Key in the form `fullName:major:minor`.
std::string definitionTypeKey(const DiscoveredDefinition& info);

/// @brief Result of narrowing a set of type keys to the newest version of each type.
struct NewestVersionSelection final
{
    /// @brief The keys that survive.
    std::unordered_set<std::string> selected;

    /// @brief The keys removed, sorted, so a diagnostic can name them reproducibly.
    std::vector<std::string> dropped;
};

/// @brief Narrows @p seedKeys to the newest version of each full name.
///
/// Most code speaks one version of a type, and a corpus that carries several forces a choice on
/// every consumer of the output: Go cannot compile two versions of a type into one package at all,
/// and C and C++ share a scope across versions. Generating the newest of each removes that choice
/// where it is not wanted, and the version-precise selectors remain for where it is.
///
/// Newest is per *full name*, not per major version. Per-major would preserve Cyphal's compatibility
/// story -- majors are incompatible, minors are not -- but it leaves a type that has two majors still
/// carrying two versions, which is the thing this exists to prevent.
///
/// This narrows the *seed*, not the result. A dependency closure computed afterwards will pull an
/// older version back in if something that survived still references it, which is correct: dropping
/// a version nothing asked for is the point, and dropping one a surviving type needs would be a bug.
/// It also means the result is not guaranteed single-version, so the backends' own guards stay.
///
/// @param[in] semantic Definitions to read names and versions from.
/// @param[in] seedKeys Keys to narrow.
/// @param[in] pinnedKeys Keys the user named precisely enough to mean it; never dropped, and never
///            reported. Naming a version is a request for that version.
/// @return The surviving keys and the dropped ones.
[[nodiscard]] NewestVersionSelection selectNewestTypeVersions(const SemanticModule&                  semantic,
                                                              const std::unordered_set<std::string>& seedKeys,
                                                              const std::unordered_set<std::string>& pinnedKeys);

/// @brief Renders the human-readable notice attached to a `@deprecated` definition.
///
/// @details
/// Every backend renders the same sentence so that the marking reads identically across languages;
/// this is the single source of truth for its wording. DSDL's `@deprecated` carries no author-supplied
/// message, so the notice is derived from the definition's own identity.
///
/// The returned text deliberately begins with `Deprecated: ` because Go's toolchain (gopls,
/// staticcheck, pkg.go.dev) recognises a deprecation only when a doc-comment paragraph starts with
/// exactly that prefix. Backends that need a different prefix strip or replace it.
///
/// @param[in] fullName Full type name, e.g. `uavcan.file.Read`.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return One-line notice with no trailing newline.
std::string deprecationNotice(const std::string& fullName, std::uint32_t majorVersion, std::uint32_t minorVersion);

/// @brief Wraps text to a column budget on word boundaries.
///
/// @details
/// Generated comments inherit whatever width their source was written at, but text synthesised by the
/// compiler has no source width to inherit -- so it must be wrapped here or it lands as one very long
/// line. The showroom renders generated code in a documentation site whose code column fits roughly
/// eighty characters, and the widest comment prefix any backend adds is eight characters, which is
/// where the default budget comes from.
///
/// Words longer than the budget are never broken; a long identifier overflows rather than being cut.
///
/// @param[in] text Text to wrap; existing newlines are not preserved.
/// @param[in] columns Maximum line width, exclusive of any comment prefix the backend adds.
/// @return Wrapped lines, never empty for non-empty input.
std::vector<std::string> wrapCommentText(const std::string& text, std::size_t columns = 72U);

/// @brief Returns a type's documentation with the deprecation notice appended when applicable.
///
/// @details
/// The notice is appended as its own trailing paragraph, separated from any existing documentation by
/// a blank line. Go requires exactly that shape to recognise the deprecation, and it reads correctly
/// in every other language's comment syntax, so all backends share it.
///
/// When @p deprecated is false the input documentation is returned unchanged, so a non-deprecated
/// definition's generated output is byte-identical to what it was before deprecation support existed.
///
/// @param[in] doc Documentation attached to the definition.
/// @param[in] deprecated True when the section carries `@deprecated`.
/// @param[in] fullName Full type name.
/// @param[in] majorVersion Major version.
/// @param[in] minorVersion Minor version.
/// @return Documentation to emit.
AttachedDoc docWithDeprecationNotice(const AttachedDoc& doc,
                                     bool               deprecated,
                                     const std::string& fullName,
                                     std::uint32_t      majorVersion,
                                     std::uint32_t      minorVersion);

/// @brief Builds a hash-set from a list of type keys.
/// @param[in] typeKeys Input key list.
/// @return Hash-set containing all keys.
std::unordered_set<std::string> makeTypeKeySet(const std::vector<std::string>& typeKeys);

/// @brief Criteria selecting when support code is generated.
///
/// @details
/// Support code is everything a backend emits that is not derived from a DSDL definition: runtime
/// headers and modules, package manifests, and scaffolding. It is rendered from content compiled
/// into this binary, so it is generated independently of which definitions were selected.
enum class SupportGeneration : std::uint8_t
{
    /// @brief Generate support code when type code is also generated. Default.
    AsNeeded,

    /// @brief Generate support code whether or not any type code is generated.
    Always,

    /// @brief Generate no support code.
    Never,

    /// @brief Generate support code and no type code.
    Only,
};

/// @brief Indicates whether support artifacts should be written.
/// @param[in] mode Selected criteria.
/// @param[in] anyTypeEmitted True when at least one definition is being emitted this run.
/// @return True when support artifacts should be written.
bool shouldEmitSupport(SupportGeneration mode, bool anyTypeEmitted);

/// @brief Indicates whether per-definition artifacts should be written.
/// @param[in] mode Selected criteria.
/// @return True for every mode except @ref SupportGeneration::Only.
bool shouldEmitTypes(SupportGeneration mode);

/// @brief Indicates whether one definition should be emitted.
/// @param[in] info Definition metadata.
/// @param[in] selectedTypeKeys Optional selected key-set.
/// @param[in] mode Support-generation criteria; @ref SupportGeneration::Only emits no definitions.
/// @return True if the definition is selected or if no selection is provided.
bool shouldEmitDefinition(const DiscoveredDefinition&            info,
                          const std::unordered_set<std::string>& selectedTypeKeys,
                          SupportGeneration                      mode = SupportGeneration::AsNeeded);

/// @brief Writes one generated file under a policy.
///
/// @details
/// When @ref EmitWritePolicy::dryRun is true, no filesystem mutation occurs.
/// In all modes, if @ref EmitWritePolicy::recordedOutputs is set, the resolved
/// absolute path is appended.
///
/// @param[in] path Destination file path.
/// @param[in] content File contents.
/// @param[in] policy Write policy.
/// @return Success or a descriptive I/O error.
llvm::Error writeGeneratedFile(const std::filesystem::path&    path,
                               llvm::StringRef                 content,
                               const EmitWritePolicy&          policy,
                               const std::vector<std::string>& requiredTypeKeys = {});

/// @brief Result of one prune pass, for reporting.
struct PruneReport final
{
    /// @brief Files removed because this run no longer produces them.
    std::vector<std::string> removedFiles;

    /// @brief Directories removed after becoming empty.
    std::vector<std::string> removedDirectories;
};

/// @brief Deletes outputs a previous run of the same tranche produced and this one did not.
///
/// @details
/// A build may split one namespace across several dsdlc invocations -- support code, the embedded
/// catalog, and definitions are the natural seams -- and each invocation owns only the files it
/// emits. Pruning is therefore scoped by a manifest supplied by the caller, one per tranche, rather
/// than by sweeping @p outputRoot: a tranche that swept the output directory would delete its
/// siblings' work.
///
/// The manifest is read before it is rewritten, so the first run of a new tranche prunes nothing.
/// Paths recorded in a manifest but lying outside @p outputRoot are refused rather than removed --
/// a manifest is an input, and an input that can name any path on the filesystem is a way to turn a
/// stale file into an arbitrary deletion.
///
/// Directories emptied by pruning are removed as well, bottom-up, stopping at @p outputRoot. A
/// deleted namespace should not leave an empty directory shaped like one that still exists.
///
/// @param[in] manifestPath Per-tranche manifest to read and then rewrite.
/// @param[in] outputRoot Directory that bounds every removal.
/// @param[in] currentOutputs Absolute paths this run produced.
/// @param[out] report Optional record of what was removed.
/// @return Success, or a descriptive I/O error. A manifest that does not exist is not an error.
llvm::Error pruneStaleOutputs(const std::filesystem::path&    manifestPath,
                              const std::filesystem::path&    outputRoot,
                              const std::vector<std::string>& currentOutputs,
                              PruneReport*                    report = nullptr);

/// @brief Renders one make-style depfile body.
///
/// @details
/// The output format is: `<escaped_target>: <escaped_dep_1> <escaped_dep_2> ...\n`.
/// Dependency inputs are sorted and de-duplicated for deterministic output.
///
/// @param[in] target Make-rule target path.
/// @param[in] deps Dependency path list.
/// @return Rendered depfile text with trailing newline.
std::string renderMakeDepfile(const std::string& target, const std::vector<std::string>& deps);

/// @brief Writes `<outputPath>.d` make depfile for one generated output path.
///
/// @details
/// Dependency paths and target path are normalized to absolute lexical paths.
/// The write path obeys @ref EmitWritePolicy semantics (`dryRun`,
/// `noOverwrite`, `fileMode`, and `recordedOutputs`).
///
/// @param[in] outputPath Generated output path the depfile describes.
/// @param[in] deps Dependency path list.
/// @param[in] policy Write policy.
/// @return Success or a descriptive I/O error.
llvm::Error writeDepfileForGeneratedOutput(const std::filesystem::path&    outputPath,
                                           const std::vector<std::string>& deps,
                                           const EmitWritePolicy&          policy);

/// @brief Writes `<outputPath>.d` depfile from pre-normalized sorted dependencies.
///
/// @details
/// This fast path assumes `normalizedSortedDedupDeps` are already absolute (or
/// otherwise final-form), sorted, and de-duplicated. No dependency
/// normalization, sorting, or de-duplication is performed in this overload.
///
/// @param[in] outputPath Generated output path the depfile describes.
/// @param[in] normalizedSortedDedupDeps Prepared dependency path list.
/// @param[in] policy Write policy.
/// @return Success or a descriptive I/O error.
llvm::Error writeDepfileForGeneratedOutputPrepared(const std::filesystem::path&    outputPath,
                                                   const std::vector<std::string>& normalizedSortedDedupDeps,
                                                   const EmitWritePolicy&          policy);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_EMITCOMMON_H
